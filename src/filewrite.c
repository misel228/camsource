#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"

#define MODULE_THREAD
#include "module.h"
#include "filewrite.h"
#include "mod_handle.h"
#include "xmlhelp.h"
#include "grab.h"
#include "image.h"
#include "jpeg.h"
#include "filter.h"
#include "log.h"
#include "configfile.h"

#define MODNAME "filewrite"

static int fw_load_conf(struct fw_ctx *, xmlNodePtr);

char *name = MODNAME;
char *version = VERSION;
char *deps[] =
{
	"jpeg_comp",
	NULL
};

int
init(struct module_ctx *mctx)
{
	int ret;
	struct fw_ctx fctx;
	
	ret = fw_load_conf(&fctx, mctx->node);
	if (ret)
		return -1;
	
	mctx->custom = malloc(sizeof(fctx));
	memcpy(mctx->custom, &fctx, sizeof(fctx));
	
	return 0;
}

void *
thread(void *arg)
{
	struct fw_ctx *fctx;
	int fd;
	char buf[1024];
	struct image curimg;
	struct grab_ctx idx;
	struct jpegbuf jbuf;
	int ret;
	int cpid;
	char tsfnbuf[1024];
	time_t now;
	struct tm tm;
	
	fctx = ((struct module_ctx *) arg)->custom;
	
	memset(&idx, 0, sizeof(idx));
	for (;;)
	{
		time(&now);
		localtime_r(&now, &tm);
		strftime(tsfnbuf, sizeof(tsfnbuf) - 1, fctx->path, &tm);
		snprintf(buf, sizeof(buf) - 1, "%s.tmp", tsfnbuf);

		filter_get_image(&curimg, &idx, ((struct module_ctx *) arg)->node, NULL);
		jpeg_compress(&jbuf, &curimg, ((struct module_ctx *) arg)->node);
		
		fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd < 0)
		{
			log_log(MODNAME, "Open of %s failed: %s\n", buf, strerror(errno));
			goto freesleeploop;
		}
		
		if (fctx->chmod != -1)
			fchmod(fd, fctx->chmod);
		
		ret = write(fd, jbuf.buf, jbuf.bufsize);
		if (ret != jbuf.bufsize)
		{
			log_log(MODNAME, "Write to %s failed: %s\n",
				buf, (ret == -1) ? strerror(errno) : "short write");
			close(fd);
			unlink(buf);
			goto freesleeploop;
		}
		
		close(fd);
		
		if (fctx->cmd)
		{
			cpid = fork();
			if (cpid < 0)
			{
				log_log(MODNAME, "fork() failed: %s\n", strerror(errno));
				unlink(buf);
				goto freesleeploop;
			}
			if (!cpid)
			{
				/* child */
				close(STDIN_FILENO);
				for (fd = 3; fd < 1024; fd++)
					close(fd);
				
				execlp(fctx->cmd, fctx->cmd, buf, NULL);
				
				/* notreached unless error */
				log_log(MODNAME, "exec(\"%s\") failed: %s\n", fctx->cmd, strerror(errno));
				_exit(0);
			}
			
			do
				ret = waitpid(cpid, NULL, 0);
			while (ret == -1 && errno == EINTR);

			ret = access(buf, F_OK);
			if (ret)
				goto freesleeploop;
		}
		
		ret = rename(buf, tsfnbuf);
		if (ret != 0)
		{
			log_log(MODNAME, "Rename of %s to %s failed: %s\n",
				buf, tsfnbuf, strerror(errno));
			unlink(buf);
			goto freesleeploop;
		}
		
freesleeploop:
		free(jbuf.buf);
		image_destroy(&curimg);
		if (fctx->interval > 0)
			sleep(fctx->interval);
		else
		{
			sleep(5);
			log_log(MODNAME, "Negative interval specified, exiting now.\n");
			exit(0);
		}
	}
}

static
int
fw_load_conf(struct fw_ctx *fctx, xmlNodePtr node)
{
	int mult, val;
	char *s;
	
	if (!node)
		return -1;
	
	memset(fctx, 0, sizeof(*fctx));
	fctx->chmod = -1;
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "path"))
			fctx->path = xml_getcontent(node);
		else if (xml_isnode(node, "cmd"))
			fctx->cmd = xml_getcontent(node);
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
			val = xml_atoi(node, 0);
			if (val == 0)
			{
				log_log(MODNAME, "Invalid interval (%s) specified\n", xml_getcontent(node));
				return -1;
			}
			fctx->interval = val * mult;
		}
		else if (xml_isnode(node, "chmod") || xml_isnode(node, "mode"))
			fctx->chmod = xml_atoi(node, fctx->chmod);
	}
	
	if (!fctx->path || fctx->interval == 0)
	{
		log_log(MODNAME, "Either path or interval not specified\n");
		return -1;
	}
	
	fctx->path = config_homedir(fctx->path);
	fctx->cmd = config_homedir(fctx->cmd);
	
	return 0;
}

