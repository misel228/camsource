#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"
#include "xmlhelp.h"

char *name = "flip";
char *version = VERSION;

struct flipctx {
	int h, v;
};

static
struct flipctx *
ctx_init(xmlNodePtr node)
{
	struct flipctx *ctx;
	char *cont;

	ctx = malloc(sizeof(*ctx));
	memset(ctx, 0, sizeof(*ctx));
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "horiz"))
		{
			cont = xml_getcontent(node);
			if (cont
				&& (!strcmp(cont, "yes")
					|| !strcmp(cont, "on")
					|| !strcmp(cont, "1")))
			ctx->h = 1;
		}
		else if (xml_isnode(node, "vert"))
		{
			cont = xml_getcontent(node);
			if (cont
				&& (!strcmp(cont, "yes")
					|| !strcmp(cont, "on")
					|| !strcmp(cont, "1")))
			ctx->v = 1;
		}
	}
	
	return ctx;
}

int
filter(struct image *img, xmlNodePtr node, void **instctx)
{
	struct image work;
	struct flipctx *ctx;
	unsigned int x, y, vy;
	unsigned char *r, *w;
	
	if (!*instctx) {
		ctx = ctx_init(node);
		*instctx = ctx;
	}
	else
		ctx = *instctx;
	
	if (!ctx->h && !ctx->v)
		return 0;
	
	image_dup(&work, img);

	/* 3x3
	 * 0/00 RGBRGBRGB
	 * 1/09 RGBRGBRGB
	 * 2/18 RGBRGBRGB */
	
	r = img->buf;
	for (y = 0; y < img->y; y++)
	{
		if (ctx->v)
			vy = img->y - y - 1;
		else
			vy = y;
			
		if (ctx->h)
			w = work.buf + (vy + 1) * work.x * 3 - 3;
		else
			w = work.buf + vy * work.x * 3;
		
		for (x = 0; x < img->x; x++)
		{
			w[0] = *r++;
			w[1] = *r++;
			w[2] = *r++;
			if (ctx->h)
				w -= 3;
			else
				w += 3;
		}
	}
	
	image_move(img, &work);
	
	return 0;
}

