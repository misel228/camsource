#include <stdlib.h>
#include <libxml/parser.h>

#include "config.h"

#include "xmlhelp.h"

int
xml_isnode(xmlNodePtr node, const char *str)
{
	if (!node)
		return 0;
	if (node->type != XML_ELEMENT_NODE)
		return 0;
	if (!strcmp(node->name, str))
		return 1;
	return 0;
}

char *
xml_getcontent(xmlNodePtr node)
{
	if (!node
		|| node->type != XML_ELEMENT_NODE
		|| !node->xml_children
		|| node->xml_children->type != XML_TEXT_NODE
		|| !node->xml_children->content)
		return NULL;
	return node->xml_children->content;
}

char *
xml_getcontent_def(xmlNodePtr node, char *def)
{
	char *ret;
	
	ret = xml_getcontent(node);
	if (!ret)
		return def;
	return ret;
}

int
xml_atoi(xmlNodePtr node, int def)
{
	int i;
	char *ret;
	char *end;
	
	ret = xml_getcontent(node);
	if (!ret)
		return def;
	
	i = (int) strtol(ret, &end, 0);
	if (!i && end == ret)
		return def;
	return i;
}

double
xml_atof(xmlNodePtr node, double def)
{
	double i;
	char *ret;
	char *end;
	
	ret = xml_getcontent(node);
	if (!ret)
		return def;
	
	i = strtod(ret, &end);
	if (!i && end == ret)
		return def;
	return i;
}

char *
xml_attrval(xmlNodePtr node, char *attr)
{
	xmlAttrPtr ap;
	
	if (!node || node->type != XML_ELEMENT_NODE)
		return NULL;
	for (ap = node->properties; ap; ap = ap->next)
	{
		if (!strcmp(ap->name, attr))
			goto found;
	}
	return NULL;
found:
	if (!ap->xml_attrnode || !ap->xml_attrnode->content)
		return NULL;
	return ap->xml_attrnode->content;
}

xmlNodePtr
xml_root(xmlDocPtr doc)
{
	xmlNodePtr node;
	
	if (!doc)
		return NULL;
	
	for (node = doc->xml_rootnode; node; node = node->next)
	{
		if (node->type == XML_ELEMENT_NODE)
			return node;
	}
	
	return NULL;
}

struct xml_privdata *
xml_privdata(xmlNodePtr node)
{
	if (!node->_private) {
		node->_private = malloc(sizeof(struct xml_privdata));
		memset(node->_private, 0, sizeof(struct xml_privdata));
	}
	return node->_private;
}
