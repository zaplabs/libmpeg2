/*
 *  mpeg2_internal.h
 *
 *  Copyright (C) Aaron Holtzman <aholtzma@ess.engr.uvic.ca> - Mar 2000
 *
 *  This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *	
 *  mpeg2dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  mpeg2dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 
 *
 */

//FIXME normally I wouldn't nest includes, but we'll leave this here until I get
//another chance to move things around
#include "video_out.h"

#ifndef AARONS_TYPES
#define AARONS_TYPES
//typedef to appropriate type for your architecture
typedef unsigned char uint_8;
typedef unsigned short uint_16;
typedef unsigned int uint_32;
typedef signed int sint_32;
typedef signed short sint_16;
typedef signed char sint_8;
#endif

//config flags
#define MPEG2_MMX_ENABLE        0x1
#define MPEG2_3DNOW_ENABLE      0x2
#define MPEG2_SSE_ENABLE        0x4
#define MPEG2_ALTIVEC_ENABLE    0x8
#define MPEG2_XIL_ENABLE        0x10
#define MPEG2_MLIB_ENABLE       0x20

typedef struct mpeg2_config_s
{
	//Bit flags that enable various things
	uint_32 flags;
	//Callback that points the decoder to new stream data
  void   (*fill_buffer_callback)(uint_8 **, uint_8 **);
} mpeg2_config_t;

void mpeg2_init(vo_functions_t*);
uint_32 mpeg2_decode_data(uint_8 *data_start,uint_8 *data_end);