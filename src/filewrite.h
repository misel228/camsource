#ifndef _FILEWRITE_H_
#define _FILEWRITE_H_

#include <libxml/parser.h>

struct fw_ctx
{
	char *path;
	int interval;
	int chmod;
	char *cmd;
};

#endif

