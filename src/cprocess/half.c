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
typedef uint16_t half;

// Based on code by Mr. Jeroen van der Zijp

extern struct { uint16_t base; uint8_t shift; } f2h_baseshifttable[];

extern uint32_t h2f_mantissatable[];
extern struct { uint16_t exponent, offset; } h2f_offsetexponenttable[];

static inline float h2f( half value ) {
    union { float f; uint32_t i; } u;

    u.i = h2f_mantissatable[h2f_offsetexponenttable[value >> 10].offset + (value & 0x3FF)] + (h2f_offsetexponenttable[value >> 10].exponent << 16);

    return u.f;
}

static inline float h2f_fast( half value ) {
    union { float f; uint32_t i; } u;

    u.i = ((value & 0x8000) << 16) | (((value & 0x7c00) + 0x1C000) << 13) | ((value & 0x03FF) << 13);

    return u.f;
}

static inline half f2h( float value ) {
    union { float f; uint32_t i; } u = { .f = value };

    return f2h_baseshifttable[(u.i >> 23) & 0x1FF].base + ((u.i & 0x007FFFFF) >> f2h_baseshifttable[(u.i >> 23) & 0x1FF].shift);
}

static inline half f2h_fast( float value ) {
    union { float f; uint32_t i; } u = { .f = value };

    return ((u.i >> 16) & 0x8000) |
        ((((u.i & 0x7f800000) - 0x38000000) >> 13) & 0x7c00) |
        ((u.i >> 13) & 0x03ff);
}

// Naive implementations
static void n_convert_h2f( float *out, const half *in, int count ) {
    while( count-- )
        *out++ = h2f( *in++ );
}

static void n_convert_f2h( half *out, const float *in, int count ) {
    while( count-- )
        *out++ = f2h( *in++ );
}

static void n_convert_h2f_fast( float *out, const half *in, int count ) {
    while( count-- )
        *out++ = h2f_fast( *in++ );
}

static void n_convert_f2h_fast( half *out, const float *in, int count ) {
    while( count-- )
        *out++ = f2h_fast( *in++ );
}

static void n_half_lookup( const half *table, half *out, const half *in, int count ) {
    while( count-- )
        *out++ = table[*in++];
}

#if defined(WINNT)
#define EXPORT __attribute__((dllexport))
#else
#define EXPORT __attribute__((visibility("default")))
#endif

EXPORT void (*half_convert_to_float)( float *, const half *, int );
EXPORT void (*half_convert_from_float)( half *, const float *, int );
EXPORT void (*half_convert_to_float_fast)( float *, const half *, int );
EXPORT void (*half_convert_from_float_fast)( half *, const float *, int );
EXPORT void (*half_lookup)( const half *, half *, const half *, int );

EXPORT void init_half() {
    half_convert_to_float = n_convert_h2f;
    half_convert_from_float = n_convert_f2h;
    half_convert_to_float_fast = n_convert_h2f_fast;
    half_convert_from_float_fast = n_convert_f2h_fast;
    half_lookup = n_half_lookup;
}


