bin_PROGRAMS = camsource
pkglib_LTLIBRARIES = libwc_serv.la libjpeg_comp.la libflip.la \
	libresize.la libsocket.la libhttp.la libfilewrite.la \
	libftpup.la libtext.la librgbbgr.la librotate.la \
	libbw.la libvloopback.la libmotiondetect.la libregrab.la \
	libinvert.la libinput_v4l.la libinput_xwd.la

pkginclude_HEADERS = configfile.h filter.h grab.h \
	image.h jpeg.h mod_handle.h module.h unpalette.h xmlhelp.h \
	socket.h log.h

camsource_SOURCES = main.c grab.c unpalette.c configfile.c \
	mod_handle.c image.c filter.c xmlhelp.c log.c main.h

libinput_v4l_la_SOURCES = input_v4l.c
libinput_xwd_la_SOURCES = input_xwd.c

libwc_serv_la_SOURCES = wc_serv.c wc_serv.h

libjpeg_comp_la_SOURCES = jpeg.c
libjpeg_comp_la_LDFLAGS = -ljpeg

libflip_la_SOURCES = flip.c
libresize_la_SOURCES = resize.c
libsocket_la_SOURCES = socket.c
libhttp_la_SOURCES = http.c http.h
libfilewrite_la_SOURCES = filewrite.c filewrite.h
libftpup_la_SOURCES = ftpup.c ftpup.h
libtext_la_SOURCES = text.c text.h font_6x11.h
librgbbgr_la_SOURCES = rgbbgr.c
librotate_la_SOURCES = rotate.c
libbw_la_SOURCES = bw.c
libvloopback_la_SOURCES = vloopback.c vloopback.h
libmotiondetect_la_SOURCES = motiondetect.c
libregrab_la_SOURCES = regrab.c
libinvert_la_SOURCES = invert.c

# $Id: Makefile.am,v 1.28 2003/02/16 00:21:58 dfx Exp $

