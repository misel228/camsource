#ifndef _JPEG_H_
#define _JPEG_H_

/* $Id: jpeg.h,v 1.7 2002/09/20 09:05:30 dfx Exp $ */

#include <libxml/parser.h>

struct image;

struct jpegbuf
{
	char *buf;
	unsigned int bufsize;
};

/* Compresses an image and returns a buffer to the jpeg data. Caller must
 * free(jpegbuf->buf). Third arg is a pointer to an xml tree, which may
 * contain a <jpegqual> tag to specify the quality to use. If no tag
 * is found or ptr is NULL, default quality will be used.
 */
void jpeg_compress(struct jpegbuf *, const struct image *, xmlNodePtr);

#endif

