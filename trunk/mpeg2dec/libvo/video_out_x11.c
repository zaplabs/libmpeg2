/*
 * video_out_x11.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 2003      Regis Duchesne <hpreg@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 * See http://libmpeg2.sourceforge.net/ for updates.
 *
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#ifdef LIBVO_X11

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <inttypes.h>
/* since it doesn't seem to be defined on some platforms */
int XShmGetEventBase (Display *);

#ifdef LIBVO_XV
#include <string.h>	/* strcmp */
#include <X11/extensions/Xvlib.h>
#define FOURCC_YV12 0x32315659
#define FOURCC_UYVY 0x59565955
#endif

#include "mpeg2.h"
#include "video_out.h"
#include "convert.h"

typedef struct {
    int width;
    int stride;
    int chroma420;
    uint8_t * out;
} convert_uyvy_t;

static void uyvy_start (void * _id, const mpeg2_fbuf_t * fbuf,
			const mpeg2_picture_t * picture,
			const mpeg2_gop_t * gop)
{
    convert_uyvy_t * instance = (convert_uyvy_t *) _id;

    instance->out = fbuf->buf[0];
    instance->stride = instance->width;
    if (picture->nb_fields == 1) {
	instance->stride <<= 1;
	if (! (picture->flags & PIC_FLAG_TOP_FIELD_FIRST))
	    instance->out += 2 * instance->width;
    }
}

#ifdef WORDS_BIGENDIAN
#define PACK(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#else
#define PACK(a,b,c,d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))
#endif

static void uyvy_line (uint8_t * py, uint8_t * pu, uint8_t * pv, void * _dst,
		       int width)
{
    uint32_t * dst = (uint32_t *) _dst;

    width >>= 4;
    do {
	dst[0] = PACK (pu[0],  py[0], pv[0],  py[1]);
	dst[1] = PACK (pu[1],  py[2], pv[1],  py[3]);
	dst[2] = PACK (pu[2],  py[4], pv[2],  py[5]);
	dst[3] = PACK (pu[3],  py[6], pv[3],  py[7]);
	dst[4] = PACK (pu[4],  py[8], pv[4],  py[9]);
	dst[5] = PACK (pu[5], py[10], pv[5], py[11]);
	dst[6] = PACK (pu[6], py[12], pv[6], py[13]);
	dst[7] = PACK (pu[7], py[14], pv[7], py[15]);
	py += 16;
	pu += 8;
	pv += 8;
	dst += 8;
    } while (--width);
}

static void uyvy_copy (void * id, uint8_t * const * src, unsigned int v_offset)
{
    const convert_uyvy_t * instance = (convert_uyvy_t *) id;
    uint8_t * py;
    uint8_t * pu;
    uint8_t * pv;
    uint8_t * dst;
    int height;

    dst = instance->out + 2 * instance->stride * v_offset;
    py = src[0]; pu = src[1]; pv = src[2];

    height = 16;
    do {
	uyvy_line (py, pu, pv, dst, instance->width);
	dst += 2 * instance->stride;
	py += instance->stride;
	if (! (--height & instance->chroma420)) {
	    pu += instance->stride >> 1;
	    pv += instance->stride >> 1;
	}
    } while (height);
}

void convert_uyvy (const mpeg2_sequence_t * seq, uint32_t accel, void * arg,
		   convert_init_t * result)
{
    convert_uyvy_t * instance = (convert_uyvy_t *) result->id;

    if (instance) {
	instance->width = seq->width;
	instance->chroma420 = (seq->chroma_height < seq->height);
	result->buf_size[0] = seq->width * seq->height * 2;
	result->buf_size[1] = result->buf_size[2] = 0;
	result->start = uyvy_start;
	result->copy = uyvy_copy;
    } else {
	result->id_size = sizeof (convert_uyvy_t);
    }
}

typedef struct {
    void * data;
    int wait_completion;
    XImage * ximage;
#ifdef LIBVO_XV
    XvImage * xvimage;
#endif
} x11_frame_t;

typedef struct x11_instance_s {
    vo_instance_t vo;
    x11_frame_t frame[3];
    int index;
    int width;
    int height;
    Display * display;
    Window window;
    GC gc;
    XVisualInfo vinfo;
    XShmSegmentInfo shminfo;
    int completion_type;
#ifdef LIBVO_XV
    XvPortID port;
    int xv;
#endif
    void (* teardown) (struct x11_instance_s * instance);
} x11_instance_t;

static int open_display (x11_instance_t * instance)
{
    int major;
    int minor;
    Bool pixmaps;
    XVisualInfo visualTemplate;
    XVisualInfo * XvisualInfoTable;
    XVisualInfo * XvisualInfo;
    int number;
    int i;
    XSetWindowAttributes attr;
    XGCValues gcValues;

    instance->display = XOpenDisplay (NULL);
    if (! (instance->display)) {
	fprintf (stderr, "Can not open display\n");
	return 1;
    }

    if ((XShmQueryVersion (instance->display, &major, &minor,
			   &pixmaps) == 0) ||
	(major < 1) || ((major == 1) && (minor < 1))) {
	fprintf (stderr, "No xshm extension\n");
	return 1;
    }

    instance->completion_type =
	XShmGetEventBase (instance->display) + ShmCompletion;

    /* list truecolor visuals for the default screen */
#ifdef __cplusplus
    visualTemplate.c_class = TrueColor;
#else
    visualTemplate.class = TrueColor;
#endif
    visualTemplate.screen = DefaultScreen (instance->display);
    XvisualInfoTable = XGetVisualInfo (instance->display,
				       VisualScreenMask | VisualClassMask,
				       &visualTemplate, &number);
    if (XvisualInfoTable == NULL) {
	fprintf (stderr, "No truecolor visual\n");
	return 1;
    }

    /* find the visual with the highest depth */
    XvisualInfo = XvisualInfoTable;
    for (i = 1; i < number; i++)
	if (XvisualInfoTable[i].depth > XvisualInfo->depth)
	    XvisualInfo = XvisualInfoTable + i;

    instance->vinfo = *XvisualInfo;
    XFree (XvisualInfoTable);

    attr.background_pixmap = None;
    attr.backing_store = NotUseful;
    attr.border_pixel = 0;
    attr.event_mask = 0;
    /* fucking sun blows me - you have to create a colormap there... */
    attr.colormap = XCreateColormap (instance->display,
				     RootWindow (instance->display,
						 instance->vinfo.screen),
				     instance->vinfo.visual, AllocNone);
    instance->window =
	XCreateWindow (instance->display,
		       DefaultRootWindow (instance->display),
		       0 /* x */, 0 /* y */, instance->width, instance->height,
		       0 /* border_width */, instance->vinfo.depth,
		       InputOutput, instance->vinfo.visual,
		       (CWBackPixmap | CWBackingStore | CWBorderPixel |
			CWEventMask | CWColormap), &attr);

    instance->gc = XCreateGC (instance->display, instance->window, 0,
			      &gcValues);

    return 0;
}

static int shmerror = 0;

static int handle_error (Display * display, XErrorEvent * error)
{
    shmerror = 1;
    return 0;
}

static void * create_shm (x11_instance_t * instance, int size)
{
    instance->shminfo.shmid = shmget (IPC_PRIVATE, size, IPC_CREAT | 0777);
    if (instance->shminfo.shmid == -1)
	goto error;

    instance->shminfo.shmaddr = (char *) shmat (instance->shminfo.shmid, 0, 0);
    if (instance->shminfo.shmaddr == (char *)-1)
	goto error;

    /* on linux the IPC_RMID only kicks off once everyone detaches the shm */
    /* doing this early avoids shm leaks when we are interrupted. */
    /* this would break the solaris port though :-/ */
    /* shmctl (instance->shminfo.shmid, IPC_RMID, 0); */

    /* XShmAttach fails on remote displays, so we have to catch this event */

    XSync (instance->display, False);
    XSetErrorHandler (handle_error);

    instance->shminfo.readOnly = True;
    if (! (XShmAttach (instance->display, &(instance->shminfo))))
	shmerror = 1;

    XSync (instance->display, False);
    XSetErrorHandler (NULL);
    if (shmerror) {
    error:
	fprintf (stderr, "cannot create shared memory\n");
	return NULL;
    }

    return instance->shminfo.shmaddr;
}

static void destroy_shm (x11_instance_t * instance)
{
    XShmDetach (instance->display, &(instance->shminfo));
    shmdt (instance->shminfo.shmaddr);
    shmctl (instance->shminfo.shmid, IPC_RMID, 0);
}

static void x11_event (x11_instance_t * instance)	/* XXXXXXXXXXX */
{
    XEvent event;
    char * addr;
    int i;

    XNextEvent (instance->display, &event);
    if (event.type == instance->completion_type) {
	addr = (instance->shminfo.shmaddr +
		((XShmCompletionEvent *)&event)->offset);
	for (i = 0; i < 3; i++)
	    if (addr == instance->frame[i].data)
		instance->frame[i].wait_completion = 0;
    }
}

static void x11_start_fbuf (vo_instance_t * _instance,
			    uint8_t * const * buf, void * id)
{
    x11_instance_t * instance = (x11_instance_t *) _instance;
    x11_frame_t * frame = (x11_frame_t *) id;

    while (frame->wait_completion)
	x11_event (instance);
}

static void x11_setup_fbuf (vo_instance_t * _instance,
			    uint8_t ** buf, void ** id)
{
    x11_instance_t * instance = (x11_instance_t *) _instance;

    buf[0] = (uint8_t *) instance->frame[instance->index].data;
    buf[1] = buf[2] = NULL;
    *id = instance->frame + instance->index++;
}

static void x11_draw_frame (vo_instance_t * _instance,
			    uint8_t * const * buf, void * id)
{
    x11_frame_t * frame;
    x11_instance_t * instance;

    frame = (x11_frame_t *) id;
    instance = (x11_instance_t *) _instance;

    XShmPutImage (instance->display, instance->window, instance->gc,
		  frame->ximage, 0, 0, 0, 0, instance->width, instance->height,
		  True);
    XFlush (instance->display);
    frame->wait_completion = 1;
}

static int x11_alloc_frames (x11_instance_t * instance)
{
    int size;
    char * alloc;
    int i;

    size = 0;
    alloc = NULL;
    for (i = 0; i < 3; i++) {
	instance->frame[i].wait_completion = 0;
	instance->frame[i].ximage =
	    XShmCreateImage (instance->display, instance->vinfo.visual,
			     instance->vinfo.depth, ZPixmap, NULL /* data */,
			     &(instance->shminfo),
			     instance->width, instance->height);
	if (instance->frame[i].ximage == NULL) {
	    fprintf (stderr, "Cannot create ximage\n");
	    return 1;
	} else if (i == 0) {
	    size = (instance->frame[0].ximage->bytes_per_line *
		    instance->frame[0].ximage->height);
	    alloc = (char *) create_shm (instance, 3 * size);
	    if (alloc == NULL)
		return 1;
	} else if (size != (instance->frame[i].ximage->bytes_per_line *
			    instance->frame[i].ximage->height)) {
	    fprintf (stderr, "unexpected ximage data size\n");
	    return 1;
	}

	instance->frame[i].data = instance->frame[i].ximage->data = alloc;
	alloc += size;
    }

    return 0;
}

static void x11_teardown (x11_instance_t * instance)
{
    int i;

    for (i = 0; i < 3; i++) {
	while (instance->frame[i].wait_completion)
	    x11_event (instance);
	XDestroyImage (instance->frame[i].ximage);
    }
    destroy_shm (instance);
}

static void x11_close (vo_instance_t * _instance)
{
    x11_instance_t * instance = (x11_instance_t *) _instance;

    instance->teardown (instance);
    XFreeGC (instance->display, instance->gc);
    XDestroyWindow (instance->display, instance->window);
    XCloseDisplay (instance->display);
}

#ifdef LIBVO_XV
static void xv_setup_fbuf (vo_instance_t * _instance,
			   uint8_t ** buf, void ** id)
{
    x11_instance_t * instance = (x11_instance_t *) _instance;
    uint8_t * data;

    data = (uint8_t *) instance->frame[instance->index].xvimage->data;
    buf[0] = data + instance->frame[instance->index].xvimage->offsets[0];
    buf[1] = data + instance->frame[instance->index].xvimage->offsets[2];
    buf[2] = data + instance->frame[instance->index].xvimage->offsets[1];
    *id = instance->frame + instance->index++;
}

static void xv_draw_frame (vo_instance_t * _instance,
			   uint8_t * const * buf, void * id)
{
    x11_frame_t * frame = (x11_frame_t *) id;
    x11_instance_t * instance = (x11_instance_t *) _instance;

    XvShmPutImage (instance->display, instance->port, instance->window,
		   instance->gc, frame->xvimage, 0, 0,
		   instance->width, instance->height, 0, 0,
		   instance->width, instance->height, True);
    XFlush (instance->display);
    frame->wait_completion = 1;
}

static int xv_check_fourcc (x11_instance_t * instance, XvPortID port,
			    uint32_t fourcc, const char * fourcc_str)
{
    XvImageFormatValues * formatValues;
    int formats;
    int i;

    formatValues = XvListImageFormats (instance->display, port, &formats);
    for (i = 0; i < formats; i++)
	if ((formatValues[i].id == fourcc) &&
	    (! (strcmp (formatValues[i].guid, fourcc_str)))) {
	    XFree (formatValues);
	    return 0;
	}
    XFree (formatValues);
    return 1;
}

static int xv_check_extension (x11_instance_t * instance,
			       uint32_t fourcc, const char * fourcc_str)
{
    unsigned int version;
    unsigned int release;
    unsigned int dummy;
    unsigned int adaptors;
    unsigned int i;
    unsigned long j;
    XvAdaptorInfo * adaptorInfo;

    if ((XvQueryExtension (instance->display, &version, &release,
			   &dummy, &dummy, &dummy) != Success) ||
	(version < 2) || ((version == 2) && (release < 2))) {
	fprintf (stderr, "No xv extension\n");
	return 1;
    }

    XvQueryAdaptors (instance->display, instance->window, &adaptors,
		     &adaptorInfo);

    for (i = 0; i < adaptors; i++)
	if (adaptorInfo[i].type & XvImageMask)
	    for (j = 0; j < adaptorInfo[i].num_ports; j++)
		if ((! (xv_check_fourcc (instance, adaptorInfo[i].base_id + j,
					 fourcc, fourcc_str))) &&
		    (XvGrabPort (instance->display, adaptorInfo[i].base_id + j,
				 0) == Success)) {
		    instance->port = adaptorInfo[i].base_id + j;
		    XvFreeAdaptorInfo (adaptorInfo);
		    return 0;
		}

    XvFreeAdaptorInfo (adaptorInfo);
    fprintf (stderr, "Cannot find xv %s port\n", fourcc_str);
    return 1;
}

static int xv_alloc_frames (x11_instance_t * instance, int size,
			    uint32_t fourcc)
{
    char * alloc;
    int i;

    alloc = (char *) create_shm (instance, 3 * size);
    if (alloc == NULL)
	return 1;

    for (i = 0; i < 3; i++) {
	instance->frame[i].wait_completion = 0;
	instance->frame[i].xvimage =
	    XvShmCreateImage (instance->display, instance->port, fourcc,
			      alloc, instance->width, instance->height,
			      &(instance->shminfo));
	if ((instance->frame[i].xvimage == NULL) ||
	    (instance->frame[i].xvimage->data_size != size)) {
	    fprintf (stderr, "Cannot create xvimage\n");
	    return 1;
	}
	instance->frame[i].data = alloc;
	alloc += size;
    }

    return 0;
}

static void xv_teardown (x11_instance_t * instance)
{
    int i;

    for (i = 0; i < 3; i++) {
	while (instance->frame[i].wait_completion)
	    x11_event (instance);
	XFree (instance->frame[i].xvimage);
    }
    destroy_shm (instance);
    XvUngrabPort (instance->display, instance->port, 0);
}
#endif

static int common_setup (vo_instance_t * _instance, unsigned int width,
			 unsigned int height, unsigned int chroma_width,
			 unsigned int chroma_height,
			 vo_setup_result_t * result)
{
    x11_instance_t * instance = (x11_instance_t *) _instance;

    if (instance->vo.close) {
        /* Already setup, just adjust to the new size */
        instance->teardown (instance);

        instance->width = width;
        instance->height = height;
        XResizeWindow (instance->display, instance->window, width, height);
    } else {
        /* Not setup yet, do the full monty */
        instance->vo.set_fbuf = NULL;
        instance->vo.discard = NULL;
        instance->vo.start_fbuf = x11_start_fbuf;
        instance->width = width;
        instance->height = height;

        if (open_display (instance))
            return 1;

        XMapWindow (instance->display, instance->window);
    }

    instance->index = 0;

#ifdef LIBVO_XV
    if (instance->xv == 1 &&
	(chroma_width == width >> 1) && (chroma_height == height >> 1) &&
	(!xv_check_extension (instance, FOURCC_YV12, "YV12"))) {
	if (xv_alloc_frames (instance, 3 * width * height / 2, FOURCC_YV12))
	    return 1;
	instance->vo.setup_fbuf = xv_setup_fbuf;
	instance->vo.draw = xv_draw_frame;
	instance->teardown = xv_teardown;
	result->convert = NULL;
    } else if (instance->xv && (chroma_width == width >> 1) &&
	       (!xv_check_extension (instance, FOURCC_UYVY, "UYVY"))) {
	if (xv_alloc_frames (instance, 2 * width * height, FOURCC_UYVY))
	    return 1;
	instance->vo.setup_fbuf = x11_setup_fbuf;
	instance->vo.draw = xv_draw_frame;
	instance->teardown = xv_teardown;
	result->convert = convert_uyvy;
    } else
#endif
    {
	if (x11_alloc_frames (instance))
	    return 1;
	instance->vo.setup_fbuf = x11_setup_fbuf;
	instance->vo.draw = x11_draw_frame;
	instance->teardown = x11_teardown;

#ifdef WORDS_BIGENDIAN
	if (instance->frame[0].ximage->byte_order != MSBFirst) {
	    fprintf (stderr, "No support for non-native byte order\n");
	    return 1;
	}
#else
	if (instance->frame[0].ximage->byte_order != LSBFirst) {
	    fprintf (stderr, "No support for non-native byte order\n");
	    return 1;
	}
#endif

	/*
	 * depth in X11 terminology land is the number of bits used to
	 * actually represent the colour.
	 *
	 * bpp in X11 land means how many bits in the frame buffer per
	 * pixel.
	 *
	 * ex. 15 bit color is 15 bit depth and 16 bpp. Also 24 bit
	 *     color is 24 bit depth, but can be 24 bpp or 32 bpp.
	 *
	 * If we have blue in the lowest bit then "obviously" RGB
	 * (the guy who wrote this convention never heard of endianness ?)
	 */

	result->convert =
	    convert_rgb (((instance->frame[0].ximage->blue_mask & 1) ?
			  CONVERT_RGB : CONVERT_BGR),
			 ((instance->vinfo.depth == 24) ?
			  instance->frame[0].ximage->bits_per_pixel :
			  instance->vinfo.depth));
    }

    instance->vo.close = x11_close;

    return 0;
}

static vo_instance_t * common_open (int xv)
{
    x11_instance_t * instance;

    instance = (x11_instance_t *) malloc (sizeof (x11_instance_t));
    if (instance == NULL)
	return NULL;

    instance->vo.setup = common_setup;
    instance->vo.close = NULL;
#ifdef LIBVO_XV
    instance->xv = xv;
#endif
    return (vo_instance_t *) instance;
}

vo_instance_t * vo_x11_open (void)
{
    return common_open (0);
}

#ifdef LIBVO_XV
vo_instance_t * vo_xv_open (void)
{
    return common_open (1);
}

vo_instance_t * vo_xv2_open (void)
{
    return common_open (2);
}
#endif
#endif
