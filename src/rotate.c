#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"
#include "xmlhelp.h"
#include "log.h"

#define MODNAME "rotate"

char *name = MODNAME;
char *version = VERSION;

int
filter(struct image *img, xmlNodePtr node, void **ctx)
{
	struct image work;
	int direct;
	char *cont;
	unsigned char *d, *e, *s;
	int x, dx, dy;
	
	direct = -1;
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "direction"))
		{
			cont = xml_getcontent(node);
			if (!cont)
				continue;
			if (!strcmp(cont, "left"))
				direct = 0;
			else if (!strcmp(cont, "right"))
				direct = 1;
		}
	}
	
	dx = img->x * 3;
	dy = -3 - img->x * img->y * 3;

	switch (direct)
	{
	case 0:	/* left */
		s = img->buf + (img->x - 1) * 3;
		break;
		
	case 1:	/* right */
		s = img->buf + img->x * (img->y - 1) * 3;
		dx = -dx;
		dy = -dy;
		break;
		
	default:
		log_log(MODNAME, "no (valid) direction specified\n");
		return -1;
	}
	
	image_new(&work, img->y, img->x);
	
	d = work.buf;
	e = d + work.bufsize;
	x = 0;
	while (d < e)
	{
		*d++ = s[0];
		*d++ = s[1];
		*d++ = s[2];
		s += dx;
		x++;
		if (x >= work.x)
		{
			s += dy;
			x = 0;
		}
	}

	image_move(img, &work);
	
	return 0;
}

