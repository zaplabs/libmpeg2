/* 
 * bitstream.h
 *
 *	Copyright (C) Aaron Holtzman - Dec 1999
 *
 * This file is part of mpeg2dec, a free MPEG-2 video decoder
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
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

// common variables - they are shared between getvlc.c and slice.c
// they should be put elsewhere !
uint32_t bitstream_buffer;
int bitstream_avail_bits;
uint8_t * bitstream_ptr;

static inline uint32_t getword (void)
{
    uint32_t value;

    value = (bitstream_ptr[0] << 8) | bitstream_ptr[1];
    bitstream_ptr += 2;
    return value;
}

static inline void bitstream_init (uint8_t * start)
{
    bitstream_ptr = start;
    bitstream_avail_bits = 0;
    bitstream_buffer = getword () << 16;
}

static inline void needbits (void)
{
    if (bitstream_avail_bits > 0) {
	bitstream_buffer |= getword () << bitstream_avail_bits;
	bitstream_avail_bits -= 16;
    }
}

static inline void dumpbits (int num_bits)
{
    bitstream_buffer <<= num_bits;
    bitstream_avail_bits += num_bits;
}

static inline uint32_t bitstream_show (void)
{
    return bitstream_buffer;
}

static inline uint32_t bitstream_get (int num_bits)
{
    uint32_t result;

    //needbits ();
    result = bitstream_buffer >> (32 - num_bits);
    dumpbits (num_bits);

    return result;
}

static inline void bitstream_flush (int num_bits)
{
    dumpbits (num_bits);
}

// make sure that there are at least 16 valid bits in bit_buf
#define NEEDBITS(bit_buf,bits)		\
do {					\
    if (bits > 0) {			\
        bit_buf |= getword () << bits;	\
        bits -= 16;			\
    }					\
} while (0)

// remove num valid bits from bit_buf
#define DUMPBITS(bit_buf,bits,num)	\
do {					\
    bit_buf <<= (num);			\
    bits += (num);			\
} while (0)

// take num bits from the high part of bit_buf and zero extend them
#define UBITS(bit_buf,num) (((uint32_t)(bit_buf)) >> (32 - (num)))

// take num bits from the high part of bit_buf and sign extend them
#define SBITS(bit_buf,num) (((int32_t)(bit_buf)) >> (32 - (num)))
