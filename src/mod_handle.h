#ifndef _MOD_HANDLE_H_
#define _MOD_HANDLE_H_

#include <pthread.h>
#include <libxml/parser.h>

#define MAX_MODULES 32

/* Module context.
 * node:   Pointer to the config section for the module
 *         in the xml tree. May be NULL if module was
 *         loaded as dependency and no config section
 *         for it could be found.
 * tid:    Thread-id, filled in by camsource when thread
 *         is started. Meaningless for non-thread mods.
 * custom: Custom module data, usually set by module's
 *         init() to hold context/config data.
 */
struct module_ctx
{
	xmlNodePtr node;
	pthread_t tid;
	void *custom;
};

struct module
{
	void *dlhand;	/* as returned by dlopen(). The same handle may appear several times in the list */
	char *name;	/* if == NULL, slot is unused */
	char *alias;
	struct module_ctx ctx;	/* Passed to init() and thread() */
};

/* None of these functions may be used from threads. All module loading/initing
 * must happen before threads are started */
void mod_init(void);
/* Loads all "active" modules as given in configdoc */
void mod_load_all(void);

void mod_start_all(void);

struct module *mod_find(char *, char *);

#endif

