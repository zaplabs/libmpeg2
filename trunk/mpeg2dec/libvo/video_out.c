/*
 * video_out.c
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
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

#include <stdlib.h>
#include <inttypes.h>

#include "video_out.h"

/* Externally visible list of all vo drivers */

extern vo_open_t vo_xv_open;
extern vo_open_t vo_xv2_open;
extern vo_open_t vo_x11_open;
extern vo_open_t vo_dxrgb_open;
extern vo_open_t vo_dx_open;
extern vo_open_t vo_sdl_open;
extern vo_open_t vo_null_open;
extern vo_open_t vo_nullslice_open;
extern vo_open_t vo_nullrgb16_open;
extern vo_open_t vo_nullrgb32_open;
extern vo_open_t vo_pgm_open;
extern vo_open_t vo_pgmpipe_open;
extern vo_open_t vo_md5_open;

static vo_driver_t video_out_drivers[] = {
#ifdef LIBVO_XV
    {"xv", vo_xv_open},
    {"xv2", vo_xv2_open},
#endif
#ifdef LIBVO_X11
    {"x11", vo_x11_open},
#endif
#ifdef LIBVO_DX
    {"dxrgb", vo_dxrgb_open},
    {"dx", vo_dx_open},
#endif
#ifdef LIBVO_SDL
    {"sdl", vo_sdl_open},
#endif
    {"null", vo_null_open},
    {"nullslice", vo_nullslice_open},
    {"nullrgb16", vo_nullrgb16_open},
    {"nullrgb32", vo_nullrgb32_open},
    {"pgm", vo_pgm_open},
    {"pgmpipe", vo_pgmpipe_open},
    {"md5", vo_md5_open},
    {NULL, NULL}
};

vo_driver_t const * vo_drivers (void)
{
    return video_out_drivers;
}
