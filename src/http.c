#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#include "http.h"
#define MODULE_THREAD
#include "module.h"
#include "xmlhelp.h"
#include "socket.h"
#include "grab.h"
#include "filter.h"
#include "jpeg.h"
#include "mod_handle.h"
#include "log.h"
#include "image.h"

#define MODNAME "http"

struct vpathcfg {
	int fps;
	char *auth;
	int raw:1;
};

static int http_load_conf(struct http_ctx *, xmlNodePtr);
static void *http_conn(void *);
static int http_path_ismatch(xmlNodePtr, char *);
static void http_err(struct peer *, char *, char *);
static void http_fill_vpathcfg(xmlNodePtr, struct vpathcfg *);
static void unbase64(unsigned char *, unsigned char *);

char *name = MODNAME;
char *version = VERSION;
char *deps[] =
{
	"jpeg_comp",
	"socket",
	NULL
};

int
init(struct module_ctx *mod_ctx)
{
	int ret;
	struct http_ctx *http_ctx;
	
	if (!mod_ctx->node)
		return -1;
	
	http_ctx = malloc(sizeof(*http_ctx));
	ret = http_load_conf(http_ctx, mod_ctx->node);
	if (ret)
	{
		free(http_ctx);
		return -1;
	}
	
	http_ctx->listen_fd = socket_listen(http_ctx->port, 0);
	if (http_ctx->listen_fd == -1)
	{
		log_log(MODNAME, "Failed to open listen socket: %s\n", strerror(errno));
		free(http_ctx);
		return -1;
	}
	
	mod_ctx->custom = http_ctx;
	
	return 0;
}

void *
thread(void *mod_ctx)
{
	int ret;
	struct http_ctx *http_ctx;
	struct http_peer *http_peer;
	
	http_ctx = ((struct module_ctx *) mod_ctx)->custom;
	
	for (;;)
	{
		http_peer = malloc(sizeof(*http_peer));
		http_peer->mod_ctx = mod_ctx;
		ret = socket_accept_thread(http_ctx->listen_fd, &http_peer->peer, http_conn, http_peer);
		if (ret)
		{
			free(http_peer);
			log_log(MODNAME, "accept() error: %s\n", strerror(errno));
			sleep(1);
			continue;
		}
	}
	
	return NULL;
}

static
int
http_load_conf(struct http_ctx *ctx, xmlNodePtr node)
{
	memset(ctx, 0, sizeof(*ctx));
	
	ctx->listen_fd = -1;
	ctx->port = 9192;
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "port"))
			ctx->port = xml_atoi(node, ctx->port);
	}
	
	if (ctx->port <= 0 || ctx->port > 0xffff)
	{
		log_log(MODNAME, "Invalid port %u\n", ctx->port);
		return -1;
	}
	
	return 0;
}

static
void *
http_conn(void *peer_p)
{
	struct http_peer http_peer;
	char buf[1024];
	int ret;
	char *url;
	char *p;
	char *httpver;
	xmlNodePtr subnode;
	struct image img;
	struct grab_ctx idx;
	struct jpegbuf jpegbuf;
	int count;
	char *error;
	struct vpathcfg vpathcfg;
	char *contenttype;
	char addheaders[256];
	char authorization[256];
	
	memcpy(&http_peer, peer_p, sizeof(http_peer));
	free(peer_p);
	
	log_log(MODNAME, "New connection from %s:%i\n",
		socket_ip(&http_peer.peer), socket_port(&http_peer.peer));
	
	count = 0;
	
	do {
		ret = socket_readline(&http_peer.peer, buf, sizeof(buf), 20000);
		if (ret)
		{
readlineerr:
			switch (ret)
			{
			case -1:
				log_log(MODNAME, "Read error on connection from %s:%i (%s)\n",
					socket_ip(&http_peer.peer), socket_port(&http_peer.peer), strerror(errno));
				break;
			case -2:
				log_log(MODNAME, "Timeout on connection from %s:%i\n",
					socket_ip(&http_peer.peer), socket_port(&http_peer.peer));
				break;
			case -3:
				if (count)
					break;
				log_log(MODNAME, "EOF on connection from %s:%i before full HTTP request was received\n",
					socket_ip(&http_peer.peer), socket_port(&http_peer.peer));
				break;
			}
			goto closenout;
		}
		
		if (strncmp("GET ", buf, 4))
		{
			log_log(MODNAME, "Invalid HTTP request (not GET) from %s:%i\n",
				socket_ip(&http_peer.peer), socket_port(&http_peer.peer));
			goto closenout;
		}
		
		url = buf + 4;
		
		p = strchr(url, ' ');
		if (!p)
			httpver = NULL;
		else
		{
			*p++ = '\0';
			if (strncmp("HTTP/", p, 5))
				httpver = NULL;
			else
			{
				httpver = p + 5;
				if (!*httpver)
					httpver = NULL;
			}
		}
		
		if (!*url || !httpver)
		{
			log_log(MODNAME, "Invalid HTTP request or version from %s:%i\n",
				socket_ip(&http_peer.peer), socket_port(&http_peer.peer));
			goto closenout;
		}
		
		p = strchr(url, '?');
		if (p)
			*p = '\0';
		
		log_log(MODNAME, "Request for %s from %s:%i\n",
			url, socket_ip(&http_peer.peer), socket_port(&http_peer.peer));
	
		error = NULL;
		for (subnode = http_peer.mod_ctx->node->xml_children; subnode; subnode = subnode->next)
		{
			if (!xml_isnode(subnode, "vpath"))
				continue;
			if (!http_path_ismatch(subnode, url))
				continue;
			goto match;
		}
		error = "404 Not found";
		
match:
		*authorization = '\0';
		for (;;)
		{
			ret = socket_readline(&http_peer.peer, buf, sizeof(buf), 20000);
			if (ret)
				goto readlineerr;
			if (!*buf)
				break;
			if (!strncasecmp(buf, "authorization:", 14)) {
				p = buf + 14;
				while (*p == ' ')
					p++;
				if (strncasecmp(p, "basic ", 6))
					continue;
				p += 6;
				while (*p == ' ')
					p++;
				if (strlen(p) >= sizeof(authorization)) {
					log_log(MODNAME, "Warning: very long Authorization: header received\n");
					continue;
				}
				unbase64(authorization, p);
			}
		}
		
		if (error) {
			http_err(&http_peer.peer, error, NULL);
			goto closenout;
		}
		
		http_fill_vpathcfg(subnode, &vpathcfg);
		if (vpathcfg.auth) {
			if (strcmp(authorization, vpathcfg.auth)) {
				http_err(&http_peer.peer, "401 Unauthorized", "WWW-Authenticate: Basic realm=\"camsource\"\r\n");
				goto closenout;
			}
		}

		if (vpathcfg.fps > 0)
		{
			snprintf(buf, sizeof(buf) - 1,
				"HTTP/1.0 200 OK\r\n"
				"Server: " PACKAGE_STRING "\r\n"
				"Connection: close\r\n"		/* <-- does that make sense? */
				"Pragma: no-cache\r\n"
				"Cache-Control: no-cache\r\n"
				"Content-Type: multipart/x-mixed-replace;boundary=Juigt9G7bb7sfdgsdZGIDFsjhn\r\n"
				"\r\n"
				"--Juigt9G7bb7sfdgsdZGIDFsjhn\r\n");
			socket_write(&http_peer.peer, buf, strlen(buf), 20000);
		}
		
		memset(&idx, 0, sizeof(idx));
		do
		{
			filter_get_image(&img, &idx, http_peer.mod_ctx->node, subnode, NULL);
			if (!vpathcfg.raw) {
				jpeg_compress(&jpegbuf, &img, subnode);
				contenttype = "image/jpeg";
				addheaders[0] = '\0';
			}
			else {
				jpegbuf.buf = img.buf;
				jpegbuf.bufsize = img.bufsize;
				contenttype = "application/octet-stream";
				snprintf(addheaders, sizeof(addheaders) - 1,
					"X-Image-Width: %i\r\n"
					"X-Image-Height: %i\r\n",
					img.x, img.y);
			}
			
			if (!vpathcfg.fps)
				snprintf(buf, sizeof(buf) - 1,
					"HTTP/1.0 200 OK\r\n"
					"Server: " PACKAGE_STRING "\r\n"
					"Content-Length: %i\r\n"
					"Connection: Keep-alive\r\n"
					"Pragma: no-cache\r\n"
					"Cache-Control: no-cache\r\n"
					"Content-Type: %s\r\n"
					"%s"
					"\r\n",
					jpegbuf.bufsize, contenttype,
					addheaders);
			else
				snprintf(buf, sizeof(buf) - 1,
					"Content-Length: %i\r\n"
					"Content-Type: %s\r\n"
					"%s"
					"\r\n",
					jpegbuf.bufsize, contenttype,
					addheaders);
			ret = socket_write(&http_peer.peer, buf, strlen(buf), 20000);
			if (ret > 0)
				ret = socket_write(&http_peer.peer, jpegbuf.buf, jpegbuf.bufsize, 20000);
	
			count++;
	
			if (vpathcfg.fps && ret > 0)
			{
				ret = socket_write(&http_peer.peer, "\r\n--Juigt9G7bb7sfdgsdZGIDFsjhn\r\n", 32, 20000);
				usleep(1000000 / vpathcfg.fps);
			}
			
			image_destroy(&img);
			if (!vpathcfg.raw)
				free(jpegbuf.buf);
		}
		while (vpathcfg.fps > 0 && ret > 0);
	}
	while (ret > 0);

closenout:
	log_log(MODNAME, "Closing connection from %s:%i, %i frame(s) served\n",
		socket_ip(&http_peer.peer), socket_port(&http_peer.peer),
		count);

	socket_close(&http_peer.peer);
	return NULL;
}

static
int
http_path_ismatch(xmlNodePtr node, char *path)
{
	int ret = 0;
	char *active;
	
	active = xml_attrval(node, "active");
	if (active && strcmp(active, "yes"))
		return 0;
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "disable")) {
			if (!strcmp("yes", xml_getcontent_def(node, "")))
				return 0;
		}
		
		if (!xml_isnode(node, "path"))
			continue;
		if (!strcmp(path, xml_getcontent_def(node, "")))
			ret = 1;
	}
	
	return ret;
}

static
void
http_err(struct peer *peer, char *err, char *addheaders)
{
	char buf[1024];
	
	snprintf(buf, sizeof(buf) - 1,
		"HTTP/1.0 %s\r\n"
		"Server: " PACKAGE_STRING "\r\n"
		"Content-Length: %i\r\n"
		"Connection: close\r\n"
		"Pragma: no-cache\r\n"
		"Cache-Control: no-cache\r\n"
		"Content-Type: text/html\r\n"
		"%s"
		"\r\n"
		"<html><body>%s</body></html>\r\n",
		err, strlen(err) + 28, addheaders ? : "", err);
	socket_write(peer, buf, strlen(buf), 20000);
}

static
void
http_fill_vpathcfg(xmlNodePtr node, struct vpathcfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "fps"))
			cfg->fps = xml_atoi(node, 0);
		else if (xml_isnode(node, "raw") && !strcmp("yes", xml_getcontent_def(node, "")))
			cfg->raw = 1;
		else if (xml_isnode(node, "auth"))
			cfg->auth = xml_getcontent(node);
	}
}

static void
unbase64(unsigned char *dst, unsigned char *src)
{
	/* yuck */
	static unsigned char lookup[256] = {
		/*   0 */ 255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		/*  16 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		/*  32 */ 0,0,0,0,0,0,0,0,0,0,0,62,0,0,0,63,
		/*  48 */ 52,53,54,55,56,57,58,59,60,61,0,0,0,255,0,0,
		/*  64 */ 0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
		/*  80 */ 15,16,17,18,19,20,21,22,23,24,25,0,0,0,0,0,
		/*  96 */ 0,26,27,28,30,30,31,32,33,34,35,36,37,38,39,40,
		/* 128 */ 41,42,43,44,45,46,47,48,49,50,51,0,0,0,0,0,
	};
	
	int extract[4];
	
	while (*src) {
		if ((extract[0] = lookup[src[0]]) == 255 || (extract[1] = lookup[src[1]]) == 255)
			break;
		
		*dst++ = (extract[0] << 2) | (extract[1] >> 4);
		
		if ((extract[2] = lookup[src[2]]) == 255)
			break;
			
		*dst++ = (extract[1] << 4) | (extract[2] >> 2);

		if ((extract[3] = lookup[src[3]]) == 255)
			break;
			
		*dst++ = (extract[2] << 6) | extract[3];
		
		src += 4;
	}
	
	*dst = '\0';
}

