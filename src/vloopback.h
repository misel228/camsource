#ifndef _VLOOPBACK_H_
#define _VLOOPBACK_H_

#include <libxml/parser.h>

struct vloopback_ctx
{
	char *dev_name;
	int width;
	int height;
	int format;
	int dev_out;
};

#endif

