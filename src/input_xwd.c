#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libxml/parser.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "config.h"

#define MODULE_INPUT
#include "module.h"
#include "xmlhelp.h"
#include "grab.h"
#include "unpalette.h"
#include "log.h"

#define MODNAME "input_xwd"

/* $Id: input_xwd.c,v 1.3 2003/02/22 10:41:47 dfx Exp $ */


char *name = MODNAME;
char *version = VERSION;
char *deps[] = { NULL };




typedef int32_t CARD32;
#define B32

struct xwdheader {
	/* header_size = SIZEOF(XWDheader) + length of null-terminated
	 * window name. */
	CARD32 header_size B32;		

	CARD32 file_version B32;	/* = XWD_FILE_VERSION above */
	CARD32 pixmap_format B32;	/* ZPixmap or XYPixmap */
	CARD32 pixmap_depth B32;	/* Pixmap depth */
	CARD32 pixmap_width B32;	/* Pixmap width */
	CARD32 pixmap_height B32;	/* Pixmap height */
	CARD32 xoffset B32;		/* Bitmap x offset, normally 0 */
	CARD32 byte_order B32;		/* of image data: MSBFirst, LSBFirst */

	/* bitmap_unit applies to bitmaps (depth 1 format XY) only.
	 * It is the number of bits that each scanline is padded to. */
	CARD32 bitmap_unit B32;		

	CARD32 bitmap_bit_order B32;	/* bitmaps only: MSBFirst, LSBFirst */

	/* bitmap_pad applies to pixmaps (non-bitmaps) only.
	 * It is the number of bits that each scanline is padded to. */
	CARD32 bitmap_pad B32;		

	CARD32 bits_per_pixel B32;	/* Bits per pixel */

	/* bytes_per_line is pixmap_width padded to bitmap_unit (bitmaps)
	 * or bitmap_pad (pixmaps).  It is the delta (in bytes) to get
	 * to the same x position on an adjacent row. */
	CARD32 bytes_per_line B32;
	CARD32 visual_class B32;	/* Class of colormap */
	CARD32 red_mask B32;		/* Z red mask */
	CARD32 green_mask B32;		/* Z green mask */
	CARD32 blue_mask B32;		/* Z blue mask */
	CARD32 bits_per_rgb B32;	/* Log2 of distinct color values */
	CARD32 colormap_entries B32;	/* Number of entries in colormap; not used? */
	CARD32 ncolors B32;		/* Number of XWDColor structures */
	CARD32 window_width B32;	/* Window width */
	CARD32 window_height B32;	/* Window height */
	CARD32 window_x B32;		/* Window upper left X coordinate */
	CARD32 window_y B32;		/* Window upper left Y coordinate */
	CARD32 window_bdrwidth B32;	/* Window border width */
};



struct xwd_ctx {
	unsigned char *buf;
	unsigned int bufsize;
};



static int xwd_grab(struct grab_camdev *);



int
opendev(xmlNodePtr node, struct grab_camdev *gcamdev)
{
	for (node = node->xml_children; node; node = node->next)
	{
		if (xml_isnode(node, "command"))
			gcamdev->name = xml_getcontent(node);
	}
	
	if (!gcamdev->name) {
		printf("No <command> in xwd config specified\n");
		return -1;
	}
	
	gcamdev->custom = malloc(sizeof(struct xwd_ctx));
	memset(gcamdev->custom, 0, sizeof(struct xwd_ctx));
	
	return 0;
}

unsigned char *
input(struct grab_camdev *gcamdev)
{
	xwd_grab(gcamdev);
	
	return ((struct xwd_ctx *) gcamdev->custom)->buf;
}

static
int
xwd_grab(struct grab_camdev *gcamdev)
{
	FILE *pp;
	int ret;
	unsigned char buf[1024];
	struct xwdheader xwdh;
	int namelen;
	unsigned char *abuf;
	int readlen, outputlen, linelen;
	unsigned char *readp, *writep, *endp;

	pp = popen(gcamdev->name, "r");
	if (!pp) {
		log_log(MODNAME, "failed to run cmd '%s' from pipe: %s\n", gcamdev->name, strerror(errno));
		return -1;
	}
	
	ret = fread(&xwdh, sizeof(xwdh), 1, pp);
	
	if (ret != 1) {
readerr:
		if (ret < 0)
			log_log(MODNAME, "error while reading from %s pipe: %s\n", gcamdev->name, strerror(errno));
		else if (ret == 0)
			log_log(MODNAME, "eof while reading from %s pipe\n", gcamdev->name);
		else
			log_log(MODNAME, "short read from %s pipe: %i items\n", gcamdev->name, ret);
		
killcloseout:
		/* should forcefully kill the child here */
		pclose(pp);
		return -1;
	}
	
	xwdh.file_version = ntohl(xwdh.file_version);
	if (xwdh.file_version != 7) {
		log_log(MODNAME, "%s output doesn't seem to be in proper xwd format\n", gcamdev->name);
		goto killcloseout;
	}
	
	xwdh.header_size = ntohl(xwdh.header_size);
	namelen = xwdh.header_size - sizeof(xwdh); 
	if (namelen <= 0 || namelen >= sizeof(buf)) {
		log_log(MODNAME, "%s output has an xwd headersize which doesnt make sense (%i)\n", gcamdev->name, xwdh.header_size);
		goto killcloseout;
	}
	
	xwdh.pixmap_format = ntohl(xwdh.pixmap_format);
	if (xwdh.pixmap_format != 1 && xwdh.pixmap_format != 2) {
		log_log(MODNAME, "unsupported xwd format from %s\n", gcamdev->name);
		goto killcloseout;
	}
	
	xwdh.pixmap_depth = ntohl(xwdh.pixmap_depth);
	xwdh.bits_per_pixel = ntohl(xwdh.bits_per_pixel);
	if (xwdh.pixmap_depth != 24
		|| (xwdh.bits_per_pixel != 32 && xwdh.bits_per_pixel != 24))
	{
		log_log(MODNAME, "unsupported xwd format from %s\n", gcamdev->name);
		goto killcloseout;
	}
	
	if ((ret = fread(buf, namelen, 1, pp)) != 1)
		goto readerr;
	
	xwdh.ncolors = ntohl(xwdh.ncolors);
	readlen = xwdh.ncolors * 12;
	abuf = malloc(readlen);
	if ((ret = fread(abuf, readlen, 1, pp)) != 1) {
freereaderr:
		free(abuf);
		goto readerr;
	}
	free(abuf);
	
	xwdh.pixmap_width = ntohl(xwdh.pixmap_width);
	xwdh.pixmap_height = ntohl(xwdh.pixmap_height);
	xwdh.bytes_per_line = ntohl(xwdh.bytes_per_line);
	readlen = xwdh.bytes_per_line * (xwdh.pixmap_height - 1) + xwdh.pixmap_width * (xwdh.bits_per_pixel / 8);
	
	gcamdev->x = xwdh.pixmap_width;
	gcamdev->y = xwdh.pixmap_height;
	if (xwdh.bits_per_pixel == 24) {
		gcamdev->pal = palettes;
		linelen = xwdh.pixmap_width * 3;
	}
	else {
		gcamdev->pal = palettes + 2;
		linelen = xwdh.pixmap_width * 4;
	}
	outputlen = linelen * xwdh.pixmap_height;
	
	if (((struct xwd_ctx *) gcamdev->custom)->bufsize != outputlen) {
		free(((struct xwd_ctx *) gcamdev->custom)->buf);
		((struct xwd_ctx *) gcamdev->custom)->buf = malloc(outputlen);
		((struct xwd_ctx *) gcamdev->custom)->bufsize = outputlen;
	}
	
	abuf = malloc(readlen);
	
	if ((ret = fread(abuf, readlen, 1, pp)) != 1)
		goto freereaderr;
	
	pclose(pp);

	readp = abuf;
	writep = ((struct xwd_ctx *) gcamdev->custom)->buf;
	endp = writep + outputlen;
	while (writep < endp) {
		memcpy(writep, readp, linelen);
		writep += linelen;
		readp += xwdh.bytes_per_line;
	}
	
	free(abuf);
	
	return 0;
}

