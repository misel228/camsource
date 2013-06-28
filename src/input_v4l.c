#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libxml/parser.h>
#include <linux/videodev.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "config.h"

#define MODULE_INPUT
#include "module.h"
#include "xmlhelp.h"
#include "grab.h"
#include "unpalette.h"
#include "image.h"

#define MODNAME "input_v4l"

/* $Id: input_v4l.c,v 1.3 2003/04/20 21:53:35 dfx Exp $ */


char *name = MODNAME;
char *version = VERSION;
char *deps[] = { NULL };



static int camdev_size_def(xmlNodePtr);
static int camdev_size_set(int, int, int, char *);



struct v4l_camdev
{
	int fd;
	struct video_capability vidcap;
	struct video_picture vidpic;
	int usemmap;
	unsigned char *mbufp;
	int mbufframe;
	unsigned char *imgbuf;
	unsigned int bpf;
	struct video_mbuf mbuf;
	int autobrightness;
};




int
opendev(xmlNodePtr node, struct grab_camdev *gcamdev)
{
	struct v4l_camdev newcamdev;
	int ret;
	struct video_window vidwin;
	char *path;
	int x, y, fps;
	int channel;
	int norm;
	struct video_channel vidchan;
	int brightness, hue, colour, contrast, whiteness, autobrightness;
	struct palette *pal;
	
	memset(&newcamdev, 0, sizeof(newcamdev));
	
	path = "/dev/video0";
	x = y = fps = 0;
	brightness = hue = colour = contrast = whiteness = channel = norm = autobrightness = -1;
	
	if (node)
	{
		for (node = node->xml_children; node; node = node->next)
		{
			if (xml_isnode(node, "path"))
				path = xml_getcontent_def(node, path);
			else if (xml_isnode(node, "width"))
				x = camdev_size_def(node);
			else if (xml_isnode(node, "height"))
				y = camdev_size_def(node);
			else if (xml_isnode(node, "fps"))
				fps = xml_atoi(node, 0);
			else if (xml_isnode(node, "channel"))
				channel = xml_atoi(node, -1);
			else if (xml_isnode(node, "brightness"))
				brightness = xml_atoi(node, -1);
			else if (xml_isnode(node, "colour"))
				colour = xml_atoi(node, -1);
			else if (xml_isnode(node, "hue"))
				hue = xml_atoi(node, -1);
			else if (xml_isnode(node, "contrast"))
				contrast = xml_atoi(node, -1);
			else if (xml_isnode(node, "whiteness"))
				whiteness = xml_atoi(node, -1);
			else if (xml_isnode(node, "autobrightness"))
				autobrightness = xml_atoi(node, 0);
			else if (xml_isnode(node, "norm"))
			{
				char *s = xml_getcontent_def(node, "");
				if (!strcasecmp(s, "pal"))
					norm = VIDEO_MODE_PAL;
				else if (!strcasecmp(s, "ntsc"))
					norm = VIDEO_MODE_NTSC;
				else if (!strcasecmp(s, "secam"))
					norm = VIDEO_MODE_SECAM;
				else if (!strcasecmp(s, "auto"))
					norm = VIDEO_MODE_AUTO;
				else
					printf("Invalid video norm \"%s\" specified, ignoring\n", s);
			}
		}
	}
	
	newcamdev.fd = open(path, O_RDONLY);
	if (newcamdev.fd < 0)
	{
		printf("Unable to open %s (%s)\n", path, strerror(errno));
		return -1;
	}
	
	ret = ioctl(newcamdev.fd, VIDIOCGCAP, &newcamdev.vidcap);
	if (ret != 0)
	{
		printf("ioctl \"get video cap\" failed: %s\n", strerror(errno));
closenerr:
		close(newcamdev.fd);
		return -1;
	}
	if (!(newcamdev.vidcap.type & VID_TYPE_CAPTURE))
	{
		printf("Video device doesn't support grabbing to memory\n");
		goto closenerr;
	}
	
	if (channel >= 0)
	{
		memset(&vidchan, 0, sizeof(vidchan));
		vidchan.channel = channel;
		if (norm >= 0)
			vidchan.norm = norm;
		ret = ioctl(newcamdev.fd, VIDIOCSCHAN, &vidchan);
		if (ret)
			printf("ioctl \"set input channel\" failed, continuing anyway: %s\n", strerror(errno));
	}
	
	memset(&vidwin, 0, sizeof(vidwin));
	
	newcamdev.autobrightness = autobrightness;

	x = camdev_size_set(x, newcamdev.vidcap.minwidth, newcamdev.vidcap.maxwidth, "width");
	y = camdev_size_set(y, newcamdev.vidcap.minheight, newcamdev.vidcap.maxheight, "height");
	if (x <= 0 || y <= 0)
		goto closenerr;
	vidwin.width = x;
	vidwin.height = y;

	vidwin.flags |= (fps & 0x3f) << 16;

	ret = ioctl(newcamdev.fd, VIDIOCSWIN, &vidwin);
	if (ret != 0)
	{
		printf("ioctl \"set grab window\" failed: %s\n", strerror(errno));
		printf("Trying again without the fps option...\n");
		
		vidwin.flags &= ~(0x3f << 16);
		ret = ioctl(newcamdev.fd, VIDIOCSWIN, &vidwin);
		
		if (!ret) {
			printf("ioctl \"set grab window\" succeeded without fps option.\n");
			printf("This probably means that your driver doesn't support setting");
			printf("the fps, and you should remove the <fps> tag from your");
			printf("config file.\n");
		}
		else {
			printf("ioctl \"set grab window\" failed again: %s\n", strerror(errno));
			printf("Check your <camdev> config section for width/height and fps settings.\n");
			printf("In case your driver simply doesn't support overlaying (e.g. meye),\n");
			printf("we will continue anyway and hope that everything goes ok.\n");
		}
	}
	else {
		ret = ioctl(newcamdev.fd, VIDIOCGWIN, &vidwin);
		if (!ret)
			ioctl(newcamdev.fd, VIDIOCSWIN, &vidwin);
	}
	
	ret = ioctl(newcamdev.fd, VIDIOCGPICT, &newcamdev.vidpic);
	if (ret != 0)
	{
		printf("ioctl \"get pict props\" failed: %s\n", strerror(errno));
		goto closenerr;
	}
	for (pal = palettes; pal->val >= 0; pal++)
	{
		newcamdev.vidpic.palette = pal->val;
		newcamdev.vidpic.depth = pal->depth;
		if (brightness >= 0)
			newcamdev.vidpic.brightness = brightness;
		if (hue >= 0)
			newcamdev.vidpic.hue = hue;
		if (colour >= 0)
			newcamdev.vidpic.colour = colour;
		if (contrast >= 0)
			newcamdev.vidpic.contrast = contrast;
		if (whiteness >= 0)
			newcamdev.vidpic.whiteness = whiteness;
		ioctl(newcamdev.fd, VIDIOCSPICT, &newcamdev.vidpic);
		ioctl(newcamdev.fd, VIDIOCGPICT, &newcamdev.vidpic);
		if (newcamdev.vidpic.palette == pal->val)
			goto palfound;	/* break */
	}
	printf("No common supported palette found\n");
	goto closenerr;
	
palfound:
	newcamdev.usemmap = -1;
	newcamdev.mbufp = NULL;
	newcamdev.mbufframe = 0;
	newcamdev.bpf = (vidwin.width * vidwin.height) * pal->bpp;
	newcamdev.imgbuf = malloc(newcamdev.bpf);

	gcamdev->name = strdup(path);
	gcamdev->pal = pal;
	gcamdev->x = vidwin.width;
	gcamdev->y = vidwin.height;

	gcamdev->custom = malloc(sizeof(newcamdev));
	memcpy(gcamdev->custom, &newcamdev, sizeof(newcamdev));
	
	return 0;
}

unsigned char *
input(struct grab_camdev *gcamdev)
{
	int ret;
	struct v4l_camdev *camdev;
	struct video_mmap mmapctx;
	unsigned char *retbuf;

	camdev = gcamdev->custom;
	
	if (camdev->usemmap)
	{
		if (camdev->usemmap < 0)
		{
			do
				ret = ioctl(camdev->fd, VIDIOCGMBUF, &camdev->mbuf);
			while (ret < 0 && errno == EINTR);
			if (ret < 0)
			{
nommap:
				printf("Not using mmap interface, falling back to read() (%s)\n", gcamdev->name);
				camdev->usemmap = 0;
				goto sysread;
			}
			camdev->mbufp = mmap(NULL, camdev->mbuf.size, PROT_READ, MAP_PRIVATE, camdev->fd, 0);
			if (camdev->mbufp == MAP_FAILED)
				goto nommap;
			camdev->usemmap = 1;
			camdev->mbufframe = 0;
		}
		memset(&mmapctx, 0, sizeof(mmapctx));
		mmapctx.frame = camdev->mbufframe;
		mmapctx.width = gcamdev->x;
		mmapctx.height = gcamdev->y;
		mmapctx.format = gcamdev->pal->val;
		do
			ret = ioctl(camdev->fd, VIDIOCMCAPTURE, &mmapctx);
		while (ret < 0 && errno == EINTR);
		if (ret < 0)
		{
unmap:
			munmap(camdev->mbufp, camdev->mbuf.size);
			goto nommap;
		}
		ret = camdev->mbufframe;
		do
			ret = ioctl(camdev->fd, VIDIOCSYNC, &ret);
		while (ret < 0 && errno == EINTR);
		if (ret < 0)
			goto unmap;
		
		retbuf = camdev->mbufp + camdev->mbuf.offsets[camdev->mbufframe];
			
		camdev->mbufframe++;
		if (camdev->mbufframe >= camdev->mbuf.frames)
			camdev->mbufframe = 0;
		
		return retbuf;
	}

sysread:
	do
		ret = read(camdev->fd, camdev->imgbuf, camdev->bpf);
	while (ret < 0 && errno == EINTR);
	if (ret < 0)
	{
		printf("Error while reading from device '%s': %s\n", gcamdev->name, strerror(errno));
		return NULL;
	}
	if (ret == 0)
	{
		printf("EOF while reading from device '%s'\n", gcamdev->name);
		return NULL;
	}
	if (ret < camdev->bpf)
		printf("Short read while reading from device '%s' (%i < %i), continuing anyway\n",
		gcamdev->name, ret, camdev->bpf);
	
	return camdev->imgbuf;
}

static
int
camdev_size_def(xmlNodePtr node)
{
	char *s;
	
	s = xml_getcontent_def(node, "max");
	if (!strcmp(s, "max") || !strcmp(s, "maximum") || !strcmp(s, "default"))
		return 0;
	else if (!strcmp(s, "min") || !strcmp(s, "minimum"))
		return -1;
	else
		return xml_atoi(node, 0);
}

static
int
camdev_size_set(int val, int min, int max, char *s)
{
	if (val == 0)
		return max;
	if (val == -1)
		return min;
	if (val < min)
	{
		printf("Invalid grabbing %s according to driver (%i < %i)\n", s, val, min);
		return 0;
	}
	if (val > max)
	{
		printf("Invalid grabbing %s according to driver (%i > %i)\n", s, val, max);
		return 0;
	}
	return val;
}

void
postprocess(struct grab_camdev *gcamdev, struct image *img)
{
	float brightness, change;
	struct v4l_camdev *camdev;

	camdev = gcamdev->custom;

	if (camdev->autobrightness <= 0)
		return;

	brightness = image_brightness(img);
	/*printf("Current Image Brightness = %2.1f\n", brightness);*/

	if (!(brightness < camdev->autobrightness -1 || brightness > camdev->autobrightness +1))
		return;

	if (ioctl(camdev->fd, VIDIOCGPICT, &camdev->vidpic) == -1) {
		perror ("ioctl (VIDIOCGPICT)");
		return;
	}

	change = camdev->autobrightness - brightness;
	if (camdev->vidpic.brightness < 50) camdev->vidpic.brightness = 50;
	change = camdev->vidpic.brightness * (change/100) * 3;
	/*printf("Calculated change = %i\n", (int)change);*/
	if (camdev->vidpic.brightness + change < 50) {
		/*printf("Setting to MIN (50)\n");*/
		camdev->vidpic.brightness = 50;
	} 
	else if (camdev->vidpic.brightness + change > 65535) {
		/*printf("Setting to MAX (65535)\n");*/
		camdev->vidpic.brightness = 65535;
	} 
	else {
		camdev->vidpic.brightness += (int)change; 
		/*printf("Setting camera brightness to %i\n", camdev->vidpic.brightness);*/
	}
	if (ioctl(camdev->fd, VIDIOCSPICT, &camdev->vidpic) == -1) {
		perror ("ioctl (VIDIOCSPICT)");
	}
}

void
capdump(xmlNodePtr node, struct grab_camdev *gcamdev)
{
	char *path;
	int fd, ret;
	struct video_capability vidcap;
	struct video_picture vidpic;
	struct palette *pal;

	path = "/dev/video0";
	if (node)
	{
		for (node = node->xml_children; node; node = node->next)
		{
			if (xml_isnode(node, "path"))
				path = xml_getcontent_def(node, path);
		}
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
	{
		printf("Unable to open %s (%s)\n", path, strerror(errno));
		return;
	}

	ret = ioctl(fd, VIDIOCGCAP, &vidcap);
	if (ret < 0)
	{
		printf("ioctl(VIDIOCGCAP) (get video capabilites) failed: %s\n", strerror(errno));
		goto closenout;
	}
	
	printf("Capability info for %s:\n", path);
	printf("  Name: %s\n", vidcap.name);
	printf("    Can %scapture to memory\n", (vidcap.type & VID_TYPE_CAPTURE) ? "" : "not ");
	printf("    %s a tuner\n", (vidcap.type & VID_TYPE_TUNER) ? "Has" : "Doesn't have");
	printf("    Can%s receive teletext\n", (vidcap.type & VID_TYPE_TELETEXT) ? "" : "not");
	printf("    Overlay is %schromakeyed\n", (vidcap.type & VID_TYPE_CHROMAKEY) ? "" : "not ");
	printf("    Overlay clipping is %ssupported\n", (vidcap.type & VID_TYPE_CLIPPING) ? "" : "not ");
	printf("    Overlay %s frame buffer mem\n", (vidcap.type & VID_TYPE_FRAMERAM) ? "overwrites" : "doesn't overwrite");
	printf("    Hardware image scaling %ssupported\n", (vidcap.type & VID_TYPE_SCALES) ? "" : "not ");
	printf("    Captures in %s\n", (vidcap.type & VID_TYPE_MONOCHROME) ? "grayscale only" : "color");
	printf("    Can capture %s image\n", (vidcap.type & VID_TYPE_SUBCAPTURE) ? "only part of the" : "the complete");
	printf("  Number of channels: %i\n", vidcap.channels);
	printf("  Number of audio devices: %i\n", vidcap.audios);
	printf("  Grabbing frame size:\n");
	printf("    Min: %ix%i\n", vidcap.minwidth, vidcap.minheight);
	printf("    Max: %ix%i\n", vidcap.maxwidth, vidcap.maxheight);
	
	ret = ioctl(fd, VIDIOCGPICT, &vidpic);
	if (ret != 0)
	{
		printf("ioctl(VIDIOCGPICT) (get picture properties) failed: %s\n", strerror(errno));
		goto closenout;
	}
	
	printf("\n");
	printf("Palette information:\n");
	for (pal = palettes; pal->val >= 0; pal++)
	{
		if (pal->val == vidpic.palette)
		{
			printf("  Currenctly active palette: %s with depth %u\n", pal->name, vidpic.depth);
			goto palfound;
		}
	}
	printf("  Currenctly active palette: not found/supported? (value %u, depth %u)\n", vidpic.palette, vidpic.depth);
	
palfound:
	printf("  Probing for supported palettes:\n");
	for (pal = palettes; pal->val >= 0; pal++)
	{
		vidpic.palette = pal->val;
		vidpic.depth = pal->depth;
		ioctl(fd, VIDIOCSPICT, &vidpic);
		ioctl(fd, VIDIOCGPICT, &vidpic);
		if (vidpic.palette == pal->val)
			printf("    Palette \"%s\" supported: Yes, with depth %u\n", pal->name, vidpic.depth);
		else
			printf("    Palette \"%s\" supported: No\n", pal->name);
	}	

closenout:
	close(fd);
}
