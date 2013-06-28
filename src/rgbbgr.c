#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"

char *name = "rgbbgr";
char *version = VERSION;

int
filter(struct image *img, xmlNodePtr node, void **ctx)
{
	unsigned char *p, *e;
	unsigned char t;
	
	e = img->buf + img->bufsize;
	
	for (p = img->buf; p < e; p += 3)
	{
		t = p[0];
		p[0] = p[2];
		p[2] = t;
	}
	
	return 0;
}

