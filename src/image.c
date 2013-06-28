#include <stdlib.h>

#include "config.h"

#include "image.h"

void
image_copy(struct image *dst, const struct image *src)
{
	image_dup(dst, src);
	if (src->buf)
		memcpy(dst->buf, src->buf, dst->bufsize);
	else
	{
		image_destroy(dst);
		dst->buf = NULL;
	}
}

void
image_new(struct image *img, unsigned int x, unsigned int y)
{
	img->x = x;
	img->y = y;
	img->bufsize = x * y * 3;
	img->buf = malloc(img->bufsize);
}

void
image_destroy(struct image *img)
{
	free(img->buf);
}

void
image_dup(struct image *dst, const struct image *src)
{
	memcpy(dst, src, sizeof(*dst));
	dst->buf = malloc(dst->bufsize);
}

void
image_move(struct image *dst, const struct image *src)
{
	image_destroy(dst);
	memcpy(dst, src, sizeof(*dst));
}

float
image_brightness(struct image *img)
{
	int i;
	float sum = 0;
	unsigned char *p = img->buf;
	for (i=0; i<img->x*img->y; i++) {
		sum += *p;
		p++;
	}
	sum /= (img->x * img->y * 3);
	return sum;
}
