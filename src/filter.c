#include <stdio.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <unistd.h>
#include <libxml/parser.h>

#include "config.h"

#include "filter.h"
#include "image.h"
#include "mod_handle.h"
#include "xmlhelp.h"
#include "grab.h"

/* $Id: filter.c,v 1.11 2003/01/26 19:50:22 dfx Exp $ */

static void preparsefilters(xmlNodePtr);

int
filter_apply(struct image *img, xmlNodePtr node)
{
	struct module *mod;
	char *filtername;
	int (*filter)(struct image *, xmlNodePtr, void **);
	int ret;
	struct xml_privdata *privdata;
	
	preparsefilters(node);
	
	privdata = xml_privdata(node);
	
	for (node = privdata->filterlist; node; node = node->next)
	{
		filtername = xml_attrval(node, "name");
		mod = mod_find(filtername, xml_attrval(node, "alias"));
		if (mod)
		{
			filter = dlsym(mod->dlhand, "filter");
			if (filter) {
				privdata = xml_privdata(node);
				ret = filter(img, node, &privdata->filterinststorage);
				if (ret > 0)
					return ret;
			}
			else
				printf("Module %s has no \"filter\" routine\n", filtername);
		}
	}
	
	return 0;
}

void
filter_get_image(struct image *img, struct grab_ctx *ctx, ...)
{
	int ret;
	xmlNodePtr node;
	va_list val;
	char *threadname;
	
	threadname = NULL;
	
	va_start(val, ctx);
	while ((node = va_arg(val, xmlNodePtr))) {
		for (node = node->xml_children; node; node = node->next) {
			if (!xml_isnode(node, "grabdev"))
				continue;
			threadname = xml_getcontent(node);
			goto donefind;
		}
	}
donefind:
	va_end(val);
	
	if (!threadname)
		threadname = "default";

	for (;;) {
		grab_get_image(img, ctx, threadname);
		
		ret = 0;
		va_start(val, ctx);
		while ((node = va_arg(val, xmlNodePtr))) {
			ret = filter_apply(img, node);
			if (ret > 0)
				break;
		}
		va_end(val);
		
		if (ret <= 0)
			break;
		
		image_destroy(img);
		
		usleep(ret);
	}
}

static
void
preparsefilters(xmlNodePtr node)
{
	struct xml_privdata *privdata;
	xmlNodePtr newnode;
	
	privdata = xml_privdata(node);
	if (privdata->filterlist)
		return;
	
	for (node = node->xml_children; node; node = node->next)
	{
		if (!xml_isnode(node, "filter"))
			continue;
		if (!xml_attrval(node, "name"))
		{
			printf("<filter> without name\n");
			continue;
		}
		newnode = xmlCopyNode(node, 1);
		if (privdata->filterlist)
			xmlAddSibling(privdata->filterlist, newnode);
		else
			privdata->filterlist = newnode;
	}
}
