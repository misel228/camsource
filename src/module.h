/* Camsource module interface */
/* $Id: module.h,v 1.16 2003/04/20 21:53:35 dfx Exp $ */

#ifndef _MODULE_H_
#define _MODULE_H_

#include <pthread.h>
#include <libxml/parser.h>

/*
 * There are several kinds of modules:
 * .) MODULE_THREAD is a worker module which runs in its own thread.
 *    A thread module has a thread() function which will be run
 *    in its own thread. The thread() function will get a pointer
 *    to the module context structure (casted to void*), containing
 *    the xml config structure, the thread id and the custom pointer
 *    (see below in the part about init()). Note that you should not
 *    use any global vars in thread modules, as the same module may
 *    be activated multuple times, and hence thread() may run several
 *    times simultaneously.
 * .) MODULE_FILTER is an image filter module. It provides a filter()
 *    function that takes an image as input and outputs another one.
 *    Filtering happens in-place. A pointer to the xml config
 *    tree is passed to the filter function (so that node->name ==
 *    "filter"). The filter function returns 0 on success, a negative
 *    number on error (which will be more or less ignore), or a
 *    positive number to "swallow" the image, in which case the
 *    number is the number of microseconds to sleep before grabbing
 *    another image. As an additional argument, the function gets
 *    a pointer to a void pointer, which is a pointer to instance-
 *    specific data. As the same filter may be specified multiple
 *    times in the config, this pointer is the place to store context
 *    info across calls. The pointer will be initialized to NULL.
 * .) MODULE_GENERIC is a module which doesn't do anything by itself,
 *    but provides special functionality for other modules. It is
 *    usually listed as dependency in other modules.
 * .) MODULE_INPUT is an input plugin, which does the low-level work
 *    of grabbing images from a certain device. Each <camdev> section
 *    given in the config file is associated with a certain input
 *    plugin. When camsource starts up, one thread is created per
 *    active <camdev> section, which will call the input() routine
 *    of the associated input plugin to get frames. The init()
 *    function will still get a pointer to its <module> structure,
 *    while the opendev() function will get a pointer to the <camdev>
 *    config structure. Another optional function is the postprocess()
 *    function, which (if present) will be called for each grabbed
 *    frame, after it has been converted to the normal rgb format.
 *    The optional capdump() function should be included to support
 *    the -c command line switch.
 * You must define at least one of the above before including this
 * file (module.h). The brave can even define multiple of them,
 * for example a module that has both its own thread and provides
 * a filter.
 *
 * Things common to all kinds of modules:
 * .) The module "name". It must match the filename, e.g. a module
 *    called "libhello.so" would have a name of "hello".
 * .) A "version" string, containing the version number of the lib.
 *    Each initialized module will be printed in the log together
 *    with its version when it's loaded (if the version info is
 *    present that is; it's optional).
 * .) Dependency list ("deps"). It's a null terminated array of
 *    strings, each giving the name of another module which will
 *    be autoloaded before the current module is activated.
 * .) An optional init() function. If present, it will be called
 *    when the module is loaded. As argument, it gets a pointer
 *    to a module context structure. It contains a pointer to
 *    its xml config (which may be NULL, if the module was loaded
 *    as dependency and there's no mention of it in the config)
 *    and the thread id var (filled in later when the thread
 *    is started). The init() function can put a pointer
 *    to a custom (dynamically allocated) structure into the "user"
 *    struct member. Note that the thread-id and custom pointer
 *    only make sense for a thread module. Returning anything but 0
 *    from init() means module init has failed. You may also omit
 *    init() altogether. Note: do not create threads from init().
 */


struct module_ctx;

extern char *name;
extern char *version;
extern char *deps[];
int init(struct module_ctx *);


#ifdef MODULE_THREAD

void *thread(void *);

#endif	/* MODULE_THREAD */



#ifdef MODULE_FILTER

struct image;
int filter(struct image *, xmlNodePtr, void **);

#endif	/* MODULE_FILTER */



#ifdef MODULE_INPUT

struct grab_camdev;
struct image;
int opendev(xmlNodePtr, struct grab_camdev *);
unsigned char *input(struct grab_camdev *);
void postprocess(struct grab_camdev *, struct image *);
void capdump(xmlNodePtr, struct grab_camdev *);

#endif	/* MODULE_INPUT */





#if !defined(MODULE_THREAD) && !defined(MODULE_FILTER) \
	&& !defined(MODULE_GENERIC) && !defined(MODULE_INPUT) && !defined(MODULE_NONE)
# error "Must define the module type prior to including module.h"
#endif	/* !def && !def && !def */

#endif

