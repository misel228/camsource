#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"
#include "xmlhelp.h"

#define MODNAME "bw"

char *name = MODNAME;
char *version = VERSION;

int
filter(struct image *img, xmlNodePtr node, void **ctx)
{
	unsigned char *s, *e;
	int y;
	
	s = img->buf;
	e = s + img->bufsize;
	
	while (s < e)
	{
		y = 0.299 * s[0] + 0.587 * s[1] + 0.114 * s[2];
		if (y < 0)
			y = 0;
		else if (y > 255)
			y = 255;
		s[0] = s[1] = s[2] = (unsigned char) y;
		s += 3;
	}
	
	return 0;
}

