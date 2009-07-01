
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

static inline half f2h( float value ) {
    union { float f; uint32_t i; } u = { .f = value };

    return f2h_baseshifttable[(u.i >> 23) & 0x1FF].base + ((u.i & 0x007FFFFF) >> f2h_baseshifttable[(u.i >> 23) & 0x1FF].shift);
}

// Naive implementations
static void n_convert_h2f( const half *in, float *out, int count ) {
    while( count-- )
        *out++ = h2f( *in++ );
}

static void n_convert_f2h( const float *in, half *out, int count ) {
    while( count-- )
        *out++ = f2h( *in++ );
}

static void n_half_lookup( const half *table, const half *in, half *out, int count ) {
    while( count-- )
        *out++ = table[*in++];
}

void (*convert_h2f)( const half *, float *, int );
void (*convert_f2h)( const float *, half *, int );
void (*half_lookup)( const half *, const half *, half *, int );

void init_half( void *m ) {
    convert_h2f = n_convert_h2f;
    convert_f2h = n_convert_f2h;
    half_lookup = n_half_lookup;
}


