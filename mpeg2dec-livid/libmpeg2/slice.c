/*
 * slice.c
 *
 * Copyright (C) Aaron Holtzman <aholtzma@ess.engr.uvic.ca> - Nov 1999
 *
 * Decodes an MPEG-2 video stream.
 *
 * This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *	
 * mpeg2dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * mpeg2dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU Make; see the file COPYING. If not, write to
 * the Free Software Foundation, 
 *
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "mpeg2.h"
#include "mpeg2_internal.h"

#include "motion_comp.h"
#include "bitstream.h"
#include "idct.h"

extern mc_functions_t mc_functions;
extern void (*idct_block_copy) (int16_t * block, uint8_t * dest, int stride);
extern void (*idct_block_add) (int16_t * block, uint8_t * dest, int stride);

//XXX put these on the stack in slice_process?
static slice_t slice;
int16_t DCTblock[64] ALIGN_16_BYTE;

typedef struct {
    char run, level, len;
} DCTtab;

extern DCTtab DCTtabfirst[],DCTtabnext[],DCTtab0[],DCTtab1[];
extern DCTtab DCTtab2[],DCTtab3[],DCTtab4[],DCTtab5[],DCTtab6[];
extern DCTtab DCTtab0a[],DCTtab1a[];

static int non_linear_quantizer_scale[32] =
{
     0, 1, 2, 3, 4, 5, 6, 7,
     8,10,12,14,16,18,20,22,
    24,28,32,36,40,44,48,52,
    56,64,72,80,88,96,104,112
};

static inline int get_quantizer_scale (int q_scale_type)
{
    int quantizer_scale_code;

    quantizer_scale_code = bitstream_get (5);

    if (q_scale_type)
	return non_linear_quantizer_scale[quantizer_scale_code];
    else
	return quantizer_scale_code << 1;
}

//This needs to be rewritten
static inline int slice_get_block_coeff (uint16_t * run, int16_t * val,
					 int non_intra_dc,
					 int intra_vlc_format)
{
    uint32_t code;
    DCTtab * tab;

    //this routines handles intra AC and non-intra AC/DC coefficients
    code = bitstream_show (16);
 
    //FIXME use a pointer to the right vlc format table based on
    //intra_vlc_format
    if (code>=16384 && !intra_vlc_format) {
	if (non_intra_dc)
	    tab = &DCTtabfirst[ (code>>12)-4];
	else
	    tab = &DCTtabnext[ (code>>12)-4];
    } else if (code>=1024) {
	if (intra_vlc_format)
	    tab = &DCTtab0a[ (code>>8)-4];
	else
	    tab = &DCTtab0[ (code>>8)-4];
    } else if (code>=512) {
	if (intra_vlc_format)
	    tab = &DCTtab1a[ (code>>6)-8];
	else
	    tab = &DCTtab1[ (code>>6)-8];
    } else if (code>=256)
	tab = &DCTtab2[ (code>>4)-16];
    else if (code>=128)
	tab = &DCTtab3[ (code>>3)-16];
    else if (code>=64)
	tab = &DCTtab4[ (code>>2)-16];
    else if (code>=32)
	tab = &DCTtab5[ (code>>1)-16];
    else if (code>=16)
	tab = &DCTtab6[code-16];
    else {
	fprintf (stderr," (vlc) invalid huffman code 0x%x in vlc_get_block_coeff ()\n",code);
	exit (1);
	return 0;
    }

    bitstream_flush (tab->len);

    if (tab->run==64) // end_of_block 
	return 0;

    if (tab->run==65) /* escape */ {
	*run = bitstream_get (6);

	*val = bitstream_get (12);
	if (*val >= 2048)
	    *val = *val - 4096;
    } else {
	*run = tab->run;

	*val = tab->level;	 
	if (bitstream_get (1)) //sign bit
	    *val = -*val;
    }

    return 1;
}

static void slice_get_intra_block (const picture_t * picture,
				   slice_t * slice,
				   int16_t * dest, int cc)
{
    uint32_t i = 1;
    uint32_t j;
    uint16_t run;
    int16_t val;
    const uint8_t *scan = picture->scan;
    uint8_t *quant_matrix = picture->intra_quantizer_matrix;
    int16_t quantizer_scale = slice->quantizer_scale;
    int16_t mismatch;

    //Get the intra DC coefficient and inverse quantize it
    if (cc == 0)
	dest[0] = (slice->dc_dct_pred[0] += Get_Luma_DC_dct_diff ()) <<
	    (3 - picture->intra_dc_precision);
    else
	dest[0] = (slice->dc_dct_pred[cc]+= Get_Chroma_DC_dct_diff ()) <<
	    (3 - picture->intra_dc_precision);

    i = 1;
    mismatch = ~dest[0];
    while ((slice_get_block_coeff (&run,&val,0,picture->intra_vlc_format))) {
	i += run;
	j = scan[i++];
	mismatch ^= dest[j] = (val * quantizer_scale * quant_matrix[j]) / 16;
    }
    dest[63] ^= mismatch & 1;
}

static void slice_get_non_intra_block (const picture_t * picture,
				       slice_t * slice, int16_t * dest)
{
    uint32_t i;
    uint32_t j;
    uint16_t run;
    int16_t val;
    const uint8_t *scan = picture->scan;
    uint8_t *quant_matrix = picture->non_intra_quantizer_matrix;
    int16_t quantizer_scale = slice->quantizer_scale;
    int k;
    int16_t mismatch;

    i = 0;
    mismatch = 1;
    while ((slice_get_block_coeff (&run,&val,i==0,0))) {
	i += run;
	j = scan[i++];
	k = (val > 0) ? 1 : ((val < 0) ? -1 : 0);
	mismatch ^= dest[j] =
	    ((2 * val + k) * quantizer_scale * quant_matrix[j]) / 32;
    }
    dest[63] ^= mismatch & 1;
}

static inline void slice_intra_DCT (picture_t * picture, slice_t * slice,
				    int cc, uint8_t * dest, int stride)
{
    slice_get_intra_block (picture, slice, DCTblock, cc);
    idct_block_copy (DCTblock, dest, stride);
    memset (DCTblock, 0, sizeof (int16_t) * 64);
}

static inline void slice_non_intra_DCT (picture_t * picture, slice_t * slice,
					uint8_t * dest, int stride)
{
    slice_get_non_intra_block (picture, slice, DCTblock);
    idct_block_add (DCTblock, dest, stride);
    memset (DCTblock, 0, sizeof (int16_t) * 64);
}

static int get_motion_delta (int f_code)
{
    int motion_code, motion_residual;

    motion_code = Get_motion_code ();
    if (motion_code == 0)
	return 0;

    motion_residual = 0;
    if (f_code != 0)
	motion_residual = bitstream_get (f_code);

    if (motion_code > 0)
	return ((motion_code - 1) << f_code) + motion_residual + 1;
    else
	return ((motion_code + 1) << f_code) - motion_residual - 1;
}

static inline int bound_motion_vector (int vector, int f_code)
{
#if 1
    int limit;

    limit = 16 << f_code;

    if (vector >= limit)
	return vector - 2*limit;
    else if (vector < -limit)
	return vector + 2*limit;
    else return vector;
#else
    return (vector << (27 - f_code)) >> (27 - f_code);
#endif
}

void motion_frame (motion_t * motion, uint8_t * dest[3], int offset, int width,
		   void (** table) (uint8_t *, uint8_t *, int, int))
{
    int motion_x, motion_y;

    motion_x = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = motion_x;

    motion_y = motion->pmv[0][1] + get_motion_delta (motion->f_code[1]);
    motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = motion_y;

    motion_block (table, motion_x, motion_y, dest, offset,
		  motion->ref_frame, offset, width, 16);
}

void motion_field (motion_t * motion, uint8_t * dest[3], int offset, int width,
		   void (** table) (uint8_t *, uint8_t *, int, int))
{
    int vertical_field_select;
    int motion_x, motion_y;

    vertical_field_select = bitstream_get (1);

    motion_x = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[0][0] = motion_x;

    motion_y = (motion->pmv[0][1] >> 1) + get_motion_delta (motion->f_code[1]);
    //motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[0][1] = motion_y << 1;

    motion_block (table, motion_x, motion_y, dest, offset,
		  motion->ref_frame, offset + vertical_field_select * width,
		  width * 2, 8);

    vertical_field_select = bitstream_get (1);

    motion_x = motion->pmv[1][0] + get_motion_delta (motion->f_code[0]);
    motion_x = bound_motion_vector (motion_x, motion->f_code[0]);
    motion->pmv[1][0] = motion_x;

    motion_y = (motion->pmv[1][1] >> 1) + get_motion_delta (motion->f_code[1]);
    //motion_y = bound_motion_vector (motion_y, motion->f_code[1]);
    motion->pmv[1][1] = motion_y << 1;

    motion_block (table, motion_x, motion_y, dest, offset + width,
		  motion->ref_frame, offset + vertical_field_select * width,
		  width * 2, 8);
}

// like motion_frame, but reuse previous motion vectors
void motion_reuse (motion_t * motion, uint8_t * dest[3], int offset, int width,
		   void (** table) (uint8_t *, uint8_t *, int, int))
{
    motion_block (table, motion->pmv[0][0], motion->pmv[0][1], dest, offset,
		  motion->ref_frame, offset, width, 16);
}

// like motion_frame, but use null motion vectors
void motion_zero (motion_t * motion, uint8_t * dest[3], int offset, int width,
		  void (** table) (uint8_t *, uint8_t *, int, int))
{
    motion_block (table, 0, 0, dest, offset,
		  motion->ref_frame, offset, width, 16);
}

// like motion_frame, but no actual motion compensation
void motion_conceal (motion_t * motion)
{
    int tmp;

    tmp = motion->pmv[0][0] + get_motion_delta (motion->f_code[0]);
    tmp = bound_motion_vector (tmp, motion->f_code[0]);
    motion->pmv[1][0] = motion->pmv[0][0] = tmp;

    tmp = motion->pmv[0][1] + get_motion_delta (motion->f_code[1]);
    tmp = bound_motion_vector (tmp, motion->f_code[1]);
    motion->pmv[1][1] = motion->pmv[0][1] = tmp;

    bitstream_flush (1); // remove marker_bit
}


#define MOTION(routine,direction,slice,dest,offset,stride)	\
do {								\
    if ((direction) & MACROBLOCK_MOTION_FORWARD)		\
	routine (& ((slice).f_motion), dest, offset, stride,	\
		 mc_functions.put);				\
    if ((direction) & MACROBLOCK_MOTION_BACKWARD)		\
	routine (& ((slice).b_motion), dest, offset, stride,	\
		 ((direction) & MACROBLOCK_MOTION_FORWARD ?	\
		  mc_functions.avg : mc_functions.put));	\
} while (0);

int get_macroblock_modes (int picture_coding_type, int frame_pred_frame_dct)
{
    int macroblock_modes;

    macroblock_modes = Get_macroblock_type (picture_coding_type);

    if (frame_pred_frame_dct) {
	if (macroblock_modes & (MACROBLOCK_MOTION_FORWARD |
				MACROBLOCK_MOTION_BACKWARD))
	    macroblock_modes |= MC_FRAME;
	return macroblock_modes;
    }

    // get frame/field motion type 
    if (macroblock_modes & (MACROBLOCK_MOTION_FORWARD |
			    MACROBLOCK_MOTION_BACKWARD))
	macroblock_modes |= bitstream_get (2) * MOTION_TYPE_BASE;

    if (macroblock_modes & (MACROBLOCK_INTRA | MACROBLOCK_PATTERN))
	macroblock_modes |= bitstream_get (1) * DCT_TYPE_INTERLACED;

    return macroblock_modes;
}

int slice_process (picture_t * picture, uint8_t code, uint8_t * buffer)
{
    int mba; 
    int mba_inc;
    int macroblock_modes;
    int width;
    uint8_t * dest[3];
    int offset;

    width = picture->coded_picture_width;
    mba = (code - 1) * (picture->coded_picture_width >> 4);
    offset = (code - 1) * width * 4;

    dest[0] = picture->current_frame[0] + offset * 4;
    dest[1] = picture->current_frame[1] + offset;
    dest[2] = picture->current_frame[2] + offset;
    slice.f_motion.ref_frame[0] =
	picture->forward_reference_frame[0] + offset * 4;
    slice.f_motion.ref_frame[1] = picture->forward_reference_frame[1] + offset;
    slice.f_motion.ref_frame[2] = picture->forward_reference_frame[2] + offset;
    slice.f_motion.f_code[0] = picture->f_code[0][0];
    slice.f_motion.f_code[1] = picture->f_code[0][1];
    slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
    slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;
    slice.b_motion.ref_frame[0] =
	picture->backward_reference_frame[0] + offset * 4;
    slice.b_motion.ref_frame[1] =
	picture->backward_reference_frame[1] + offset;
    slice.b_motion.ref_frame[2] =
	picture->backward_reference_frame[2] + offset;
    slice.b_motion.f_code[0] = picture->f_code[1][0];
    slice.b_motion.f_code[1] = picture->f_code[1][1];
    slice.b_motion.pmv[0][0] = slice.b_motion.pmv[0][1] = 0;
    slice.b_motion.pmv[1][0] = slice.b_motion.pmv[1][1] = 0;

    //reset intra dc predictor
    slice.dc_dct_pred[0]=slice.dc_dct_pred[1]=slice.dc_dct_pred[2]= 
	1<< (picture->intra_dc_precision + 7) ;

    bitstream_init (buffer);

    slice.quantizer_scale = get_quantizer_scale (picture->q_scale_type);

    //Ignore intra_slice and all the extra data
    while (bitstream_get (1))
	bitstream_flush (8);

    mba_inc = Get_macroblock_address_increment () - 1;
    mba += mba_inc;
    offset = 16 * mba_inc;

    while (1) {
	macroblock_modes =
	    get_macroblock_modes (picture->picture_coding_type,
				  picture->frame_pred_frame_dct);

	if (macroblock_modes & MACROBLOCK_QUANT)
	    slice.quantizer_scale =
		get_quantizer_scale (picture->q_scale_type);

	if (macroblock_modes & MACROBLOCK_INTRA) {

	    int DCT_offset, DCT_stride;

	    if (picture->concealment_motion_vectors)
		motion_conceal (&slice.f_motion);
	    else {
		slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
		slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;
		slice.b_motion.pmv[0][0] = slice.b_motion.pmv[0][1] = 0;
		slice.b_motion.pmv[1][0] = slice.b_motion.pmv[1][1] = 0;
	    }

	    if (macroblock_modes & DCT_TYPE_INTERLACED) {
		DCT_offset = width;
		DCT_stride = width * 2;
	    } else {
		DCT_offset = width * 8;
		DCT_stride = width;
	    }

	    // Decode lum blocks
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset, DCT_stride);
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset + 8, DCT_stride);
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset + DCT_offset, DCT_stride);
	    slice_intra_DCT (picture, &slice, 0,
			     dest[0] + offset + DCT_offset + 8, DCT_stride);

	    // Decode chroma blocks
	    slice_intra_DCT (picture, &slice, 1,
			     dest[1] + (offset>>1), width>>1);
	    slice_intra_DCT (picture, &slice, 2,
			     dest[2] + (offset>>1), width>>1);

	} else {

	    switch (macroblock_modes & MOTION_TYPE_MASK) {
	    case MC_FRAME:
		MOTION (motion_frame, macroblock_modes, slice, dest,
			offset, width);
		break;

	    case MC_FIELD:
		MOTION (motion_field, macroblock_modes, slice, dest,
			offset, width);
		break;

	    case 0:
		// non-intra mb without forward mv in a P picture
		slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
		slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;

		MOTION (motion_zero, MACROBLOCK_MOTION_FORWARD,
			slice, dest, offset, width);
		break;
	    }

	    //6.3.17.4 Coded block pattern
	    if (macroblock_modes & MACROBLOCK_PATTERN) {
		int coded_block_pattern;
		int DCT_offset, DCT_stride;

		if (macroblock_modes & DCT_TYPE_INTERLACED) {
		    DCT_offset = width;
		    DCT_stride = width * 2;
		} else {
		    DCT_offset = width * 8;
		    DCT_stride = width;
		}

		coded_block_pattern = Get_coded_block_pattern ();

		// Decode lum blocks

		if (coded_block_pattern & 0x20)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset, DCT_stride);
		if (coded_block_pattern & 0x10)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset + 8, DCT_stride);
		if (coded_block_pattern & 0x08)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset + DCT_offset,
					 DCT_stride);
		if (coded_block_pattern & 0x04)
		    slice_non_intra_DCT (picture, &slice,
					 dest[0] + offset + DCT_offset + 8,
					 DCT_stride);

		// Decode chroma blocks

		if (coded_block_pattern & 0x2)
		    slice_non_intra_DCT (picture, &slice,
					 dest[1] + (offset>>1), width >> 1);
		if (coded_block_pattern & 0x1)
		    slice_non_intra_DCT (picture, &slice,
					 dest[2] + (offset>>1), width >> 1);
	    }

	    slice.dc_dct_pred[0]=slice.dc_dct_pred[1]=slice.dc_dct_pred[2]=
		1 << (picture->intra_dc_precision + 7);
	}

	mba++;
	offset += 16;

	if (! bitstream_show (11))
	    break;

	mba_inc = Get_macroblock_address_increment () - 1;

	if (mba_inc) {
	    //reset intra dc predictor on skipped block
	    slice.dc_dct_pred[0]=slice.dc_dct_pred[1]=slice.dc_dct_pred[2]=
		1<< (picture->intra_dc_precision + 7);

	    //handling of skipped mb's differs between P_TYPE and B_TYPE
	    //pictures
	    if (picture->picture_coding_type == P_TYPE) {
		slice.f_motion.pmv[0][0] = slice.f_motion.pmv[0][1] = 0;
		slice.f_motion.pmv[1][0] = slice.f_motion.pmv[1][1] = 0;

		do {
		    MOTION (motion_zero, MACROBLOCK_MOTION_FORWARD,
			    slice, dest, offset, width);

		    mba++;
		    offset += 16;
		} while (--mba_inc);
	    } else {
		do {
		    MOTION (motion_reuse, macroblock_modes,
			    slice, dest, offset, width);

		    mba++;
		    offset += 16;
		} while (--mba_inc);
	    }
	}
    }

    return (mba >= picture->last_mba);
}
