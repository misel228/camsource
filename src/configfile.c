#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libxml/parser.h>

#include "config.h"

#include "configfile.h"
#include "xmlhelp.h"
#include "log.h"

char *globalconfigs[] =
{
	"/etc/camsource.conf",
	"/etc/defaults/camsource",
	"/usr/local/etc/camsource.conf",
	"/usr/local/etc/defaults/camsource",
	"./camsource.conf",
	NULL
};
char localconfig[256];
char *ourconfig;

xmlDocPtr configdoc;

int
config_init(char *customconfig)
{
	char *s, **ss;
	
	if (customconfig && !access(customconfig, R_OK))
	{
		ourconfig = customconfig;
		goto found;
	}
	
	s = getenv("HOME");
	if (s)
		snprintf(localconfig, sizeof(localconfig) - 1, "%s/.camsource", s);
	
	if (*localconfig && !access(localconfig, R_OK))
	{
		ourconfig = localconfig;
		goto found;
	}
	else
	{
		for (ss = globalconfigs; *ss; ss++)
		{
			if (access(*ss, R_OK))
				continue;
			ourconfig = *ss;
			goto found;
		}
	}
	return -1;

found:
	return 0;
}

int
config_load()
{
	xmlNodePtr node;
	
	configdoc = xmlParseFile(ourconfig);
	if (!configdoc)
		return -1;
	
	node = xml_root(configdoc);
	if (!xml_isnode(node, "camsourceconfig"))
	{
		printf("Root node isn't 'camsourceconfig'\n");
		xmlFreeDoc(configdoc);
		configdoc = NULL;
		return -1;
	}
	
	return 0;
}

char *
config_homedir(char *dirspec)
{
	char *env, *temp;
	
	if (!dirspec)
		return NULL;
	
	if (strncmp(dirspec, "~/", 2))
		return strdup(dirspec);
		
	env = getenv("HOME");
	if (!env)
	{
		log_log(NULL, "Invalid path spec: HOME not set. (Arg: \"%s\")\n", dirspec);
		return strdup(dirspec);
	}
	
	temp = malloc(strlen(env) + strlen(dirspec));
	sprintf(temp, "%s%s", env, dirspec + 1);
	
	return temp;
}

