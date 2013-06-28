#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"

char *name = "invert";
char *version = VERSION;

int
filter(struct image *img, xmlNodePtr node, void **ctx)
{
	unsigned char *p, *e;
	
	e = img->buf + img->bufsize;
	
	for (p = img->buf; p < e; p++)
		*p = '\xff' - *p;
	
	return 0;
}

