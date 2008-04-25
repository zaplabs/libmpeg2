/*
 *  parse.c
 *
 *  Copyright (C) Aaron Holtzman <aholtzma@ess.engr.uvic.ca> - Nov 1999
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

#include <stddef.h>
#include <stdio.h>
#include "config.h"
#include "mpeg2.h"
#include "mpeg2_internal.h"

#include "bitstream.h"
#include "stats.h"
#include "parse.h"

//FIXME remove when new vlc stuff is in
#include "old_crap.h"

//FIXME remove when you put vlc_get_block_coeff into vlc.
typedef struct {
  char run, level, len;
} DCTtab;

extern DCTtab DCTtabfirst[],DCTtabnext[],DCTtab0[],DCTtab1[];
extern DCTtab DCTtab2[],DCTtab3[],DCTtab4[],DCTtab5[],DCTtab6[];
extern DCTtab DCTtab0a[],DCTtab1a[];
//FIXME remove when you put vlc_get_block_coeff into vlc.



uint_32 non_linear_quantizer_scale[32] =
{
   0, 1, 2, 3, 4, 5, 6, 7,
   8,10,12,14,16,18,20,22,
  24,28,32,36,40,44,48,52,
  56,64,72,80,88,96,104,112
};

static uint_8 scan_norm_mmx[64] =
{ 
	// MMX Zig-Zag scan pattern (transposed)  
	 0, 8, 1, 2, 9,16,24,17,10, 3, 4,11,18,25,32,40,
	33,26,19,12, 5, 6,13,20,27,34,41,48,56,49,42,35,
	28,21,14, 7,15,22,29,36,43,50,57,58,51,44,37,30,
	23,31,38,45,52,59,60,53,46,39,47,54,61,62,55,63
};

static uint_8 scan_alt_mmx[64] = 
{ 
	// Alternate scan pattern (transposed)
	 0, 1, 2, 3, 8, 9,16,17,10,11, 4, 5, 6, 7,15,14,
	13,12,19,18,24,25,32,33,26,27,20,21,22,23,28,29,
	30,31,34,35,40,41,48,49,42,43,36,37,38,39,44,45,
	46,47,50,51,56,57,58,59,52,53,54,55,60,61,62,63,
};

static uint_8 scan_norm[64] =
{ 
	// Zig-Zag scan pattern
	0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,
	12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
	35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,
	58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63
};

static uint_8 scan_alt[64] =
{ 
	// Alternate scan pattern 
	0,8,16,24,1,9,2,10,17,25,32,40,48,56,57,49,
	41,33,26,18,3,11,4,12,19,27,34,42,50,58,35,43,
	51,59,20,28,5,13,6,14,21,29,36,44,52,60,37,45,
	53,61,22,30,7,15,23,31,38,46,54,62,39,47,55,63
};

static uint_8 default_intra_quantization_matrix[64] = 
{
   8, 16, 19, 22, 26, 27, 29, 34,
  16, 16, 22, 24, 27, 29, 34, 37,
  19, 22, 26, 27, 29, 34, 34, 38,
  22, 22, 26, 27, 29, 34, 37, 40,
  22, 26, 27, 29, 32, 35, 40, 48,
  26, 27, 29, 32, 35, 40, 48, 58,
  26, 27, 29, 34, 38, 46, 56, 69,
  27, 29, 35, 38, 46, 56, 69, 83
};

static uint_8 default_non_intra_quantization_matrix[64] = 
{
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16, 
	16, 16, 16, 16, 16, 16, 16, 16 
};

static void parse_sequence_display_extension(picture_t *picture);
static void parse_sequence_extension(picture_t *picture);
static void parse_picture_coding_extension(picture_t *picture);

void
parse_state_init(picture_t *picture)
{
	picture->intra_quantizer_matrix = default_intra_quantization_matrix;
	picture->non_intra_quantizer_matrix = default_non_intra_quantization_matrix;
	//FIXME we should set pointers to the real scan matrices
	//here (mmx vs normal) instead of the ifdefs in parse_picture_coding_extension
	picture->scan = scan_norm;
}

//FIXME remove once we get everything working
void parse_marker_bit(char *string)
{
  int marker;

  marker = Get_Bits(1);

  if(!marker)
    fprintf(stderr,"(parse) %s marker_bit set to 0\n",string);
}


void 
parse_sequence_header(picture_t *picture)
{
	uint_32 i;

  picture->horizontal_size             = Get_Bits(12);
  picture->vertical_size               = Get_Bits(12);

	//XXX this needs field fixups
	picture->coded_picture_height = ((picture->vertical_size + 15)/16) * 16;
	picture->coded_picture_width  = ((picture->horizontal_size   + 15)/16) * 16;

  picture->aspect_ratio_information    = Get_Bits(4);
  picture->frame_rate_code             = Get_Bits(4);
  picture->bit_rate_value              = Get_Bits(18);
  parse_marker_bit("sequence_header");
  picture->vbv_buffer_size             = Get_Bits(10);
  picture->constrained_parameters_flag = Get_Bits(1);

  if((picture->use_custom_intra_quantizer_matrix = Get_Bits(1)))
  {
		picture->intra_quantizer_matrix = picture->custom_intra_quantization_matrix;
    for (i=0; i < 64; i++)
      picture->custom_intra_quantization_matrix[scan_norm[i]] = Get_Bits(8);
  }
	else
		picture->intra_quantizer_matrix = default_intra_quantization_matrix;

  if((picture->use_custom_non_intra_quantizer_matrix = Get_Bits(1)))
  {
		picture->non_intra_quantizer_matrix = picture->custom_non_intra_quantization_matrix;
    for (i=0; i < 64; i++)
      picture->custom_non_intra_quantization_matrix[scan_norm[i]] = Get_Bits(8);
  }
	else
		picture->non_intra_quantizer_matrix = default_non_intra_quantization_matrix;

	stats_sequence_header(picture);

}

void
parse_extension(picture_t *picture)
{
	uint_32 code;

	code = Get_Bits(4);
		
	switch(code)
	{
		case PICTURE_CODING_EXTENSION_ID:
			parse_picture_coding_extension(picture);
			break;

		case SEQUENCE_EXTENSION_ID:
			parse_sequence_extension(picture);
			break;

		case SEQUENCE_DISPLAY_EXTENSION_ID:
			parse_sequence_display_extension(picture);
			break;

		default:
			fprintf(stderr,"(parse) unsupported extension %d\n",code);
			exit(1);
	}
}

void
parse_user_data(void)
{
  while (Show_Bits(24)!=0x01L)
    Flush_Buffer(8);
}

static void
parse_sequence_extension(picture_t *picture)
{

  /*picture->profile_and_level_indication = */ Get_Bits(8);
  picture->progressive_sequence           =    Get_Bits(1);
  picture->chroma_format                  =    Get_Bits(2);
  /*picture->horizontal_size_extension    = */ Get_Bits(2);
  /*picture->vertical_size_extension      = */ Get_Bits(2);
  /*picture->bit_rate_extension           = */ Get_Bits(12);
  parse_marker_bit("sequence_extension");
  /*picture->vbv_buffer_size_extension    = */ Get_Bits(8);
  /*picture->low_delay                    = */ Get_Bits(1);
  /*picture->frame_rate_extension_n       = */ Get_Bits(2);
  /*picture->frame_rate_extension_d       = */ Get_Bits(5);

	stats_sequence_ext(picture);
	//
	//XXX since we don't support anything but 4:2:0, die gracefully
	if(picture->chroma_format != CHROMA_420)
	{
		fprintf(stderr,"(parse) sorry, mpeg2dec doesn't support color formats other than 4:2:0\n");
		exit(1);
	}
}


static void
parse_sequence_display_extension(picture_t *picture)
{
  picture->video_format      = Get_Bits(3);
  picture->color_description = Get_Bits(1);

  if (picture->color_description)
  {
    picture->color_primaries          = Get_Bits(8);
    picture->transfer_characteristics = Get_Bits(8);
    picture->matrix_coefficients      = Get_Bits(8);
  }

  picture->display_horizontal_size = Get_Bits(14);
  parse_marker_bit("sequence_display_extension");
  picture->display_vertical_size   = Get_Bits(14);

	stats_sequence_display_ext(picture);
}

void 
parse_gop_header(picture_t *picture)
{
  picture->drop_flag   = Get_Bits(1);
  picture->hour        = Get_Bits(5);
  picture->minute      = Get_Bits(6);
  parse_marker_bit("gop_header");
  picture->sec         = Get_Bits(6);
  picture->frame       = Get_Bits(6);
  picture->closed_gop  = Get_Bits(1);
  picture->broken_link = Get_Bits(1);

	stats_gop_header(picture);

}


static void 
parse_picture_coding_extension(picture_t *picture)
{
  picture->f_code[0][0] = Get_Bits(4);
  picture->f_code[0][1] = Get_Bits(4);
  picture->f_code[1][0] = Get_Bits(4);
  picture->f_code[1][1] = Get_Bits(4);

  picture->intra_dc_precision         = Get_Bits(2);
  picture->picture_structure          = Get_Bits(2);
  picture->top_field_first            = Get_Bits(1);
  picture->frame_pred_frame_dct       = Get_Bits(1);
  picture->concealment_motion_vectors = Get_Bits(1);
  picture->q_scale_type               = Get_Bits(1);
  picture->intra_vlc_format           = Get_Bits(1);
  picture->alternate_scan             = Get_Bits(1);

#ifdef __i386__
	if(picture->alternate_scan)
		picture->scan = scan_alt_mmx;
	else
		picture->scan = scan_norm_mmx;
#else
	if(picture->alternate_scan)
		picture->scan = scan_alt;
	else
		picture->scan = scan_norm;
#endif

  picture->repeat_first_field         = Get_Bits(1);
	/*chroma_420_type isn't used */       Get_Bits(1);
  picture->progressive_frame          = Get_Bits(1);
  picture->composite_display_flag     = Get_Bits(1);

  if (picture->composite_display_flag)
  {
		//This info is not used in the decoding process
    /* picture->v_axis            = */ Get_Bits(1);
    /* picture->field_sequence    = */ Get_Bits(3);
    /* picture->sub_carrier       = */ Get_Bits(1);
    /* picture->burst_amplitude   = */ Get_Bits(7);
    /* picture->sub_carrier_phase = */ Get_Bits(8);
  }

	//XXX die gracefully if we encounter a field picture based stream
	if(picture->picture_structure != FRAME_PICTURE)
	{
		fprintf(stderr,"(parse) sorry, mpeg2dec doesn't support field based pictures yet\n");
		exit(1);
	}

	stats_picture_coding_ext_header(picture);
}

void 
parse_picture_header(picture_t *picture)
{
  picture->temporal_reference  = Get_Bits(10);
  picture->picture_coding_type = Get_Bits(3);
  picture->vbv_delay           = Get_Bits(16);

  if (picture->picture_coding_type==P_TYPE || picture->picture_coding_type==B_TYPE)
  {
    picture->full_pel_forward_vector = Get_Bits(1);
    picture->forward_f_code = Get_Bits(3);
  }
  if (picture->picture_coding_type==B_TYPE)
  {
    picture->full_pel_backward_vector = Get_Bits(1);
    picture->backward_f_code = Get_Bits(3);
  }

	stats_picture_header(picture);
}


void
parse_slice_header(const picture_t *picture, slice_t *slice)
{

	uint_32 intra_slice_flag;

  slice->quantizer_scale_code = Get_Bits(5);

	if (picture->q_scale_type)
  	slice->quantizer_scale = non_linear_quantizer_scale[slice->quantizer_scale_code];
	else
  	slice->quantizer_scale = slice->quantizer_scale_code << 1 ;
		

  if ((intra_slice_flag = Get_Bits(1)))
  {
		//Ignore the value of intra_slice
    Get_Bits(1);

    slice->slice_picture_id_enable = Get_Bits(1);
		slice->slice_picture_id = Get_Bits(6);

		//Ignore all the extra_data
		while(Get_Bits1())
			Flush_Buffer(8);
	}

	//reset intra dc predictor
	slice->dc_dct_pred[0]=slice->dc_dct_pred[1]=slice->dc_dct_pred[2]= 
			1<<(picture->intra_dc_precision + 7) ;

	stats_slice_header(slice);
}


//This goes into vlc.c when it gets written
sint_32
vlc_get_block_coeff(uint_16 non_intra_dc,uint_16 intra_vlc_format)
{
  uint_32 code;
  DCTtab *tab;
	uint_16 run;
	sint_16 val;

	//this routines handles intra AC and non-intra AC/DC coefficients
	code = Show_Bits(16);
 
	if (code>=16384 && !intra_vlc_format)
	{
		if (non_intra_dc)
			tab = &DCTtabfirst[(code>>12)-4];
		else
			tab = &DCTtabnext[(code>>12)-4];
	}
	else if (code>=1024)
	{
		if (intra_vlc_format)
			tab = &DCTtab0a[(code>>8)-4];
		else
			tab = &DCTtab0[(code>>8)-4];
	}
	else if (code>=512)
	{
		if (intra_vlc_format)
			tab = &DCTtab1a[(code>>6)-8];
		else
			tab = &DCTtab1[(code>>6)-8];
	}
	else if (code>=256)
		tab = &DCTtab2[(code>>4)-16];
	else if (code>=128)
		tab = &DCTtab3[(code>>3)-16];
	else if (code>=64)
		tab = &DCTtab4[(code>>2)-16];
	else if (code>=32)
		tab = &DCTtab5[(code>>1)-16];
	else if (code>=16)
		tab = &DCTtab6[code-16];
	else
	{
		fprintf(stderr,"invalid Huffman code in vlc_get_block_coeff()\n");
		return 0;
	}

	Flush_Buffer(tab->len);

	if (tab->run==64) // end_of_block 
		return 0;

	if (tab->run==65) /* escape */
	{
		run = Get_Bits(6);

		val = Get_Bits(12);

		if ((val&2047)==0)
		{
			fprintf(stderr,"invalid escape in vlc_get_block_coeff()\n");
			return 0;
		}

		if(val >= 2048)
			val =  val - 4096;
	}
	else
	{
		run = tab->run;
		val = tab->level;
		 
		if(Get_Bits(1)) //sign bit
			val = -val;
	}

	return ((val << 16) + run);
}


void
parse_intra_block(const picture_t *picture,slice_t *slice,sint_16 *dest,uint_32 cc)
{
	sint_32 coeff;
	uint_32 i = 1;
	uint_32 j;
	uint_16 run;
	sint_16 val;
	uint_8 *scan = picture->scan;
	uint_8 *quant_matrix = picture->intra_quantizer_matrix;
	sint_16 quantizer_scale = slice->quantizer_scale;

	//Clear the entire block and bring it into the cache
	//XXX we should perhaps do this at the start of the frame
	memset(dest,0,sizeof(sint_16) * 64);

	//Get the intra DC coefficient and inverse quantize it
  if (cc == 0)
    dest[0] = (slice->dc_dct_pred[0] += Get_Luma_DC_dct_diff()) << (3 - picture->intra_dc_precision);
  else 
    dest[0] = (slice->dc_dct_pred[cc]+= Get_Chroma_DC_dct_diff()) << (3 - picture->intra_dc_precision);

	//FIXME convert the cross platform IDCT to use 11.4 fixed point
#ifdef __i386__
	dest[0] <<= 4;
#endif

	i = 1;
	while((coeff = vlc_get_block_coeff(0,picture->intra_vlc_format)))
	{
		val = coeff >> 16;	
		run = coeff & 0xffffL;
		
		i += run;
    j = scan[i++];

		//FIXME convert the cross platform IDCT to use 11.4 fixed point
#ifdef __i386__
    dest[j] = (val * quantizer_scale * quant_matrix[j]);
#else
    dest[j] = (val * quantizer_scale * quant_matrix[j]) >> 4;
#endif

#if 0
		sint_32 sum = 0;
		if(dest[j] > 2047)
			dest[j] = 2047;
		else if(dest[j] < -2048)
			dest[j] = -2048;

		sum += dest[j];
#endif
	}

#if 0
	if ((sum & 1) == 0)
		dest[63] ^= 1;
#endif

	if(i > 64)
	{
		fprintf(stderr,"Invalid DCT index in parse_intra_block()\n");
		exit(1);
	}
}
	
void
parse_non_intra_block(const picture_t *picture,slice_t *slice,sint_16 *dest,uint_32 cc)
{
	uint_32 coeff;
	uint_32 i = 0;
	uint_32 j;
	uint_16 run;
	sint_16 val;
	uint_8 *scan = picture->scan;
	uint_8 *quant_matrix = picture->intra_quantizer_matrix;
	sint_16 quantizer_scale = slice->quantizer_scale;

	//Clear the entire block and bring it into the cache
	memset(dest,0,sizeof(sint_16) * 64);

	while((coeff = vlc_get_block_coeff(i==0,0)))
	{
		val = coeff >> 16;	
		run = coeff & 0xffffL;
		
		i += run;
    j = scan[i++];
		//FIXME mmx
		//FIXME convert the cross platform IDCT to use 11.4 fixed point
#ifdef __i386__
    dest[j] = (((val<<1) + (val>>15))* quantizer_scale * quant_matrix[j]) >> 1;
#else
    dest[j] = (((val<<1) + (val>>15))* quantizer_scale * quant_matrix[j]) >> 5;
#endif
	}

	if(i > 64)
	{
		fprintf(stderr,"Invalid DCT index in parse_intra_block()\n");
		exit(1);
	}
}


void
parse_macroblock(const picture_t *picture,slice_t* slice, macroblock_t *mb)
{
  uint_32 quantizer_scale_code;
  uint_32 picture_structure = picture->picture_structure;
	uint_32 i;

	//Clear the skipped flag
	mb->skipped = 0;

  // get macroblock_type 
  mb->macroblock_type = Get_macroblock_type(picture->picture_coding_type);

  // get frame/field motion type 
  if (mb->macroblock_type & (MACROBLOCK_MOTION_FORWARD|MACROBLOCK_MOTION_BACKWARD))
  {
    if (picture_structure==FRAME_PICTURE) // frame_motion_type 
    {
      mb->motion_type = picture->frame_pred_frame_dct ? MC_FRAME : Get_Bits(2);
			if(!picture->frame_pred_frame_dct)
      {
        printf("frame_motion_type (");
        printf("): %s\n",mb->motion_type==MC_FIELD?"Field":
                         mb->motion_type==MC_FRAME?"Frame":
                         mb->motion_type==MC_DMV?"Dual_Prime":"Invalid");
			}
      
    }
    else // field_motion_type 
    {
      mb->motion_type = Get_Bits(2);
    }
  }
  else if ((mb->macroblock_type & MACROBLOCK_INTRA) && picture->concealment_motion_vectors)
  {
    // concealment motion vectors 
    mb->motion_type = (picture_structure==FRAME_PICTURE) ? MC_FRAME : MC_FIELD;
  }

  // derive motion_vector_count, mv_format and dmv, (table 6-17, 6-18)
  if (picture_structure==FRAME_PICTURE)
  {
    //mb->motion_vector_count = (motion_type==MC_FIELD && stwclass<2) ? 2 : 1;
    mb->motion_vector_count = 1;
    mb->mv_format = (mb->motion_type==MC_FRAME) ? MV_FRAME : MV_FIELD;
  }
  else
  {
    mb->motion_vector_count = (mb->motion_type==MC_16X8) ? 2 : 1;
    mb->mv_format = MV_FIELD;
  }

  mb->dmv = (mb->motion_type==MC_DMV); // dual prime

	//Set if we need to scale motion vector prediction by 1/2
  mb->mvscale = ((mb->mv_format==MV_FIELD) && (picture_structure==FRAME_PICTURE));

  // get dct_type (frame DCT / field DCT) 
	if( (picture_structure==FRAME_PICTURE) && (!picture->frame_pred_frame_dct) && 
			(mb->macroblock_type & (MACROBLOCK_PATTERN|MACROBLOCK_INTRA)) )
		mb->dct_type =  Get_Bits(1);
	else
		mb->dct_type =  0;


	if (mb->macroblock_type & MACROBLOCK_QUANT)
  {
    quantizer_scale_code = Get_Bits(5);

		//The quantizer scale code propogates up to the slice level
		if(picture->q_scale_type)
			slice->quantizer_scale = non_linear_quantizer_scale[quantizer_scale_code];
		else
      slice->quantizer_scale = quantizer_scale_code << 1;
  }
		

  // 6.3.17.2 Motion vectors 

  // decode forward motion vectors 
  if ((mb->macroblock_type & MACROBLOCK_MOTION_FORWARD) || 
			((mb->macroblock_type & MACROBLOCK_INTRA) && picture->concealment_motion_vectors))
  {
		//Field pictures are definately broken here
		//FIXME this could be faster too
      motion_vectors(slice->pmv,(int*)mb->dmvector,mb->motion_vertical_field_select,
        0,mb->motion_vector_count,mb->mv_format,0,0,mb->dmv,
				mb->mvscale);
  }

  // decode backward motion vectors 
  if (mb->macroblock_type & MACROBLOCK_MOTION_BACKWARD)
  {
      motion_vectors(slice->pmv,(int*)mb->dmvector,mb->motion_vertical_field_select,
        1,mb->motion_vector_count,mb->mv_format,0,0,mb->dmv,
        mb->mvscale);
  }

  if ((mb->macroblock_type & MACROBLOCK_INTRA) && picture->concealment_motion_vectors)
    Flush_Buffer(1); // remove marker_bit 

  // 6.3.17.4 Coded block pattern 
  if (mb->macroblock_type & MACROBLOCK_PATTERN)
  {
    mb->coded_block_pattern = Get_coded_block_pattern();

    if (picture->chroma_format==CHROMA_422)
    {
      // coded_block_pattern_1 
      mb->coded_block_pattern = (mb->coded_block_pattern<<2) | Get_Bits(2); 

		}
		else if (picture->chroma_format==CHROMA_444)
		{
			// coded_block_pattern_2 
			mb->coded_block_pattern = (mb->coded_block_pattern<<6) | Get_Bits(6); 
		}
	}
  else
    mb->coded_block_pattern = (mb->macroblock_type & MACROBLOCK_INTRA) ? 
      0x3f : 0;

	//FIXME remove
	//fprintf(stderr,"(mb) cbp %02x\n",mb->coded_block_pattern);

	//coded_block_pattern is set only if there are blocks in bitstream
  if(mb->coded_block_pattern)
	{
		//XXX only 4:2:0 is supported here - fix later
		
		// Decode lum blocks 
		for (i=0; i<4; i++)
		{
			if (mb->coded_block_pattern & (1<<(5-i)))
			{
				if (mb->macroblock_type & MACROBLOCK_INTRA)
					parse_intra_block(picture,slice,&mb->y_blocks[i*64],0);
				else
					parse_non_intra_block(picture,slice,&mb->y_blocks[i*64],0);
			}
		}
		
		// Decode chroma blocks 
		if (mb->coded_block_pattern & 0x2)
		{
			if (mb->macroblock_type & MACROBLOCK_INTRA)
				parse_intra_block(picture,slice,mb->cr_blocks,1);
			else
				parse_non_intra_block(picture,slice,mb->cr_blocks,1);
		}

		if (mb->coded_block_pattern & 0x1)
		{
			if (mb->macroblock_type & MACROBLOCK_INTRA)
				parse_intra_block(picture,slice,mb->cb_blocks,2);
			else
				parse_non_intra_block(picture,slice,mb->cb_blocks,2);
		}
	}

  // 7.2.1 DC coefficients in intra blocks 
  if (!(mb->macroblock_type & MACROBLOCK_INTRA))
	{
		//FIXME this looks suspicious...should be reset to 2^(intra_dc_precision+7)
		//
		//lets see if it works
		slice->dc_dct_pred[0]=slice->dc_dct_pred[1]=slice->dc_dct_pred[2]= 
			1<<(picture->intra_dc_precision + 7) ;
	}

  //7.6.3.4 Resetting motion vector predictors 
  if ((mb->macroblock_type & MACROBLOCK_INTRA) && !picture->concealment_motion_vectors)
		memset(slice->pmv,0,sizeof(uint_16) * 8);


  // special "No_MC" macroblock_type case 
  // 7.6.3.5 Prediction in P pictures 
  if ((picture->picture_coding_type==P_TYPE) 
    && !(mb->macroblock_type & (MACROBLOCK_MOTION_FORWARD|MACROBLOCK_INTRA)))
  {
    // non-intra mb without forward mv in a P picture 
    // 7.6.3.4 Resetting motion vector predictors 
		memset(slice->pmv,0,sizeof(uint_16) * 8);

    //6.3.17.1 Macroblock modes, frame_motion_type 
    //if (picture_structure==FRAME_PICTURE)
    //  *motion_type = MC_FRAME;
    //else
    //{
    //  *motion_type = MC_FIELD;
    //  /* predict from field of same parity */
    //  motion_vertical_field_select[0][0] = (picture_structure==BOTTOM_FIELD);
    //}
  }

  //if (*stwclass==4)
  //{
  //  /* purely spatially predicted macroblock */
  //  /* 7.7.5.1 Resetting motion vector predictions */
	//  slice_reset_pmv(slice);
  // }
}