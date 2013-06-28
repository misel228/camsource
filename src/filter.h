#ifndef _FILTER_H_
#define _FILTER_H_

#include <libxml/parser.h>

struct image;
struct grab_ctx;

/* Modifies an image in-place by applying all filters found under the xml tree.
 * May print error messages. Returns 0 if everything ok, or non-zero if the
 * image was "swallowed" by a filter. */
int filter_apply(struct image *, xmlNodePtr);

/* Combines grab_get_image with filter_apply_multiple. Also re-grabs image if image
 * got swallowed. Takes a sequence of xmlNodePtrs as args, terminated by NULL. */
void filter_get_image(struct image *, struct grab_ctx *, ...);

#endif

