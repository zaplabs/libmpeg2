/*
 * mpeg2dec.c
 * Copyright (C) 2000-2001 Michel Lespinasse <walken@zoy.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#include <signal.h>
#endif

#include "video_out.h"
#include "mpeg2.h"
#include "mm_accel.h"

#define BUFFER_SIZE 4096
static uint8_t buffer[BUFFER_SIZE];
static FILE * in_file;
static int demux_track = 0;
static int disable_accel = 0;
static mpeg2dec_t mpeg2dec;
static vo_open_t * output_open = NULL;

#ifdef HAVE_SYS_TIME_H

#include <sys/time.h>
#include <signal.h>

static void print_fps (int final);

static RETSIGTYPE signal_handler (int sig)
{
    print_fps (1);
    signal (sig, SIG_DFL);
    raise (sig);
}

static void print_fps (int final) 
{
    static uint32_t frame_counter = 0;
    static struct timeval tv_beg, tv_start;
    static int total_elapsed;
    static int last_count = 0;
    struct timeval tv_end;
    int fps, tfps, frames, elapsed;

    gettimeofday (&tv_end, NULL);

    if (!frame_counter) {
	tv_start = tv_beg = tv_end;
	signal (SIGINT, signal_handler);
    }

    elapsed = (tv_end.tv_sec - tv_beg.tv_sec) * 100 +
	(tv_end.tv_usec - tv_beg.tv_usec) / 10000;
    total_elapsed = (tv_end.tv_sec - tv_start.tv_sec) * 100 +
	(tv_end.tv_usec - tv_start.tv_usec) / 10000;

    if (final) {
	if (total_elapsed) 
	    tfps = frame_counter * 10000 / total_elapsed;
	else
	    tfps = 0;

	fprintf (stderr,"\n%d frames decoded in %d.%02d "
		 "seconds (%d.%02d fps)\n", frame_counter,
		 total_elapsed / 100, total_elapsed % 100,
		 tfps / 100, tfps % 100);

	return;
    }

    frame_counter++;

    if (elapsed < 50)	/* only display every 0.50 seconds */
	return;

    tv_beg = tv_end;
    frames = frame_counter - last_count;

    fps = frames * 10000 / elapsed;			/* 100x */
    tfps = frame_counter * 10000 / total_elapsed;	/* 100x */

    fprintf (stderr, "%d frames in %d.%02d sec (%d.%02d fps), "
	     "%d last %d.%02d sec (%d.%02d fps)\033[K\r", frame_counter,
	     total_elapsed / 100, total_elapsed % 100,
	     tfps / 100, tfps % 100, frames, elapsed / 100, elapsed % 100,
	     fps / 100, fps % 100);

    last_count = frame_counter;
}

#else /* !HAVE_SYS_TIME_H */

static void print_fps (int final)
{
}

#endif

static void print_usage (char ** argv)
{
    int i;
    vo_driver_t * drivers;

    fprintf (stderr, "usage: %s [-o <mode>] [-s[<track>]] [-c] <file>\n"
	     "\t-s\tuse program stream demultiplexer, "
	     "track 0-15 or 0xe0-0xef\n"
	     "\t-c\tuse c implementation, disables all accelerations\n"
	     "\t-o\tvideo output mode\n", argv[0]);

    drivers = vo_drivers ();
    for (i = 0; drivers[i].name; i++)
	fprintf (stderr, "\t\t\t%s\n", drivers[i].name);

    exit (1);
}

static void handle_args (int argc, char ** argv)
{
    int c;
    vo_driver_t * drivers;
    int i;

    drivers = vo_drivers ();
    while ((c = getopt (argc, argv, "s::co:")) != -1)
	switch (c) {
	case 'o':
	    for (i = 0; drivers[i].name != NULL; i++)
		if (strcmp (drivers[i].name, optarg) == 0)
		    output_open = drivers[i].open;
	    if (output_open == NULL) {
		fprintf (stderr, "Invalid video driver: %s\n", optarg);
		print_usage (argv);
	    }
	    break;

	case 's':
	    demux_track = 0xe0;
	    if (optarg != NULL) {
		char * s;

		demux_track = strtol (optarg, &s, 16);
		if (demux_track < 0xe0)
		    demux_track += 0xe0;
		if ((demux_track < 0xe0) || (demux_track > 0xef) || (*s)) {
		    fprintf (stderr, "Invalid track number: %s\n", optarg);
		    print_usage (argv);
		}
	    }
	    break;

	case 'c':
	    disable_accel = 1;
	    break;

	default:
	    print_usage (argv);
	}

    /* -o not specified, use a default driver */
    if (output_open == NULL)
	output_open = drivers[0].open;

    if (optind < argc) {
	in_file = fopen (argv[optind], "rb");
	if (!in_file) {
	    fprintf (stderr, "%s - couldnt open file %s\n", strerror (errno),
		     argv[optind]);
	    exit (1);
	}
    } else
	in_file = stdin;
}

static int demux (uint8_t * buf, uint8_t * end)
{
    static int mpeg1_skip_table[16] = {
	0, 0, 4, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    /*
     * the demuxer keeps some state between calls:
     * if "state" = DEMUX_HEADER, then "head_buf" contains the first
     *     "bytes" bytes from some header.
     * if "state" == DEMUX_DATA, then we need to copy "bytes" bytes
     *     of ES data before the next header.
     * if "state" == DEMUX_SKIP, then we need to skip "bytes" bytes
     *     of data before the next header.
     *
     * NEEDBYTES makes sure we have the requested number of bytes for a
     * header. If we dont, it copies what we have into head_buf and returns,
     * so that when we come back with more data we finish decoding this header.
     *
     * DONEBYTES updates "buf" to point after the header we just parsed.
     */

#define DEMUX_HEADER 0
#define DEMUX_DATA 1
#define DEMUX_SKIP 2
    static int state = DEMUX_HEADER;
    static int state_bytes = 0;
    static uint8_t head_buf[264];

    uint8_t * header;
    int bytes;
    int len;
    int num_frames;

#define NEEDBYTES(x)						\
    do {							\
	int missing;						\
								\
	missing = (x) - bytes;					\
	if (missing > 0) {					\
	    if (header == head_buf) {				\
		if (missing <= end - buf) {			\
		    memcpy (header + bytes, buf, missing);	\
		    buf += missing;				\
		    bytes = (x);				\
		} else {					\
		    memcpy (header + bytes, buf, end - buf);	\
		    state_bytes = bytes + end - buf;		\
		    return 0;					\
		}						\
	    } else {						\
		memcpy (head_buf, header, bytes);		\
		state = DEMUX_HEADER;				\
		state_bytes = bytes;				\
		return 0;					\
	    }							\
	}							\
    } while (0)

#define DONEBYTES(x)		\
    do {			\
	if (header != head_buf)	\
	    buf = header + (x);	\
    } while (0)

    switch (state) {
    case DEMUX_HEADER:
	if (state_bytes > 0) {
	    header = head_buf;
	    bytes = state_bytes;
	    goto continue_header;
	}
	break;
    case DEMUX_DATA:
	if (state_bytes > end - buf) {
	    num_frames = mpeg2_decode_data (&mpeg2dec, buf, end);
	    while (num_frames--)
		print_fps (0);
	    state_bytes -= end - buf;
	    return 0;
	}
	num_frames = mpeg2_decode_data (&mpeg2dec, buf, buf + state_bytes);
	while (num_frames--)
	    print_fps (0);
	buf += state_bytes;
	break;
    case DEMUX_SKIP:
	if (state_bytes > end - buf) {
	    state_bytes -= end - buf;
	    return 0;
	}
	buf += state_bytes;
	break;
    }

    while (1) {
	header = buf;
	bytes = end - buf;
    continue_header:
	NEEDBYTES (4);
	if (header[0] || header[1] || (header[2] != 1)) {
	    if (header != head_buf) {
		buf++;
		continue;
	    } else {
		header[0] = header[1];
		header[1] = header[2];
		header[2] = header[3];
		bytes = 3;
		goto continue_header;
	    }
	}
	switch (header[3]) {
	case 0xb9:	/* program end code */
	    /* DONEBYTES (4); */
	    /* break;         */
	    return 1;
	case 0xba:	/* pack header */
	    NEEDBYTES (12);
	    if ((header[4] & 0xc0) == 0x40) {	/* mpeg2 */
		NEEDBYTES (14);
		len = 14 + (header[13] & 7);
		NEEDBYTES (len);
		DONEBYTES (len);
		/* header points to the mpeg2 pack header */
	    } else if ((header[4] & 0xf0) == 0x20) {	/* mpeg1 */
		DONEBYTES (12);
		/* header points to the mpeg1 pack header */
	    } else {
		fprintf (stderr, "weird pack header\n");
		exit (1);
	    }
	    break;
	default:
	    if (header[3] == demux_track) {
		NEEDBYTES (7);
		if ((header[6] & 0xc0) == 0x80) {	/* mpeg2 */
		    NEEDBYTES (9);
		    len = 9 + header[8];
		    NEEDBYTES (len);
		    /* header points to the mpeg2 pes header */
		} else {	/* mpeg1 */
		    len = 7;
		    while ((header-1)[len] == 0xff) {
			len++;
			NEEDBYTES (len);
			if (len == 23) {
			    fprintf (stderr, "too much stuffing\n");
			    break;
			}
		    }
		    if (((header-1)[len] & 0xc0) == 0x40) {
			len += 2;
			NEEDBYTES (len);
		    }
		    len += mpeg1_skip_table[(header - 1)[len] >> 4];
		    NEEDBYTES (len);
		    /* header points to the mpeg1 pes header */
		}
		DONEBYTES (len);
		bytes = 6 + (header[4] << 8) + header[5] - len;
		if (bytes <= 0)
		    continue;
		if (bytes > end - buf) {
		    num_frames = mpeg2_decode_data (&mpeg2dec, buf, end);
		    while (num_frames--)
			print_fps (0);
		    state = DEMUX_DATA;
		    state_bytes = bytes - (end - buf);
		    return 0;
		}
		num_frames = mpeg2_decode_data (&mpeg2dec, buf, buf + bytes);
		while (num_frames--)
		    print_fps (0);
		buf += bytes;
	    } else if (header[3] < 0xb9) {
		fprintf (stderr,
			 "looks like a video stream, not system stream\n");
		exit (1);
	    } else {
		NEEDBYTES (6);
		DONEBYTES (6);
		bytes = (header[4] << 8) + header[5];
		if (bytes > end - buf) {
		    state = DEMUX_SKIP;
		    state_bytes = bytes - (end - buf);
		    return 0;
		}
		buf += bytes;
	    }
	}
    }
}

static void ps_loop (void)
{
    uint8_t * end;

    do {
	end = buffer + fread (buffer, 1, BUFFER_SIZE, in_file);
	if (demux (buffer, end))
	    break;	/* hit program_end_code */
    } while (end == buffer + BUFFER_SIZE);
}

static void es_loop (void)
{
    uint8_t * end;
    int num_frames;
		
    do {
	end = buffer + fread (buffer, 1, BUFFER_SIZE, in_file);

	num_frames = mpeg2_decode_data (&mpeg2dec, buffer, end);

	while (num_frames--)
	    print_fps (0);

    } while (end == buffer + BUFFER_SIZE);
}

int main (int argc, char ** argv)
{
    vo_instance_t * output;
    uint32_t accel;

    fprintf (stderr, PACKAGE"-"VERSION
	     " - by Michel Lespinasse <walken@zoy.org> and Aaron Holtzman\n");

    handle_args (argc, argv);

    accel = disable_accel ? 0 : (mm_accel () | MM_ACCEL_MLIB);

    vo_accel (accel);
    output = vo_open (output_open);
    if (output == NULL) {
	fprintf (stderr, "Can not open output\n");
	return 1;
    }
    mpeg2_init (&mpeg2dec, accel, output);

    if (demux_track)
	ps_loop ();
    else
	es_loop ();

    mpeg2_close (&mpeg2dec);
    vo_close (output);
    print_fps (1);
    return 0;
}
