/* 
 *    video_out_syncfb.c
 *
 *	Copyright (C) Aaron Holtzman - Aug 1999
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include "config.h"

#ifdef LIBVO_SYNCFB

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/videodev.h>

#include "video_out.h"
#include "video_out_internal.h"
#include "drivers/syncfb.h"

LIBVO_EXTERN(syncfb)

static vo_info_t vo_info =
{
	"Matrox G200/G400 Synchronous (/dev/syncfb)",
	"syncfb",
	"Matthias Oelmann <mao@well.com>",
	""
};

/* deinterlacing on? looks only good in 50 Hz(PAL) or 60 Hz(NTSC) modes */
static int vo_conf_deinterlace = 1;

/* 72/75 Hz Monitor frequency for progressive output */
static int vo_conf_cinemode = 1;


static syncfb_config_t config;
static syncfb_capability_t sfb_caps;

static syncfb_buffer_info_t bufinfo;

static uint_8 *vid_data;
static uint_8 *frame_mem;

static int debug_skip_first = 250;
static int dbg_singleframe = 0;

static int conf_palette;

static int f;



/*
	it seems that mpeg2dec never calls
	draw_frame, so i could not test it....
*/

static void
write_frame_YUV422(uint_8 *y,uint_8 *cr, uint_8 *cb)
{
	uint_8 *crp, *cbp;
	uint_32 *dest32;
	uint_32 bespitch,h,w;


	bespitch = config.src_pitch;
	dest32 = (uint_32 *)vid_data;

	for(h=0; h < config.src_height/2; h++)
	{
		cbp = cb;
		crp = cr;
		for(w=0; w < config.src_width/2; w++)
		{
			*dest32++ = (*y) + ((*cr)<<8) + ((*(y+1))<<16) + ((*cb)<<24);
			y++; y++; cb++; cr++;
		}
		dest32 += (bespitch - config.src_width) / 2;

		for(w=0; w < config.src_width/2; w++)
		{
			*dest32++ = (*y) + ((*crp)<<8) + ((*(y+1))<<16) + ((*cbp)<<24);
			y++; y++; cbp++; crp++;
		}
		dest32 += (bespitch - config.src_width) / 2;
	}
}


static void
write_frame_YUV420P2(uint_8 *y,uint_8 *cr, uint_8 *cb)
{
	uint_8 *dest, *tmp;
	uint_32 bespitch,h,w;

	bespitch = config.src_pitch;
	dest = frame_mem + bufinfo.offset;

	for(h=0; h < config.src_height; h++)
	{
		memcpy(dest, y, config.src_width);
		y += config.src_width;
		dest += bespitch;
	}

	dest = frame_mem + bufinfo.offset_p2;
	for(h=0; h < config.src_height/2; h++)
	{
		tmp = dest;
		for(w=0; w < config.src_width/2; w++)
		{
			*tmp++ =  *cr++;
			*tmp++ =  *cb++;
		}
		dest += bespitch;
	}
}

static void
write_frame_YUV420P3(uint_8 *y,uint_8 *cr, uint_8 *cb)
{
}

static void
write_slice_YUV420P2(uint_8 *y,uint_8 *cr, uint_8 *cb,uint_32 slice_num)
{
	uint_8 *dest, *tmp;
	uint_32 bespitch,h,w;

	bespitch = config.src_pitch;
	dest = frame_mem + bufinfo.offset + (bespitch * 16 * slice_num);

	for(h=0; h < 16; h++)
	{
		memcpy(dest, y, config.src_width);
		y += config.src_width;
		dest += bespitch;
	}

	dest = frame_mem + bufinfo.offset_p2 + (bespitch * 16 * slice_num) /2;
	for(h=0; h < 8; h++)
	{
		tmp = dest;
		for(w=0; w < config.src_width/2; w++)
		{
			*tmp++ =  *cr++;
			*tmp++ =  *cb++;
		}
		dest += bespitch;
	}
}

static void
write_slice_YUV420P3(uint_8 *y,uint_8 *cr, uint_8 *cb,uint_32 slice_num)
{
	uint_8 *dest;
	uint_32 bespitch,h;

	bespitch = config.src_pitch;
	dest = frame_mem + bufinfo.offset + (bespitch * 16 * slice_num);

	for(h=0; h < 16; h++)
	{
		memcpy(dest, y, config.src_width);
		y += config.src_width;
		dest += bespitch;
	}

	dest = frame_mem + bufinfo.offset_p2 + (bespitch * 16 * slice_num)/4;
	for(h=0; h < 8; h++)
	{
		memcpy(dest, cr, config.src_width/2);
		cr += config.src_width/2;
		dest += bespitch/2;
	}

	dest = frame_mem + bufinfo.offset_p3 + (bespitch * 16 * slice_num)/4;
	for(h=0; h < 8; h++)
	{
		memcpy(dest, cb, config.src_width/2);
		cb += config.src_width/2;
		dest += bespitch/2;
	}


}


static void
write_slice_YUV422(uint_8 *y,uint_8 *cr, uint_8 *cb,uint_32 slice_num)
{
	uint_8 *crp, *cbp;
	uint_32 *dest32;
	uint_32 bespitch,h,w;


	bespitch = config.src_pitch;
	dest32 = (uint_32 *)(vid_data + (bespitch * 16 * slice_num) * 2);

	for(h=0; h < 8; h++)
	{
		cbp = cb;
		crp = cr;
		for(w=0; w < config.src_width/2; w++)
		{
			*dest32++ = (*y) + ((*cr)<<8) + ((*(y+1))<<16) + ((*cb)<<24);
			y++; y++; cb++; cr++;
		}
		dest32 += (bespitch - config.src_width) / 2;

		for(w=0; w < config.src_width/2; w++)
		{
			*dest32++ = (*y) + ((*crp)<<8) + ((*(y+1))<<16) + ((*cbp)<<24);
			y++; y++; cbp++; crp++;
		}
		dest32 += (bespitch - config.src_width) / 2;
	}
}

static uint32_t draw_slice(uint8_t *src[], int slice_num)
{
	if ( vid_data == NULL ) return 0;

	if ( conf_palette == VIDEO_PALETTE_YUV422 ) {
		write_slice_YUV422(src[0],src[1], src[2],slice_num);
	} else if ( conf_palette == VIDEO_PALETTE_YUV420P2 ) {
		write_slice_YUV420P2(src[0],src[1], src[2],slice_num);
	} else if ( conf_palette == VIDEO_PALETTE_YUV420P3 ) {
		write_slice_YUV420P3(src[0],src[1], src[2],slice_num);
	}

	return 0;
}




static void
flip_page(void)
{

//	memset(frame_mem + bufinfo.offset_p2, 0x80, config.src_width*config.src_height);
	ioctl(f,SYNCFB_COMMIT_BUFFER,&bufinfo);

	if ( dbg_singleframe ) {
		if ( debug_skip_first == 0 ) {
			printf( "Press 'anykey' for field 1\n" );
			getchar();
			ioctl(f,SYNCFB_VBI,0);
		}

		if ( debug_skip_first > 0 ) {
			debug_skip_first--;
			// debug_skip_first = 0;
			if ( debug_skip_first == 0 ) {
			ioctl(f,SYNCFB_VBI,0);
			ioctl(f,SYNCFB_VBI,0);
			ioctl(f,SYNCFB_VBI,0);
			}
		}

		if ( debug_skip_first == 0 ) {
			printf( "Press 'anykey' for field 2\n" );
			getchar();
			ioctl(f,SYNCFB_VBI,0);
		}
	}

	ioctl(f,SYNCFB_REQUEST_BUFFER,&bufinfo);
	if ( bufinfo.id == -1 ) printf( "Got buffer #%d\n", bufinfo.id );

	vid_data = (uint_8 *)(frame_mem + bufinfo.offset);
	if ( bufinfo.id == -1 ) {
		//vid_data = frame_mem;
		vid_data = NULL;
	}
//	printf("Flip %d\n", bufinfo.offset);

}

static uint32_t draw_frame(uint8_t *src[])
{
	printf("DRAW FRAME!!!\n");
	if ( conf_palette == VIDEO_PALETTE_YUV422 ) {
		write_frame_YUV422(src[0],src[1], src[2]);
	} else if ( conf_palette == VIDEO_PALETTE_YUV420P2 ) {
		write_frame_YUV420P2(src[0],src[1], src[2]);
	} else if ( conf_palette == VIDEO_PALETTE_YUV420P3 ) {
		write_frame_YUV420P3(src[0],src[1], src[2]);
	}

	flip_page();
	return 0;
}

static uint32_t init(int width, int height, int fullscreen, char *title, uint32_t format)
{
	uint_32 frame_size;

	f = open("/dev/syncfb",O_RDWR);

	if(f == -1)
	{
		f = open("/dev/mga_vid",O_RDWR);
		if(f == -1)
		{
			fprintf(stderr,"Couldn't open /dev/syncfb or /dev/mga_vid\n");
			return(-1);
		}
	}

	if (ioctl(f,SYNCFB_GET_CAPS,&sfb_caps)) perror("Error in mga_vid_config ioctl");
	if (ioctl(f,SYNCFB_GET_CONFIG,&config)) perror("Error in mga_vid_config ioctl");

	if (sfb_caps.palettes & (1<<VIDEO_PALETTE_YUV420P3) ) {
		config.src_palette= VIDEO_PALETTE_YUV420P3;
		printf("using palette yuv420p3\n");
	}else if ( sfb_caps.palettes & (1<<VIDEO_PALETTE_YUV420P2) ) {
		config.src_palette= VIDEO_PALETTE_YUV420P2;
		printf("using palette yuv420p2\n");
	} else if ( sfb_caps.palettes & (1<<VIDEO_PALETTE_YUV422) ) {
		config.src_palette= VIDEO_PALETTE_YUV422;
		printf("using palette yuv422\n");
	} else {
		printf("no supported palette found\n");
		exit(1);
	}

	// config.src_palette= VIDEO_PALETTE_YUV422;

	if ( vo_conf_cinemode ) {
		config.default_repeat = 3;
	} else {
		config.default_repeat = 2;
	}

	conf_palette = config.src_palette;
	if ( vo_conf_deinterlace ) {
		config.syncfb_mode = SYNCFB_FEATURE_SCALE | SYNCFB_FEATURE_BLOCK_REQUEST | SYNCFB_FEATURE_DEINTERLACE;
		config.default_repeat = 1;
	} else {
		config.syncfb_mode = SYNCFB_FEATURE_SCALE | SYNCFB_FEATURE_BLOCK_REQUEST;
	}

	config.fb_screen_size = (1280 * 1024 * 8) / 8;
	config.src_width = width;
	config.src_height= height;

	config.image_width = width;
	config.image_height= height; 
	//config.image_width = 1024;
	//config.image_height= 576;

	config.image_xorg= 10;
	config.image_yorg= 10;


	printf ("BES Sourcer size: %d x %d\n", width, height);

	ioctl(f,SYNCFB_ON,0);
	if (ioctl(f,SYNCFB_SET_CONFIG,&config)) perror("Error in mga_vid_config ioctl");

	printf ("Framebuffer memory: %ld in %ld buffers\n", sfb_caps.memory_size, config.buffers);

	frame_size = ((width + 31) & ~31) * height + (((width + 31) & ~31) * height) / 2;
	frame_mem = (uint_8*)mmap(0,sfb_caps.memory_size,PROT_WRITE,MAP_SHARED,f,0);

	printf( "Requesting first buffer #%d\n", bufinfo.id );
	ioctl(f,SYNCFB_REQUEST_BUFFER,&bufinfo);
	printf( "Got first buffer #%d\n", bufinfo.id );


	vid_data = (uint_8 *)(frame_mem + bufinfo.offset);

	//clear the buffer
	// memset(frame_mem,0x80,frame_size*2);
  	return 0;
}

static const vo_info_t*
get_info(void)
{
	return &vo_info;
}

//FIXME this should allocate AGP memory via agpgart and then we
//can use AGP transfers to the framebuffer
static vo_image_buffer_t* allocate_image_buffer()
{
	//use the generic fallback
	return allocate_image_buffer_common(config.src_height,config.src_width,0x32315659);
}

static void	
free_image_buffer(vo_image_buffer_t* image)
{
	//use the generic fallback
	free_image_buffer_common(image);
}

#endif
