//PLUGIN_INFO(INFO_NAME, "Simple Direct Media Library (SDL)");
//PLUGIN_INFO(INFO_AUTHOR, "Ryan C. Gordon <icculus@lokigames.com>");

/*
 *  video_out_sdl.c
 *
 *  Copyright (C) Ryan C. Gordon <icculus@lokigames.com> - April 22, 2000.
 *
 *  A mpeg2dec display driver that does output through the
 *  Simple DirectMedia Layer (SDL) library. This effectively gives us all
 *  sorts of output options: X11, SVGAlib, fbcon, AAlib, GGI. Win32, MacOS
 *  and BeOS support, too. Yay. SDL info, source, and binaries can be found
 *  at http://slouken.devolution.com/SDL/
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <oms/oms.h>
#include <oms/plugin/output_video.h>

#include <SDL/SDL.h>

static SDL_Surface *surface = NULL;
static SDL_Overlay *overlay = NULL;
static SDL_Rect dispSize;
static Uint8 *keyState = NULL;
static int framePlaneY = -1;
static int framePlaneUV = -1;
static int slicePlaneY = -1;
static int slicePlaneUV = -1;


static int _sdl_open		(plugin_t *plugin, void *name);
static int _sdl_close		(plugin_t *plugin);
static int _sdl_setup		(uint32_t width, uint32_t height, uint32_t fullscreen, char *title);
static int _sdl_draw_frame	(uint8_t *src[]);
static int _sdl_draw_slice	(uint8_t *src[], uint32_t slice_num);
static void flip_page		(void);
static void free_image_buffer	(vo_image_buffer_t* image);
static vo_image_buffer_t *allocate_image_buffer (uint32_t height, uint32_t width, uint32_t format);

static plugin_output_video_t video_sdl = {
        NULL,
        _sdl_open,
        _sdl_close,
        _sdl_setup,
        _sd_draw_frame,
        _sd_draw_slice,
        flip_page,
        allocate_image_buffer,
        free_image_buffer
};


/**
 *
 **/

static int _sdl_open (plugin_t *plugin, void *name)
{
	return 0;
}


/**
 *
 **/

static int _sdl_close (plugin_t *plugin)
{
	return 0;
}


/**
 * Take a null-terminated array of pointers, and find the last element.
 *
 *    params : array == array of which we want to find the last element.
 *   returns : index of last NON-NULL element.
 */*

static inline int findArrayEnd(SDL_Rect **array)
{
	int i;
	for (i = 0; array[i] != NULL; i++);  // keep loopin'...
	return i-1;
}


/**
 * Initialize an SDL surface and an SDL YUV overlay.
 *
 *    params : width  == width of video we'll be displaying.
 *             height == height of video we'll be displaying.
 *             fullscreen == want to be fullscreen?
 *             title == Title for window titlebar.
 *   returns : non-zero on success, zero on error.
 **/

static int _sdl_setup (uint32_t width, uint32_t height, uint32_t fullscreen, char *title)
{
	int rc = 0;
	int i = 0;
	const SDL_VideoInfo *vidInfo = NULL;
	int desiredWidth = -1;
	int desiredHeight = -1;
	SDL_Rect **modes = NULL;
	Uint32 sdlflags = SDL_HWSURFACE;
	Uint8 bpp;

	if (fullscreen)
		sdlflags |= SDL_FULLSCREEN;

	if ((rc = SDL_Init(SDL_INIT_VIDEO))) {
		printf("SDL: SDL_Init() failed! rc == (%d).\n", rc);
		return 0;
	}

	atexit (SDL_Quit);

	vidInfo = SDL_GetVideoInfo();

	if (!(modes = SDL_ListModes(vidInfo->vfmt, sdlflags))) {
		sdlflags &= ~SDL_FULLSCREEN;
		if (!(modes = SDL_ListModes(vidInfo->vfmt, sdlflags))) { // try without fullscreen.
			sdlflags &= ~SDL_HWSURFACE;
			modes = SDL_ListModes(vidInfo->vfmt, sdlflags);   // give me ANYTHING.
			if (modes == NULL) {
				printf("SDL: SDL_ListModes() failed.\n");
				return(0);
			}
		}
	}

	if (modes == (SDL_Rect **) -1) {   // anything is fine.
		desiredWidth = width;
		desiredHeight = height;
	} else {
            // we want to get the lowest resolution that'll fit the video.
            //  ...so start at the far end of the array.
        for (i = findArrayEnd(modes); ((i >= 0) && (desiredWidth == -1)); i--)
        {
            if ((modes[i]->w >= width) && (modes[i]->h >= height))
            {
                desiredWidth = modes[i]->w;
                desiredHeight = modes[i]->h;
            } // if
        } // for
    } // else

    if ((desiredWidth < 0) || (desiredHeight < 0))
    {
        printf("SDL: Couldn't produce a mode with at least"
               " a (%dx%d) resolution!\n", width, height);
        return(0);
    } // if

    dispSize.x = (desiredWidth - width) / 2;
    dispSize.y = (desiredHeight - height) / 2;
    dispSize.w = width;
    dispSize.h = height;

        // hide cursor. The cursor is annoying in fullscreen, and when
        //  using the SDL AAlib target, it tries to draw the cursor,
        //  which slows us down quite a bit.
//    if ((sdlflags & SDL_FULLSCREEN) ||
    SDL_ShowCursor(0);

        // YUV overlays need at least 16-bit color depth, but the
        //  display might less. The SDL AAlib target says it can only do
        //  8-bits, for example. So, if the display is less than 16-bits,
        //  we'll force the BPP to 16, and pray that SDL can emulate for us.
    bpp = vidInfo->vfmt->BitsPerPixel;
    if (bpp < 16)
    {
        printf("\n\n"
               "WARNING: Your SDL display target wants to be at a color\n"
               " depth of (%d), but we need it to be at least 16 bits,\n"
               " so we need to emulate 16-bit color. This is going to slow\n"
               " things down; you might want to increase your display's\n"
               " color depth, if possible.\n\n", bpp);
        bpp = 16;  // (*shrug*)
    } // if

    surface = SDL_SetVideoMode(desiredWidth, desiredHeight, bpp, sdlflags);
    if (surface == NULL)
    {
        printf("ERROR: SDL could not set the video mode!\n");
        return(0);
    } // if

    if (title == NULL)
        title = "Help! I'm trapped inside a Palm IIIc!";

    SDL_WM_SetCaption(title, "MPEG2DEC");

    overlay = SDL_CreateYUVOverlay(width, height, SDL_IYUV_OVERLAY, surface);
    if (overlay == NULL)
    {
        printf("ERROR: Couldn't create an SDL-based YUV overlay!\n");
        return(0);
    } // if

    keyState = SDL_GetKeyState(NULL);

    framePlaneY = (dispSize.w * dispSize.h);
    framePlaneUV = ((dispSize.w / 2) * (dispSize.h / 2));
    slicePlaneY = ((dispSize.w) * 16);
    slicePlaneUV = ((dispSize.w / 2) * (8));

// temp !!!!
setbuf(stdout, NULL);
    return(-1);  // non-zero == SUCCESS. Oooh yeah.
} // display_init


/**
 * Draw a frame to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the frame.
 *  returns : non-zero on success, zero on error.
 **/

static int _sdl_draw_frame (uint8_t *src[])
{
	char *dst;

	if (SDL_LockYUVOverlay(overlay)) {
		printf("ERROR: Couldn't lock SDL-based YUV overlay!\n");
		return 0;
	}

	dst = overlay->pixels;
	memcpy(dst, src[0], framePlaneY);
	dst += framePlaneY;
	memcpy(dst, src[1], framePlaneUV);
	dst += framePlaneUV;
	memcpy(dst, src[2], framePlaneUV);

	SDL_UnlockYUVOverlay(overlay);
	flip_page();

	return -1;
}


/**
 * Draw a slice (16 rows of image) to the SDL YUV overlay.
 *
 *   params : *src[] == the Y, U, and V planes that make up the slice.
 *  returns : non-zero on success, zero on error.
 **/

static int _sdl_draw_slice (uint8_t *src[], uint32_t slice_num)
{
	char *dst;

	if (SDL_LockYUVOverlay(overlay)) {
		printf("ERROR: Couldn't lock SDL-based YUV overlay!\n");
		return 0;
	}

	dst = overlay->pixels + (slicePlaneY * slice_num);
	memcpy(dst, src[0], slicePlaneY);
	dst = (overlay->pixels + framePlaneY) + (slicePlaneUV * slice_num);
	memcpy(dst, src[1], slicePlaneUV);
	dst += framePlaneUV;
	memcpy(dst, src[2], slicePlaneUV);

	SDL_UnlockYUVOverlay(overlay);
	return -1;
}


/**
 *
 **/

static void flip_page (void)
{
	SDL_PumpEvents();  // get keyboard and win resize events.

	if ((SDL_GetModState() & KMOD_ALT) &&
		((keyState[SDLK_KP_ENTER] == SDL_PRESSED) ||
		(keyState[SDLK_RETURN] == SDL_PRESSED))) {
		SDL_WM_ToggleFullScreen(surface);
	}

	SDL_DisplayYUVOverlay(overlay, &dispSize);
}


/**
 *
 **/

static vo_image_buffer_t *allocate_image_buffer (uint32_t height, uint32_t width, uint32_t format)
{
	vo_image_buffer_t *image;

	if (!(image = malloc (sizeof (vo_image_buffer_t))))
		return NULL;

	image->height   = height;
	image->width    = width;
	image->format   = format;

	//we only know how to do 4:2:0 planar yuv right now.
	if (!(image->base = malloc (width * height * 3 / 2))) {
		free(image);
		return NULL;
	}

	return image;
}


/**
 *
 **/

static void free_image_buffer (vo_image_buffer_t* image)
{
	free (image->base);
	free (image);
}


/**
 * Initialize Plugin.
 **/

void *plugin_init (char *whoami)
{
	pluginRegister (whoami,
		PLUGIN_ID_OUTPUT_VIDEO,
		0,
		&video_sdl);

	return &video_sdl;
}


/**
 * Cleanup Plugin.
 **/

void plugin_exit (void)
{
}

