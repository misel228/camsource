#include <stdio.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"
#include "xmlhelp.h"
#include "log.h"

#define MODNAME "resize"

static struct image *resize_get_dim(struct image *, xmlNodePtr);

char *name = MODNAME;
char *version = VERSION;

int
filter(struct image *img, xmlNodePtr node, void **ctx)
{
	struct image work;
	double xratio, yratio;
	unsigned int x, y, rx, ry;
	unsigned char *r, *rline, *w;
	
	if (!*ctx) {
		*ctx = resize_get_dim(img, node);
		if (!*ctx)
		{
			log_log(MODNAME, "Invalid/missing resize parameters\n");
			return -1;
		}
	}
	memcpy(&work, *ctx, sizeof(work));
	image_new(&work, work.x, work.y);
	
	w = work.buf;
	xratio = (double) img->x / work.x;
	yratio = (double) img->y / work.y;
	for (y = 0; y < work.y; y++)
	{
		ry = yratio * y;
		rline = img->buf + ry * img->x * 3;
		for (x = 0; x < work.x; x++)
		{
			rx = xratio * x;
			r = rline + rx * 3;
			*w++ = *r++;
			*w++ = *r++;
			*w++ = *r++;
		}
	}
	
	image_move(img, &work);

	return 0;
}

static
struct image *
resize_get_dim(struct image *oriimg, xmlNodePtr node)
{
	double scale;
	struct image ret;
	struct image *retptr;
	
	memcpy(&ret, oriimg, sizeof(ret));
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "width"))
			ret.x = xml_atoi(node, ret.x);
		else if (xml_isnode(node, "height"))
			ret.y = xml_atoi(node, ret.y);
		else if (xml_isnode(node, "scale"))
		{
			scale = xml_atof(node, 0);
			if (scale <= 0)
				continue;
			ret.x = (ret.x * scale) / 100;
			ret.y = (ret.y * scale) / 100;
		}
	}
	
	if (ret.x == 0 || ret.y == 0)
		return NULL;
		
	retptr = malloc(sizeof(*retptr));
	memcpy(retptr, &ret, sizeof(*retptr));
	
	return retptr;
}

