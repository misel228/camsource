#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <libxml/parser.h>

#include "config.h"

#include "ftpup.h"
#define MODULE_THREAD
#include "module.h"
#include "mod_handle.h"
#include "xmlhelp.h"
#include "log.h"
#include "filter.h"
#include "grab.h"
#include "image.h"
#include "jpeg.h"
#include "socket.h"

static int ftpup_load_conf(struct ftpup_ctx *, xmlNodePtr);
static int ftpup_read_ftp_resp(struct ftpup_ctx *, int);
static void ftpup_cmd(struct ftpup_ctx *, char *, ...)
	__attribute__ ((format (printf, 2, 3)));
static int ftpup_setup_data_conn(struct ftpup_ctx *);
static int ftpup_create_data_conn(struct ftpup_ctx *);

#define MODNAME "ftpup"

char *name = MODNAME;
char *version = VERSION;
char *deps[] =
{
	"jpeg_comp",
	"socket",
	NULL
};



int
init(struct module_ctx *mctx)
{
	struct ftpup_ctx fctx;
	int ret;
	
	ret = ftpup_load_conf(&fctx, mctx->node);
	if (ret)
		return -1;
	
	mctx->custom = malloc(sizeof(fctx));
	memcpy(mctx->custom, &fctx, sizeof(fctx));
	
	return 0;
}

void *
thread(void *mctx)
{
	struct timeval tstart, tend;
	struct ftpup_ctx *fctx;
	struct image img;
	struct grab_ctx idx;
	struct jpegbuf jbuf;
	struct peer peer;
	int ret;
	long usecs;
	char tsfnbuf[1024];
	struct tm tm;
	
	fctx = ((struct module_ctx *) mctx)->custom;
	
	memset(&idx, 0, sizeof(idx));
	for (;;)
	{
		gettimeofday(&tstart, NULL);
		filter_get_image(&img, &idx, ((struct module_ctx *) mctx)->node, NULL);
		jpeg_compress(&jbuf, &img, ((struct module_ctx *) mctx)->node);
		
		ret = socket_connect(&peer, fctx->host, fctx->port, 20000);
		if (ret)
		{
			log_log(MODNAME, "Connect to %s:%i failed (code %i, errno: %s)\n",
				fctx->host, fctx->port, ret, strerror(errno));
			goto freensleep;
		}
		
		fctx->peer = &peer;
		
		if (ftpup_read_ftp_resp(fctx, 220))
			goto closenstuff;
		
		ftpup_cmd(fctx, "USER %s\r\n", fctx->user);
		ret = ftpup_read_ftp_resp(fctx, 0);
		
		if (ret == 331)
		{
			ftpup_cmd(fctx, "PASS %s\r\n", fctx->pass);
			if (ftpup_read_ftp_resp(fctx, 230))
				goto closenstuff;
		}
		else if (ret != 230)
		{
			log_log(MODNAME, "Ftp login failure? (got code %i after USER command)\n", ret);
			goto closenstuff;
		}
		
		if (fctx->dir)
		{
			ftpup_cmd(fctx, "CWD %s\r\n", fctx->dir);
			if (ftpup_read_ftp_resp(fctx, 250))
				goto closenstuff;
		}
		
		ftpup_cmd(fctx, "TYPE I\r\n");
		if (ftpup_read_ftp_resp(fctx, 200))
			goto closenstuff;

		ret = ftpup_setup_data_conn(fctx);
		if (ret)
			goto closenstuff;
		
		localtime_r(&tstart.tv_sec, &tm);
		strftime(tsfnbuf, sizeof(tsfnbuf) - 1, fctx->file, &tm);
		
		if (!fctx->safemode)
			ftpup_cmd(fctx, "STOR %s\r\n", tsfnbuf);
		else
			ftpup_cmd(fctx, "STOR %s.tmp\r\n", tsfnbuf);
		
		ret = ftpup_create_data_conn(fctx);

		if (ftpup_read_ftp_resp(fctx, 150))
		{
			socket_close(&fctx->datapeer);
			goto closenstuff;
		}
		
		ret = socket_write(&fctx->datapeer, jbuf.buf, jbuf.bufsize, 20000);
		if (ret != jbuf.bufsize)
		{
			log_log(MODNAME, "Write error while uploading: %s\n", strerror(errno));
			socket_close(&fctx->datapeer);
			goto closenstuff;
		}
		
		socket_close(&fctx->datapeer);
		if (ftpup_read_ftp_resp(fctx, 226))
			goto closenstuff;
		
		if (fctx->safemode)
		{
			ftpup_cmd(fctx, "RNFR %s.tmp\r\n", tsfnbuf);
			if (ftpup_read_ftp_resp(fctx, 350))
				goto closenstuff;
			ftpup_cmd(fctx, "RNTO %s\r\n", tsfnbuf);
			if (ftpup_read_ftp_resp(fctx, 250))
				goto closenstuff;
		}
		
		socket_printf(&peer, "QUIT\r\n");
		ftpup_read_ftp_resp(fctx, 0);
		gettimeofday(&tend, NULL);
		log_log(MODNAME, "Upload of '%s' completed in %2.2f seconds\n",
			tsfnbuf, (tend.tv_sec - tstart.tv_sec) + (tend.tv_usec - tstart.tv_usec)/1000000.0f);
closenstuff:
		socket_close(&peer);
freensleep:
		image_destroy(&img);
		free(jbuf.buf);
		usecs = (tend.tv_sec - tstart.tv_sec) * 1000000 + (tend.tv_usec - tstart.tv_usec);
		if (fctx->interval > 0)
		{
			usecs = 1000000 * fctx->interval - usecs;
			if (usecs > 0)
				usleep(usecs);
		}
		else
		{
			sleep(5);
			log_log(MODNAME, "Negative interval specified, exiting now.\n");
			exit(0);
		}
	}
	
	return NULL;
}

static
int
ftpup_load_conf(struct ftpup_ctx *fctx, xmlNodePtr node)
{
	char *s;
	int mult;
	
	if (!node)
		return -1;
	
	memset(fctx, 0, sizeof(*fctx));
	fctx->port = 21;
	fctx->user = "anonymous";
	fctx->pass = "camsource@";
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "host"))
			fctx->host = xml_getcontent(node);
		else if (xml_isnode(node, "port"))
			fctx->port = xml_atoi(node, fctx->port);
		else if (xml_isnode(node, "user") || xml_isnode(node, "username"))
			fctx->user = xml_getcontent_def(node, fctx->user);
		else if (xml_isnode(node, "pass") || xml_isnode(node, "password"))
			fctx->pass = xml_getcontent_def(node, fctx->pass);
		else if (xml_isnode(node, "dir"))
			fctx->dir = xml_getcontent(node);
		else if (xml_isnode(node, "file"))
			fctx->file = xml_getcontent(node);
		else if (xml_isnode(node, "passive"))
		{
			s = xml_getcontent_def(node, "no");
			if (!strcmp(s, "yes") || !strcmp(s, "on") || !strcmp(s, "1"))
				fctx->passive = 1;
			else
				fctx->passive = 0;
		}
		else if (xml_isnode(node, "safemode"))
		{
			s = xml_getcontent_def(node, "no");
			if (!strcmp(s, "yes") || !strcmp(s, "on") || !strcmp(s, "1"))
				fctx->safemode = 1;
			else
				fctx->safemode = 0;
		}
		else if (xml_isnode(node, "interval"))
		{
			mult = 1;
			s = xml_attrval(node, "unit");
			if (!s)
				s = xml_attrval(node, "units");
			if (s)
			{
				if (!strcmp(s, "sec") || !strcmp(s, "secs") || !strcmp(s, "seconds"))
					;
				else if (!strcmp(s, "min") || !strcmp(s, "mins") || !strcmp(s, "minutes"))
					mult = 60;
				else if (!strcmp(s, "hour") || !strcmp(s, "hours"))
					mult = 3600;
				else if (!strcmp(s, "day") || !strcmp(s, "days"))
					mult = 86400;
				else
				{
					log_log(MODNAME, "Invalid \"unit\" attribute value \"%s\"\n", s);
					return -1;
				}
			}
			fctx->interval = xml_atoi(node, 0) * mult;
		}
	}
	
	if (!fctx->host)
	{
		log_log(MODNAME, "No host specified\n");
		return -1;
	}
	if (fctx->port <= 0 || fctx->port > 0xffff)
	{
		log_log(MODNAME, "Invalid port (%i) specified\n", fctx->port);
		return -1;
	}
	if (!fctx->dir || !fctx->file)
	{
		log_log(MODNAME, "No dir or path specified\n");
		return -1;
	}
	if (fctx->interval == 0)
	{
		log_log(MODNAME, "No or invalid interval specified\n");
		return -1;
	}

	return 0;
}

static
int
ftpup_read_ftp_resp(struct ftpup_ctx *fctx, int expect)
{
	int ret;
	char buf[1024];
	int gotcode;
	
	if (!fctx || !fctx->peer)
		return -1;
	
	gotcode = 0;
	for (;;)
	{
		ret = socket_readline(fctx->peer, buf, sizeof(buf), 20000);
		if (ret)
		{
			log_log(MODNAME, "Connection error or eof on ftp control connection (last command: \"%s\")\n",
				fctx->lastcmd);
			return -1;
		}
		if (!gotcode && strlen(buf) < 3)
		{
			log_log(MODNAME, "Short ftp response line (\"%s\") in response to command \"%s\"\n",
				buf, fctx->lastcmd);
			return -1;
		}
		ret = atoi(buf);
		if (*buf < '0' || *buf > '9' || !ret)
		{
			if (gotcode)
				continue;
invalid:
			log_log(MODNAME, "Invalid ftp response line (\"%s\") in response to command \"%s\"\n",
				buf, fctx->lastcmd);
			return -1;
		}
		
		if (buf[3] == ' ')
			break;
		if (buf[3] != '-')
			goto invalid;
			
		gotcode = 1;
	}
	
	if (expect > 0)
	{
		if (ret != expect)
		{
			log_log(MODNAME, "Did not get expected ftp resonse code after command \"%s\": expected %i, got line \"%s\"\n",
				fctx->lastcmd, expect, buf);
			return -1;
		}
		return 0;
	}
	
	return ret;
}

static
void
ftpup_cmd(struct ftpup_ctx *fctx, char *fmt, ...)
{
	va_list vl;
	char *p;
	
	strncpy(fctx->lastcmd, fmt, 4);
	p = strchr(fctx->lastcmd, ' ');
	if (p)
		*p = '\0';
	
	va_start(vl, fmt);
	socket_vprintf(fctx->peer, fmt, vl);
	va_end(vl);
}

static
int
ftpup_setup_data_conn(struct ftpup_ctx *fctx)
{
	struct peer localpeer, portpeer;
	int ret;
	char buf[1024];
	int h1, h2, h3, h4, p1, p2;
	
	if (!fctx->passive)
	{
		fctx->actpasv.act.listen_fd = socket_listen(0, 0);
		if (fctx->actpasv.act.listen_fd < 0)
		{
			log_log(MODNAME, "Error opening listen socket: %s\n", strerror(errno));
			return -1;
		}
		socket_fill(fctx->peer->fd, &localpeer);
		socket_fill(fctx->actpasv.act.listen_fd, &portpeer);
		ftpup_cmd(fctx, "PORT %u,%u,%u,%u,%u,%u\r\n",
			(localpeer.sin.sin_addr.s_addr >>  0) & 0xff,
			(localpeer.sin.sin_addr.s_addr >>  8) & 0xff,
			(localpeer.sin.sin_addr.s_addr >> 16) & 0xff,
			(localpeer.sin.sin_addr.s_addr >> 24) & 0xff,
			(portpeer.sin.sin_port >> 0) & 0xff,
			(portpeer.sin.sin_port >> 8) & 0xff);
		if (ftpup_read_ftp_resp(fctx, 200))
		{
			close(fctx->actpasv.act.listen_fd);
			return -1;
		}
		
		return 0;
	}

	ftpup_cmd(fctx, "PASV\r\n");
	ret = socket_readline(fctx->peer, buf, sizeof(buf), 20000);
	if (ret || strncasecmp(buf, "227 Entering Passive Mode (", 27))
	{
		log_log(MODNAME, "Unrecognized response to PASV command: %s\n", buf);
		return -1;
	}
	ret = sscanf(buf + 27, "%i,%i,%i,%i,%i,%i",
		&h1, &h2, &h3, &h4, &p1, &p2);
	if (ret != 6)
	{
		log_log(MODNAME, "Response to PASV command didn't match: %s\n", buf);
		return -1;
	}
	snprintf(fctx->actpasv.pasv.ip, sizeof(fctx->actpasv.pasv.ip) - 1, "%i.%i.%i.%i", h1, h2, h3, h4);
	fctx->actpasv.pasv.port = p1 << 8 | p2;
	
	return 0;
}

static
int
ftpup_create_data_conn(struct ftpup_ctx *fctx)
{
	int ret;
	
	if (!fctx->passive)
	{
		ret = socket_accept(fctx->actpasv.act.listen_fd, &fctx->datapeer, 20000);
		if (ret)
		{
			log_log(MODNAME, "Accept() error or timeout: %s\n", strerror(errno));
			close(fctx->actpasv.act.listen_fd);
			return -1;
		}
		close(fctx->actpasv.act.listen_fd);
		
		return 0;
	}

	ret = socket_connect(&fctx->datapeer, fctx->actpasv.pasv.ip, fctx->actpasv.pasv.port, 20000);
	if (ret)
	{
		log_log(MODNAME, "Could not connect to %s:%i in passive mode\n",
			fctx->actpasv.pasv.ip, fctx->actpasv.pasv.port);
		return -1;
	}
	
	return 0;
}

