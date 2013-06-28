#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
#include <libxml/parser.h>

#include "config.h"

#include "mod_handle.h"
#include "configfile.h"
#include "xmlhelp.h"

/* Given an xml node, return alias name if present, otherwise module name */
static char *mod_get_aliasname(xmlNodePtr, char *);
/* Given a module name, try to dlopen its lib. Lib names are created from predefined patterns */
static void *mod_try_dlopen(char *);
/* Make sure the filename matches the module's built in name. Return -1 on error. */
static int mod_validate(void *, char *);
/* Look at the module's dep list, and load each mod. Return -1 on error */
static int mod_load_deps(struct module *);
/* Calls module's init(). Returns init()'s return value (0 == success) */
static int mod_init_mod(struct module *);
/* Cleans up module struct and dlclose()s lib if not used any more */
static void mod_close(struct module *);
static xmlNodePtr mod_find_config(char *);
/* Loads one module by name, optionally with an xml config */
static int mod_load(char *, xmlNodePtr);


/* Built once on startup, then never modified and readonly */
static struct module modules[MAX_MODULES];

void
mod_init()
{
}

void
mod_load_all()
{
	xmlNodePtr node;
	int modidx;
	char *modname, *loadyn;
	
	modidx = 0;
	node = xml_root(configdoc);
	printf("Loading modules:\n");
	for (node = node->xml_children; node; node = node->next)
	{
		if (!xml_isnode(node, "module"))
			continue;
		modname = xml_attrval(node, "name");
		if (!modname)
		{
			printf("<module> tag without valid name property\n");
			continue;
		}
		loadyn = xml_attrval(node, "active");
		if (!loadyn
			|| (strcmp(loadyn, "yes")
				&& strcmp(loadyn, "1")
				&& strcmp(loadyn, "on")))
			continue;
		
		mod_load(modname, node);
	}
}

static
int
mod_load(char *mod, xmlNodePtr node)
{
	int ret;
	int i, idx;
	char *alias;
	void *dlh;
	char **version;
	
	/* check if mod is already loaded */
	alias = mod_get_aliasname(node, mod);

	idx = -1;
	dlh = NULL;
	for (i = 0; i < MAX_MODULES; i++)
	{
		if (!modules[i].name)
		{
			/* save index of first free slot */
			if (idx == -1)
				idx = i;
			continue;
		}
		if (!strcmp(modules[i].name, mod))
		{
			/* the lib is dlopened already */
			if (!strcmp(alias, modules[i].alias))
				return 0;
			/* but we're looking for an alias */
			dlh = modules[i].dlhand;
		}
	}
	
	if (idx == -1)
	{
		printf("Max number of modules exceeded when trying to load %s/%s\n", mod, alias);
		return -1;
	}
	
	if (!dlh)
	{
		dlh = mod_try_dlopen(mod);
		if (!dlh)
		{
			printf("Failed to load module %s\n", mod);
			printf("Last dlopen error: %s\n", dlerror());
			return -1;
		}
		
		ret = mod_validate(dlh, mod);
		if (ret)
		{
			dlclose(dlh);
			return -1;
		}
	}

	modules[idx].dlhand = dlh;
	modules[idx].name = strdup(mod);
	modules[idx].alias = strdup(alias);
	modules[idx].ctx.node = node;

	ret = mod_load_deps(&modules[idx]);
	if (ret)
	{
		printf("Failed to load dependencies for module %s\n", mod);
		mod_close(&modules[idx]);
		return -1;
	}
	
	ret = mod_init_mod(&modules[idx]);
	if (ret)
	{
		printf("Failed to initialize module %s (code %i)\n", mod, ret);
		mod_close(&modules[idx]);
		return -1;
	}
	
	version = dlsym(modules[idx].dlhand, "version");
	printf("  %s (alias %s) version %s OK\n",
		modules[idx].name, modules[idx].alias,
		(version && *version) ? *version : "unknown");

	return 0;
}

void
mod_start_all()
{
	int i;
	void *(*thread)(void *);
	pthread_attr_t attr;
	
	for (i = 0; i < MAX_MODULES; i++)
	{
		if (!modules[i].name)
			continue;
		thread = dlsym(modules[i].dlhand, "thread");
		if (!thread)
			continue;
		
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&modules[i].ctx.tid, &attr, thread, &modules[i].ctx);
		pthread_attr_destroy(&attr);
	}
}

struct module *
mod_find(char *mod, char *alias)
{
	int i;
	
	for (i = 0; i < MAX_MODULES; i++)
	{
		if (!modules[i].name)
			continue;
		if (!strcmp(modules[i].name, mod))
		{
			if (!alias || !strcmp(alias, modules[i].alias))
				return &modules[i];
		}
	}
	
	return NULL;
}

static
char *
mod_get_aliasname(xmlNodePtr node, char *mod)
{
	char *ret;
	
	if (!node)
		return mod;
	ret = xml_attrval(node, "alias");
	if (ret)
		return ret;
	return mod;
}

static
void *
mod_try_dlopen(char *mod)
{
	int i;
	char modname[1024];
	char *modpatterns[] =
	{
#ifdef PKGLIBDIR
		PKGLIBDIR "/lib%s.so",
#endif
#ifdef PREFIX
		PREFIX "/camsource/lib%s.so",
		PREFIX "/lib/lib%s.so",
#endif
		"/usr/local/lib/camsource/lib%s.so",
		"/usr/lib/camsource/lib%s.so",
		"lib%s.so",
		".libs/lib%s.so",
		"src/.libs/lib%s.so",
		NULL
	};
	void *dlh;
	
	for (i = 0; modpatterns[i]; i++)
	{
		snprintf(modname, sizeof(modname) - 1, modpatterns[i], mod);
		/* We have to use lazy symbol resolving, otherwise we couldn't
		 * load dependency libs */
		dlh = dlopen(modname, RTLD_LAZY | RTLD_GLOBAL);
		if (!dlh)
			continue;
		return dlh;
	}
	return NULL;
}

static
int
mod_validate(void *dlh, char *mod)
{
	char **name;
	
	name = dlsym(dlh, "name");
	if (!name)
	{
		printf("Module %s doesn't contain \"name\" symbol\n", mod);
		return -1;
	}
	if (strcmp(*name, mod))
	{
		printf("Module name doesn't match filename (%s != %s), not loaded\n", mod, *name);
		return -1;
	}
	return 0;
}

static
int
mod_load_deps(struct module *mod)
{
	char **deps;
	
	deps = dlsym(mod->dlhand, "deps");
	if (!deps)
		return 0;

	for (; *deps; deps++)
	{
		if (mod_load(*deps, mod_find_config(*deps)))
			return -1;
	}
	
	return 0;
}

static
void
mod_close(struct module *mod)
{
	void *dlh;
	int i;
	
	dlh = mod->dlhand;
	
	free(mod->name);
	free(mod->alias);
	memset(mod, 0, sizeof(*mod));
	
	if (dlh)
	{
		for (i = 0; i < MAX_MODULES; i++)
		{
			if (modules[i].dlhand == dlh)
				goto inuse;	/* break */
		}
		dlclose(dlh);
inuse:
	}
}

static
int
mod_init_mod(struct module *mod)
{
	int (*init)(struct module_ctx *);
	int ret;
	
	init = dlsym(mod->dlhand, "init");
	if (!init)
		return 0;
	
	ret = init(&mod->ctx);
	return ret;
}

static
xmlNodePtr
mod_find_config(char *mod)
{
	xmlNodePtr node;
	char *modname, *alias;
	
	node = xml_root(configdoc);
	for (node = node->xml_children; node; node = node->next)
	{
		if (!xml_isnode(node, "module"))
			continue;
		modname = xml_attrval(node, "name");
		alias = xml_attrval(node, "alias");
		if (!modname || strcmp(modname, mod) || (alias && strcmp(alias, mod)))
			continue;
		return node;
	}
	return NULL;
}

