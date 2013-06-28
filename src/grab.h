#ifndef _GRAB_H_
#define _GRAB_H_

#include <pthread.h>
#include <sys/time.h>

/* $Id: grab.h,v 1.14 2003/04/20 21:53:35 dfx Exp $ */

struct image;
struct palette;

struct grab_ctx
{
	unsigned int idx;
	struct timeval tv;
};

struct grab_camdev
{
	char *name;
	unsigned int x, y;
	struct palette *pal;
	void *custom;
};



int grab_threads_init(void);
int grab_open_all(void);
void grab_dump_all(void);
void grab_start_all(void);
long timeval_diff(struct timeval *, struct timeval *);

/* Does the necessary locking and checking to get a _new_ frame out of
 * current_img. A copy of the new frame is stored into the first argument
 * (which must be image_destroy()'d by the caller). The second argument
 * is a pointer to a local index var, holding the index of the last
 * grabbed image. Init this var to 0 to make sure a new frame is grabbed
 * at the beginning. If the index in current_img is larger than the value
 * of the pointer, the current frame is copied without signalling the
 * grabbing thread.
 * (The above description is a bit outdated now that we use a struct
 * grab_ctx instead of a simple int idx.)
 *
 * Example code:
 *
 * struct image img;
 * struct grabimage_ctx ctx;
 * memset(&ctx, 0, sizeof(ctx));
 * for (;;) {
 *   grab_get_image(&img, &ctx);
 *   do_something_with(&img);
 * }
 *
 * If you only want a single image (after which the thread exits), pass
 * NULL as the second argument.
 */
void grab_get_image(struct image *, struct grab_ctx *, const char *);

#endif

