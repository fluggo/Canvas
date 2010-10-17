/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009 Brian J. Crowell <brian@fluggo.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>

#if !defined(fluggo_half)
#define fluggo_half

typedef uint16_t half;
#define HALF_COUNT 65536

void init_half();

extern void (*half_convert_from_float)( half *out, const float *in, int count );
extern void (*half_convert_to_float)( float *out, const half *in, int count );
extern void (*half_convert_from_float_fast)( const float *in, half *out, int count );
extern void (*half_convert_to_float_fast)( const half *in, float *out, int count );
extern void (*half_lookup)( const half *table, const half *in, half *out, int count );

#endif
