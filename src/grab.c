#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#include <libxml/parser.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "config.h"

#include "grab.h"
#include "configfile.h"
#include "mod_handle.h"
#include "image.h"
#include "unpalette.h"
#include "filter.h"
#include "xmlhelp.h"

/* $Id: grab.c,v 1.33 2003/04/20 21:53:34 dfx Exp $ */

static void grab_glob_filters(struct image *);
static struct grabthread *find_thread(const char *);
static void *grab_thread(void *);
static void firecmd(struct grabthread *, int);

/* The grabimage struct and its only instance current_img.
 * (Private data now, not directly accessible by threads.)
 * This is where the grabbing thread stores the frames.
 * Every time a new frame is grabbed, the index is
 * incremented by one. The function grab_get_image is
 * provided to make it easy for a worker thread to read
 * images out of this.
 *
 * The grabber thread works like this:
 *  1) Acquire mutex.
 *  2) If request != 0, skip to step 5.
 *  3) Wait on request_cond, releasing mutex.
 *  4) Wake up from request_cond, reacquiring the mutex.
 *  5) Release mutex.
 *  6) Read frame from device into private local buffer,
 *     convert it to rgb palette, apply any global filters.
 *  7) Acquire mutex.
 *  8) Put new frame into current_img, increase index and
 *     set request = 0;
 *  9) Broadcast signal on ready_cond.
 * 10) Release mutex.
 * 11) Jump back to step 1.
 */
struct grabimage
{
	pthread_mutex_t mutex;

	struct image img;
	struct grab_ctx ctx;
	
	int request;
	pthread_cond_t request_cond;
	pthread_cond_t ready_cond;
};

struct grabthread {
	struct grabthread *next;
	char *name;
	struct grabimage curimg;
	struct grab_camdev gcamdev;
	xmlNodePtr node;
	int (*opendev)(xmlNodePtr, struct grab_camdev *);
	unsigned char *(*input)(struct grab_camdev *);
	void (*postprocess)(struct grab_camdev *, struct image *);
	void (*capdump)(xmlNodePtr, struct grab_camdev *);
	char *cmd;
	int cmdtimeout;
	int cmdfired;
};
static struct grabthread *grabthreadshead;

int
grab_threads_init()
{
	xmlNodePtr node;
	char *name, *active;
	struct grabthread *newthread;
	int ret = 0;
	struct module *mod;
	char *plugin, *cmd;
	void *opendev, *input;
	xmlNodePtr subnode;
	int cmdtimeout;
	
	node = xml_root(configdoc);
	for (node = node->xml_children; node; node = node->next) {
		if (!xml_isnode(node, "camdev"))
			continue;
			
		active = xml_attrval(node, "active");
		if (active && strcmp(active, "yes"))
			continue;
		
		name = xml_attrval(node, "name");
		if (!name)
			name = "default";
		if (find_thread(name)) {
			printf("Duplicate <camdev> name '%s', skipping\n", name);
			continue;
		}
		
		plugin = cmd = NULL;
		cmdtimeout = 0;
		for (subnode = node->xml_children; subnode; subnode = subnode->next) {
			if (xml_isnode(subnode, "plugin"))
				plugin = xml_getcontent(subnode);
			else if (xml_isnode(subnode, "cmd"))
				cmd = config_homedir(xml_getcontent(subnode));
			else if (xml_isnode(subnode, "cmdtimeout"))
				cmdtimeout = xml_atoi(subnode, 0) * 1000000;
		}
		
		if (!plugin || !(mod = mod_find(plugin, NULL))) {
			printf("Invalid or missing \"plugin\" attribute for '%s'\n", name);
			continue;
		}
		
		opendev = dlsym(mod->dlhand, "opendev");
		input = dlsym(mod->dlhand, "input");
		if (!opendev || !input) {
			printf("Module \"%s\" has no \"opendev\" or \"input\" routine\n", plugin);
			continue;
		}
		
		newthread = malloc(sizeof(*newthread));
		memset(newthread, 0, sizeof(*newthread));
		newthread->name = name;
		newthread->node = node;
		newthread->opendev = opendev;
		newthread->input = input;
		newthread->postprocess = dlsym(mod->dlhand, "postprocess");
		newthread->capdump = dlsym(mod->dlhand, "capdump");
		newthread->cmd = cmd;
		newthread->cmdtimeout = cmdtimeout;

		pthread_mutex_init(&newthread->curimg.mutex, NULL);
		pthread_cond_init(&newthread->curimg.request_cond, NULL);
		pthread_cond_init(&newthread->curimg.ready_cond, NULL);
		
		newthread->next = grabthreadshead;
		grabthreadshead = newthread;
		
		ret++;
	}
	
	return ret;
}

static
struct grabthread *
find_thread(const char *name)
{
	struct grabthread *thread;
	
	for (thread = grabthreadshead; thread; thread = thread->next) {
		if (!strcmp(name, thread->name))
			return thread;
	}
	
	return NULL;
}

int
grab_open_all()
{
	int ret;
	struct grabthread *thread;
	
	for (thread = grabthreadshead; thread; thread = thread->next) {
		ret = thread->opendev(thread->node, &thread->gcamdev);
		if (ret == -1) {
			printf("Failed to open device for <camdev name=\"%s\">\n", thread->name);
			return -1;
		}
	}
	
	return 0;
}

void
grab_dump_all()
{
	struct grabthread *thread;
	
	printf("\n");
	for (thread = grabthreadshead; thread; thread = thread->next) {
		if (thread->capdump)
			thread->capdump(thread->node, &thread->gcamdev);
		else
			printf("Device \"%s\" doesn't support capabilities dumping.\n", thread->name);
		printf("\n");
	}
}

void
grab_start_all(void)
{
	struct grabthread *thread;
	pthread_t grab_tid;
	pthread_attr_t attr;

	for (thread = grabthreadshead; thread; thread = thread->next) {
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&grab_tid, &attr, grab_thread, thread);
		pthread_attr_destroy(&attr);
	}
}

static
void
firecmd(struct grabthread *thread, int onoff)
{
	int ret;
	
	if (!thread->cmd || thread->cmdtimeout <= 0)
		return;
	if (thread->cmdfired && onoff)
		return;
	if (!thread->cmdfired && !onoff)
		return;

	ret = fork();
	if (!ret) {
		ret = fork();
		if (ret)
			_exit(0);
		close(STDIN_FILENO);
		for (ret = 3; ret < 1024; ret++)
			close(ret);
		execlp(thread->cmd, thread->cmd,
			onoff ? "start" : "end", thread->name, thread->gcamdev.name,
			NULL);
		printf("exec(\"%s\") failed: %s\n", thread->cmd, strerror(errno));
		_exit(0);
	}
	
	waitpid(ret, NULL, 0);
	thread->cmdfired = onoff;
}

static
void *
grab_thread(void *arg)
{
	int ret;
	struct grabthread *thread;
	unsigned char *rawimg;
	struct image newimg;
	struct timeval tvnow;
	struct timespec abstime;
	
	thread = arg;
	printf("Camsource " VERSION " ready to grab images for device '%s'...\n", thread->name);
	
	for (;;)
	{
		pthread_mutex_lock(&thread->curimg.mutex);
		while (!thread->curimg.request) {
			gettimeofday(&tvnow, NULL);
			abstime.tv_sec = tvnow.tv_sec + 1;
			abstime.tv_nsec = tvnow.tv_usec;
			
			ret = pthread_cond_timedwait(&thread->curimg.request_cond, &thread->curimg.mutex, &abstime);
			
			pthread_mutex_unlock(&thread->curimg.mutex);
			
			if (ret == ETIMEDOUT) {
				gettimeofday(&tvnow, NULL);
				if (timeval_diff(&tvnow, &thread->curimg.ctx.tv) > thread->cmdtimeout)
					firecmd(thread, 0);
			}
			
			pthread_mutex_lock(&thread->curimg.mutex);
		}
		pthread_mutex_unlock(&thread->curimg.mutex);
		
		firecmd(thread, 1);
		
		rawimg = thread->input(&thread->gcamdev);
		if (!rawimg) {
			printf("Device '%s' returned no image\n", thread->name);
			continue;
		}
		
		image_new(&newimg, thread->gcamdev.x, thread->gcamdev.y);
		thread->gcamdev.pal->routine(&newimg, rawimg);
		if (thread->postprocess)
			thread->postprocess(&thread->gcamdev, &newimg);
		
		grab_glob_filters(&newimg);
		filter_apply(&newimg, thread->node);

		pthread_mutex_lock(&thread->curimg.mutex);
		image_move(&thread->curimg.img, &newimg);
		
		thread->curimg.ctx.idx++;
		if (thread->curimg.ctx.idx == 0)
			thread->curimg.ctx.idx++;
		gettimeofday(&thread->curimg.ctx.tv, NULL);
		
		thread->curimg.request = 0;
		pthread_cond_broadcast(&thread->curimg.ready_cond);
		pthread_mutex_unlock(&thread->curimg.mutex);
	}

	return 0;
}

static
void
grab_glob_filters(struct image *img)
{
	xmlNodePtr node;

	node = xml_root(configdoc);
	filter_apply(img, node);
}

void
grab_get_image(struct image *img, struct grab_ctx *ctx, const char *name)
{
	struct timeval now;
	long diff;
	struct grabthread *thread;
	
	if (!img)
		return;
	
	thread = find_thread(name);
	if (!thread) {
		printf("Warning: trying to grab from non-existant device '%s'\n", name);
		/* dont crash */
		image_new(img, 1, 1);
		return;
	}
	
	pthread_mutex_lock(&thread->curimg.mutex);
	
	if (ctx)
	{
		if (!ctx->idx || ctx->idx == thread->curimg.ctx.idx)
		{
request:
			thread->curimg.request = 1;
			pthread_cond_signal(&thread->curimg.request_cond);
			pthread_cond_wait(&thread->curimg.ready_cond, &thread->curimg.mutex);
		}
		else
		{
			gettimeofday(&now, NULL);
			diff = timeval_diff(&now, &ctx->tv);
			if (diff < 0 || diff >= 500000)
				goto request;	/* considered harmful (!) */
		}

		memcpy(ctx, &thread->curimg.ctx, sizeof(*ctx));
	}
	
	image_copy(img, &thread->curimg.img);
	pthread_mutex_unlock(&thread->curimg.mutex);
}

long
timeval_diff(struct timeval *a, struct timeval *b)
{
	long ret;
	
	ret = a->tv_sec - b->tv_sec;
	ret *= 1000000;
	ret += a->tv_usec - b->tv_usec;
	
	return ret;
}

