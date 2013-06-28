#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <libxml/parser.h>
#include <stdarg.h>

#include "config.h"

#include "log.h"
#include "configfile.h"
#include "xmlhelp.h"

int
log_open()
{
	int fd;
	xmlNodePtr node;
	char *file;
	
	node = xml_root(configdoc);
	if (!node)
		return -1;
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "logfile"))
			goto found;	/* break */
	}
	return -1;
	
found:
	file = xml_getcontent(node);
	if (!file || !*file)
		return -1;
	
	file = config_homedir(file);
	fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
	free(file);
	
	if (fd < 0)
		return -1;
	return fd;
}

void
log_replace_bg(int fd)
{
	char buf[256];
	struct tm tm;
	time_t now;
	int nullfd;
	
	printf("Main init done and logfile opened.\n");
	printf("Closing stdout and going into background...\n");
	nullfd = open("/dev/null", O_RDONLY);
	dup2(nullfd, STDIN_FILENO);
	close(nullfd);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	close(fd);
	if (!fork()) {
		if (!fork())
		{
			setsid();
			setpgrp();
			time(&now);
			localtime_r(&now, &tm);
			strftime(buf, sizeof(buf) - 1, "%b %d %Y %T", &tm);
			printf("-----\nLog file opened at %s\n", buf);
			return;
		}
	}
	exit(0);
}

void
log_log(char *modname, char *format, ...)
{
	va_list vl;
	char buf[64];
	struct tm tm;
	time_t now;
	
	time(&now);
	localtime_r(&now, &tm);
	strftime(buf, sizeof(buf) - 1, "%b %d %Y %T", &tm);
	if (modname)
		fprintf(stderr, "[%s / %s] ", buf, modname);
	else
		fprintf(stderr, "[%s] ", buf);
	
	va_start(vl, format);
	vfprintf(stderr, format, vl);
	va_end(vl);
}

