/*
 * dump_state.c
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

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "mpeg2.h"

static struct {
    const mpeg2_fbuf_t * ptr;
    mpeg2_fbuf_t value;
} buf_code_list[26];
static int buf_code_index = 0;
static int buf_code_new = -1;
static struct {
    const mpeg2_picture_t * ptr;
    mpeg2_picture_t value;
} pic_code_list[26];
static int pic_code_index = 0;
static int pic_code_new = -1;

static char buf_code (const mpeg2_fbuf_t * fbuf)
{
    int i;

    if (fbuf == NULL)
	return '0';
    for (i = 0; i < 26; i++)
	if (buf_code_list[i].ptr == fbuf &&
	    !memcmp (fbuf, &buf_code_list[i].value, sizeof (mpeg2_fbuf_t)))
	    return ((i == buf_code_new) ? 'A' : 'a') + i;
    return '?';
}

static void buf_code_add (const mpeg2_fbuf_t * fbuf, FILE * f)
{
    int i;

    if (fbuf == NULL)
	return;
    for (i = 0; i < 26; i++)
	if (buf_code_list[i].ptr == fbuf)
	    fprintf (f, "buf_code_add error\n");
    buf_code_new = buf_code_index;
    buf_code_list[buf_code_index].ptr = fbuf;
    buf_code_list[buf_code_index].value = *fbuf;
    if (++buf_code_index == 26)
	buf_code_index = 0;
}

static void buf_code_del (const mpeg2_fbuf_t * fbuf)
{
    int i;

    if (fbuf == NULL)
	return;
    for (i = 0; i < 26; i++)
	if (buf_code_list[i].ptr == fbuf &&
	    !memcmp (fbuf, &buf_code_list[i].value, sizeof (mpeg2_fbuf_t))) {
	    buf_code_list[i].ptr = NULL;
	    return;
	}
}

static char pic_code (const mpeg2_picture_t * pic)
{
    int i;

    if (pic == NULL)
	return '0';
    for (i = 0; i < 26; i++)
	if (pic_code_list[i].ptr == pic &&
	    !memcmp (pic, &pic_code_list[i].value, sizeof (mpeg2_picture_t)))
	    return ((i == pic_code_new) ? 'A' : 'a') + i;
    return '?';
}

static void pic_code_add (const mpeg2_picture_t * pic, FILE * f)
{
    int i;

    if (pic == NULL)
	return;
    for (i = 0; i < 26; i++)
	if (pic_code_list[i].ptr == pic)
	    fprintf (f, "pic_code_add error\n");
    pic_code_new = pic_code_index;
    pic_code_list[pic_code_index].ptr = pic;
    pic_code_list[pic_code_index].value = *pic;
    if (++pic_code_index == 26)
	pic_code_index = 0;
}

static void pic_code_del (const mpeg2_picture_t * pic)
{
    int i;

    if (pic == NULL)
	return;
    for (i = 0; i < 26; i++)
	if (pic_code_list[i].ptr == pic &&
	    !memcmp (pic, &pic_code_list[i].value, sizeof (mpeg2_picture_t))) {
	    pic_code_list[i].ptr = NULL;
	    return;
	}
}

void dump_state (FILE * f, mpeg2_state_t state, const mpeg2_info_t * info,
		 int offset, int verbose)
{
    static char * state_name[] = {
	"BUFFER", "SEQUENCE", "SEQUENCE_REPEATED", "GOP",
	"PICTURE", "SLICE_1ST", "PICTURE_2ND", "SLICE", "END", "INVALID"
    };
    static char * profile[] = { "HP", "Spatial", "SNR", "MP", "SP" };
    static char * level[] = { "HL", "H-14", "ML", "LL" };
    static char * profile2[] = { "422@HL", NULL, NULL, "422@ML",
				 NULL, NULL, NULL, NULL, "MV@HL",
				 "MV@H-14", NULL, "MV@ML", "MV@LL" };
    static char * video_fmt[] = { "COMPONENT", "PAL", "NTSC", "SECAM", "MAC"};
    static char coding_type[] = { '0', 'I', 'P', 'B', 'D', '5', '6', '7'};
    const mpeg2_sequence_t * seq = info->sequence;
    const mpeg2_gop_t * gop = info->gop;
    const mpeg2_picture_t * pic;
    int i;
    int nb_pos;

    fprintf (f, "%8x", offset);
    if (verbose > 1) {
	if (state == STATE_PICTURE) {
	    buf_code_add (info->current_fbuf, f);
	    pic_code_add (info->current_picture, f);
	} else if (state == STATE_PICTURE_2ND)
	    pic_code_add (info->current_picture_2nd, f);
	fprintf (f, " %c%c %c%c%c %c%c%c %c", seq ? 'S' : 's', gop ? 'G' : 'g',
		 buf_code (info->current_fbuf),
		 pic_code (info->current_picture),
		 pic_code (info->current_picture_2nd),
		 buf_code (info->display_fbuf),
		 pic_code (info->display_picture),
		 pic_code (info->display_picture_2nd),
		 buf_code (info->discard_fbuf));
	if (state == STATE_SLICE) {
	    buf_code_del (info->discard_fbuf);
	    pic_code_del (info->display_picture);
	    pic_code_del (info->display_picture_2nd);
	}
	buf_code_new = pic_code_new = -1;
    }
    fprintf (f, " %s", state_name[state]);
    switch (state) {
    case STATE_SEQUENCE:
    case STATE_SEQUENCE_REPEATED:
	if (seq->flags & SEQ_FLAG_MPEG2)
	    fprintf (f, " MPEG2");
	if (0x10 <= seq->profile_level_id && seq->profile_level_id < 0x60 &&
	    !(seq->profile_level_id & 1) &&
	    4 <= (seq->profile_level_id & 15) &&
	    (seq->profile_level_id & 15) <= 10)
	    fprintf (f, " %s@%s",
		     profile[(seq->profile_level_id >> 4) - 1],
		     level[((seq->profile_level_id & 15) - 4) >> 1]);
	else if (0x82 <= seq->profile_level_id &&
		 seq->profile_level_id<= 0x8e &&
		 profile2[seq->profile_level_id - 0x82])
	    fprintf (f, " %s", profile2[seq->profile_level_id - 0x82]);
	else if (seq->flags & SEQ_FLAG_MPEG2)
	    fprintf (f, " profile %02x", seq->profile_level_id);
	if (seq->flags & SEQ_FLAG_CONSTRAINED_PARAMETERS)
	    fprintf (f, " CONST");
	if (seq->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE)
	    fprintf (f, " PROG");
	if (seq->flags & SEQ_FLAG_LOW_DELAY)
	    fprintf (f, " LOWDELAY");
	if ((seq->flags & SEQ_MASK_VIDEO_FORMAT) <
	    SEQ_VIDEO_FORMAT_UNSPECIFIED)
	    fprintf (f, " %s", video_fmt[(seq->flags & SEQ_MASK_VIDEO_FORMAT) /
					 SEQ_VIDEO_FORMAT_PAL]);
	if (seq->flags & SEQ_FLAG_COLOUR_DESCRIPTION)
	    fprintf (f, " COLORS (prim %d transfer %d matrix %d)",
		     seq->colour_primaries, seq->transfer_characteristics,
		     seq->matrix_coefficients);
	fprintf (f, " %dx%d chroma %dx%d fps %.*f maxBps %d vbv %d "
		 "picture %dx%d display %dx%d pixel %dx%d\n",
		 seq->width, seq->height,
		 seq->chroma_width, seq->chroma_height,
		 27000000%seq->frame_period?2:0, 27000000.0/seq->frame_period,
		 seq->byte_rate, seq->vbv_buffer_size,
		 seq->picture_width, seq->picture_height,
		 seq->display_width, seq->display_height,
		 seq->pixel_width, seq->pixel_height);
	break;
    case STATE_GOP:
	if (gop->flags & GOP_FLAG_DROP_FRAME)
	    fprintf (f, " DROP");
	if (gop->flags & GOP_FLAG_CLOSED_GOP)
	    fprintf (f, " CLOSED");
	if (gop->flags & GOP_FLAG_BROKEN_LINK)
	    fprintf (f, " BROKEN");
	fprintf (f, " %2d:%2d:%2d:%2d\n",
		 gop->hours, gop->minutes, gop->seconds, gop->pictures);
	break;
    case STATE_PICTURE:
	pic = info->current_picture;
	goto show_pic;
    case STATE_PICTURE_2ND:
	pic = info->current_picture_2nd;
	goto show_pic;
    case STATE_SLICE:
	pic = info->display_picture_2nd;
	if (pic)
	    goto show_pic;
    case STATE_SLICE_1ST:
	pic = info->display_picture;
	if (!pic)
	    goto done_pic;
    show_pic:
	fprintf (f, " %c",
		 coding_type[pic->flags & PIC_MASK_CODING_TYPE]);
	if (pic->flags & PIC_FLAG_PROGRESSIVE_FRAME)
	    fprintf (f, " PROG");
	if (pic->flags & PIC_FLAG_SKIP)
	    fprintf (f, " SKIP");
	fprintf (f, " fields %d", pic->nb_fields);
	if (pic->flags & PIC_FLAG_TOP_FIELD_FIRST)
	    fprintf (f, " TFF");
	if (pic->flags & PIC_FLAG_PTS)
	    fprintf (f, " pts %08x", pic->pts);
	fprintf (f, " time_ref %d", pic->temporal_reference);
	if (pic->flags & PIC_FLAG_COMPOSITE_DISPLAY)
	    fprintf (f, " composite %05x", pic->flags >> 12);
	fprintf (f, " offset");
	nb_pos = pic->nb_fields;
	if (seq->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE)
	    nb_pos >>= 1;
	for (i = 0; i < nb_pos; i++)
	    fprintf (f, " %d/%d",
		     pic->display_offset[i].x, pic->display_offset[i].y);
    done_pic:
	fprintf (f, "\n");
	break;
    default:
	fprintf (f, "\n");
    }
}
