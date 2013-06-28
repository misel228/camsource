#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"
#include "xmlhelp.h"
#include "log.h"

#define MODNAME "motiondetect"

char *name = MODNAME;
char *version = VERSION;

struct mdetectctx {
	int pixeldiff;
	int minthres, maxthres;
	int delay;
	struct image img;
};

static
struct mdetectctx *
ctx_init(xmlNodePtr node, struct image *img)
{
	struct mdetectctx *ctx;
	
	ctx = malloc(sizeof(*ctx));
	memset(ctx, 0, sizeof(*ctx));
	
	for (node = node->xml_children; node; node = node->next) {
		if (xml_isnode(node, "pixeldiff"))
			ctx->pixeldiff = (xml_atof(node, 0) * 768) / 100;
		else if (xml_isnode(node, "minthres"))
			ctx->minthres = (xml_atof(node, 0) * img->bufsize) / 100;
		else if (xml_isnode(node, "maxthres"))
			ctx->maxthres = (xml_atof(node, 0) * img->bufsize) / 100;
		else if (xml_isnode(node, "delay"))
			ctx->delay = xml_atoi(node, 0) * 1000;
	}
	
	return ctx;
}

static
int
pixeldiff(unsigned char a, unsigned char b)
{
	int diff;
	
	diff = a - b;
	if (diff < 0)
		diff *= -1;
	return diff;
}

int
filter(struct image *img, xmlNodePtr node, void **instctx)
{
	struct mdetectctx *ctx;
	unsigned char *curbuf, *refbuf;
	int togo;
	int diff, diffs;
	
	if (!*instctx) {
		ctx = ctx_init(node, img);
		*instctx = ctx;
	}
	else
		ctx = *instctx;
	
	if (!ctx->img.buf) {
		image_copy(&ctx->img, img);
		return ctx->delay;
	}
	
	if (ctx->img.bufsize != img->bufsize) {
		log_log(MODNAME, "Motion detect buffer size mismatch!?\n");
		return -1;
	}
	
	curbuf = img->buf;
	refbuf = ctx->img.buf;
	togo = img->bufsize;
	diffs = 0;
	while (togo > 0) {
		diff = pixeldiff(curbuf[0], refbuf[0]);
		diff += pixeldiff(curbuf[1], refbuf[1]);
		diff += pixeldiff(curbuf[2], refbuf[2]);
		
		if (diff > ctx->pixeldiff)
			diffs++;
			
		refbuf[0] = (curbuf[0] + refbuf[0]) / 2;
		refbuf[1] = (curbuf[1] + refbuf[1]) / 2;
		refbuf[2] = (curbuf[2] + refbuf[2]) / 2;

		togo -= 3;
		curbuf += 3;
		refbuf += 3;
	}
	
	if (diffs < ctx->minthres || diffs > ctx->maxthres)
		return ctx->delay;
	
	return 0;
}

