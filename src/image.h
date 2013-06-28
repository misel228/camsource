#ifndef _IMAGE_H_
#define _IMAGE_H_

/* $Id: image.h,v 1.3 2003/04/20 21:13:41 dfx Exp $ */

struct image
{
	unsigned int x, y;
	unsigned int bufsize;
	unsigned char *buf;
};

/* Inits image struct and allocates buf */
void image_new(struct image *, unsigned int x, unsigned int y);

/* Copies image with contents (= image_dup & memcpy(buf, buf)) */
void image_copy(struct image *dest, const struct image *src);

/* Only duplicates the structure and buffer, not the contents of the buffer */
void image_dup(struct image *dest, const struct image *src);

/* Moves the buffer and structure over to another var, and frees the old var and buf */
void image_move(struct image *dest, const struct image *src);

/* Frees buffer */
void image_destroy(struct image *);

/* Calculate image brightness co-efficient */
float image_brightness(struct image *);

#endif

