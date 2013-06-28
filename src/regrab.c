#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#define MODULE_FILTER
#include "module.h"
#include "image.h"
#include "xmlhelp.h"
#include "log.h"

#define MODNAME "regrab"

char *name = MODNAME;
char *version = VERSION;

struct regrabctx {
	int count;
	int sleeptime;
	int upto;
};

static
struct regrabctx *
regrab_init(xmlNodePtr node)
{
	struct regrabctx *ctx;
	
	ctx = malloc(sizeof(*ctx));
	memset(ctx, 0, sizeof(*ctx));
	
	for (node = node->xml_children; node; node = node->next) {
		if (xml_isnode(node, "times"))
			ctx->count = xml_atoi(node, 0);
		else if (xml_isnode(node, "delay"))
			ctx->sleeptime = xml_atoi(node, 0);
	}
	
	return ctx;
}

int
filter(struct image *img, xmlNodePtr node, void **instctx)
{
	struct regrabctx *ctx;
	
	if (!*instctx) {
		ctx = regrab_init(node);
		*instctx = ctx;
	}
	else
		ctx = *instctx;
		
	ctx->upto++;
	if (ctx->upto > ctx->count) {
		ctx->upto = 0;
		return 0;
	}
	
	return ctx->sleeptime * 1000;
}

