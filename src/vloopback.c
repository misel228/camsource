#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev.h>
#include <memory.h>

#include "config.h"
#include "vloopback.h"

#define MODULE_THREAD
#include "module.h"
#include "image.h"
#include "filter.h"
#include "xmlhelp.h"
#include "config.h"
#include "configfile.h"
#include "grab.h"
#include "log.h"




#define MODNAME "vloopback"

char *name = MODNAME;
char *version = VERSION;

/**
 * Init the video loopback kernel module.
 * Set the image dimension and the image format
 * The overlay window is used to init image dimension.
 * 
 */
int init_kernel_module (struct vloopback_ctx * mctx) {
	/*struct video_capability vid_caps;*/
	struct video_window vid_win;
	struct video_picture vid_pic;
	
	mctx->dev_out =open (mctx->dev_name, O_RDWR);				
	if (mctx->dev_out < 0) {
		perror ("Failed to open video device");
	}

	if (ioctl (mctx->dev_out, VIDIOCGPICT, &vid_pic)== -1) {
		perror ("ioctl VIDIOCGPICT");
		return (1);
	}
	vid_pic.palette=mctx->format;
	if (ioctl (mctx->dev_out, VIDIOCSPICT, &vid_pic)== -1) {
		perror ("ioctl VIDIOCSPICT");
		return (1);
	}
	if (ioctl (mctx->dev_out, VIDIOCGWIN, &vid_win)== -1) {
		perror ("ioctl VIDIOCGWIN");
		return (1);
	}

	vid_win.width=mctx->width;
	vid_win.height=mctx->height;
	if (ioctl (mctx->dev_out, VIDIOCSWIN, &vid_win)== -1) {
		perror ("ioctl VIDIOCSWIN");
		return (1);
	}

	return 0;
}



int
init(struct module_ctx *ctx)
{
	int ret;
	struct vloopback_ctx *mctx;
	xmlNodePtr node;

	mctx = malloc (sizeof(*mctx));
	
	mctx->width = 320;
	mctx->height = 240;
	mctx->format = VIDEO_PALETTE_RGB24;
	mctx->dev_name = "/dev/video1";

	node =  ctx->node;
	for (node = node->xml_children; node; node = node->next)
		{
			if (xml_isnode(node, "width"))
				mctx->width = xml_atoi(node, 320);
			else if (xml_isnode(node, "height"))
				mctx->height = xml_atoi(node, 240);
			else if (xml_isnode(node, "format"))
				mctx->format = xml_atoi(node, VIDEO_PALETTE_RGB24);
			else if (xml_isnode(node, "device"))
				mctx->dev_name = xml_getcontent_def(node, "/dev/video1");

		}
	log_log(MODNAME, "loopback %s %dx%d format=%d (RGB24=%d)\n",
									mctx->dev_name,
									mctx->width, 
									mctx->height, 
									mctx->format, VIDEO_PALETTE_RGB24);
	ctx->custom = mctx;
	ret = init_kernel_module (mctx);

	return ret;
}

/**
 *
 * Get the current image, apply filters and write to loopback module.
 * TODO : close the device
 * */
void *
thread(void *arg)
{
	struct image img;
	struct grab_ctx idx;
	struct vloopback_ctx *mctx;
	struct module_ctx *ctx;
	
	ctx = (struct module_ctx *)arg;
	mctx = ctx->custom;
	
	
	memset(&idx, 0, sizeof(idx));
	while (1)
	{
		filter_get_image(&img, &idx, ctx->node, NULL);
		write (mctx->dev_out, img.buf, img.bufsize);
		image_destroy (&img);
	}
	
	return NULL;
}
