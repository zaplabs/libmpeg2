/*
 *  mpeg2_internal.h
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


#include <inttypes.h>

//header start codes
#define PICTURE_START_CODE      0x00
#define SLICE_START_CODE_MIN    0x01
#define SLICE_START_CODE_MAX    0xAF
#define USER_DATA_START_CODE    0xB2
#define SEQUENCE_HEADER_CODE    0xB3
#define SEQUENCE_ERROR_CODE     0xB4
#define EXTENSION_START_CODE    0xB5
#define SEQUENCE_END_CODE       0xB7
#define GROUP_START_CODE        0xB8
#define SYSTEM_START_CODE_MIN   0xB9
#define SYSTEM_START_CODE_MAX   0xFF

#define ISO_END_CODE            0xB9
#define PACK_START_CODE         0xBA
#define SYSTEM_START_CODE       0xBB

#define VIDEO_ELEMENTARY_STREAM 0xe0

// macroblock type 
#define MACROBLOCK_INTRA                        1
#define MACROBLOCK_PATTERN                      2
#define MACROBLOCK_MOTION_BACKWARD              4
#define MACROBLOCK_MOTION_FORWARD               8
#define MACROBLOCK_QUANT                        16
#define SPATIAL_TEMPORAL_WEIGHT_CODE_FLAG       32
#define PERMITTED_SPATIAL_TEMPORAL_WEIGHT_CLASS 64

//picture structure 
#define TOP_FIELD     1
#define BOTTOM_FIELD  2
#define FRAME_PICTURE 3

//picture coding type 
#define I_TYPE 1
#define P_TYPE 2
#define B_TYPE 3
#define D_TYPE 4

// motion_type 
#define MC_FIELD 1
#define MC_FRAME 2
#define MC_16X8  2
#define MC_DMV   3

// mv_format 
#define MV_FIELD 0
#define MV_FRAME 1

//use gcc attribs to align critical data structures
#define ALIGN_16_BYTE __attribute__ ((aligned (16)))

//The picture struct contains all of the top level state
//information (ie everything except slice and macroblock
//state)
typedef struct picture_s
{
	//-- sequence header stuff --
	uint8_t *intra_quantizer_matrix;
	uint8_t *non_intra_quantizer_matrix;

	uint8_t custom_intra_quantization_matrix[64];
	uint8_t custom_non_intra_quantization_matrix[64];

	//--sequence extension stuff--
	//a lot of the stuff in the sequence extension stuff we dont' care
	//about, so it's not in here
	
	//color format of the sequence (4:2:0, 4:2:2, or 4:4:4)
	uint16_t chroma_format;

	//-- sequence display extension stuff --
	uint16_t video_format;
	uint16_t display_horizontal_size;
	uint16_t display_vertical_size;
  uint16_t color_description;
  uint16_t color_primaries;
  uint16_t transfer_characteristics;
  uint16_t matrix_coefficients;
	
	//-- group of pictures header stuff --
	
	//these are all part of the time code for this gop
	uint32_t drop_flag;
	uint32_t hour;
	uint32_t minute;
	uint32_t sec;
	uint32_t frame;

	//set if the B frames in this gop don't reference outside of the gop
	uint32_t closed_gop;
	//set if the previous I frame is missing (ie don't decode)
	uint32_t broken_link;

	//-- picture header stuff --
	//what type of picture this is (I,P,or B) D from MPEG-1 isn't supported
	uint32_t picture_coding_type;
	
	//-- picture coding extension stuff --
	
	//quantization factor for motion vectors
	uint8_t f_code[2][2];
	//quantization factor for intra dc coefficients
	uint16_t intra_dc_precision;
	//bool to indicate all predictions are frame based
	uint16_t frame_pred_frame_dct;
	//bool to indicate whether intra blocks have motion vectors 
	//(for concealment)
	uint16_t concealment_motion_vectors;
	//bit to indicate which quantization table to use
	uint16_t q_scale_type;
	//bool to use different vlc tables
	uint16_t intra_vlc_format;

	//last macroblock in the picture
	uint32_t last_mba;
	//width of picture in macroblocks
	uint32_t mb_width;

	//stuff derived from bitstream

	//pointer to the zigzag scan we're supposed to be using
	const uint8_t *scan;

	//Pointer to the current planar frame buffer (Y,Cr,CB)
	uint8_t *current_frame[3];
	//storage for reference frames plus a b-frame
	uint8_t *forward_reference_frame[3];
	uint8_t *backward_reference_frame[3];
	uint8_t *throwaway_frame[3];

	//these things are not needed by the decoder
	//NOTICE : this is a temporary interface, we will build a better one later.
	uint16_t frame_rate_code;
	uint16_t progressive_sequence;
	uint16_t top_field_first;
	uint16_t repeat_first_field;
	uint16_t progressive_frame;


	uint32_t coded_picture_width;
	uint32_t coded_picture_height;
} picture_t;

// state that is carried from one macroblock to the next inside of a same slice
typedef struct slice_s
{
	//Motion vectors
	//The f_ and b_ correspond to the forward and backward motion
	//predictors
	int16_t f_pmv[2][2];
	int16_t b_pmv[2][2];

	// predictor for DC coefficients in intra blocks
	int16_t dc_dct_pred[3];

	uint16_t quantizer_scale;
} slice_t;

typedef struct macroblock_s
{
	int16_t blocks [6*64];

	uint16_t mba;
	uint16_t macroblock_type;

	//Motion vector stuff
	//The f_ and b_ correspond to the forward and backward motion
	//predictors
	uint16_t motion_type;
	uint16_t motion_vector_count;
	int16_t b_motion_vectors[2][2];
	int16_t f_motion_vectors[2][2];
	int16_t f_motion_vertical_field_select[2];
	int16_t b_motion_vertical_field_select[2];
	int16_t dmvector[2];
	uint16_t mv_format;
	uint16_t mvscale;

	uint16_t dmv;
	uint16_t dct_type;
	uint16_t coded_block_pattern; 
	uint16_t quantizer_scale_code;

	uint16_t skipped;
} macroblock_t;

//The only global variable,
//the config struct
extern mpeg2_config_t config;




//FIXME remove
int Get_Luma_DC_dct_diff(void);
int Get_Chroma_DC_dct_diff(void);
int Get_macroblock_type(int picture_coding_type);
int Get_motion_code(void);
int Get_dmvector(void);
int Get_coded_block_pattern(void);
int Get_macroblock_address_increment(void);