
#include <stdint.h>

#if !defined(fluggo_half)
#define fluggo_half

// Based on code by Mr. Jeroen van der Zijp

typedef uint16_t half;

extern struct { uint16_t base; uint8_t shift; } f2h_baseshifttable[];

extern uint32_t h2f_mantissatable[];
extern struct { uint16_t exponent, offset; } h2f_offsetexponenttable[];

inline float h2f( half value ) {
    union { float f; uint32_t i; } u;

    u.i = h2f_mantissatable[h2f_offsetexponenttable[value >> 10].offset + (value & 0x3FF)] + (h2f_offsetexponenttable[value >> 10].exponent << 16);

    return u.f;
}

inline half f2h( float value ) {
    union { float f; uint32_t i; } u = { .f = value };

    return f2h_baseshifttable[(u.i >> 23) & 0x1FF].base + ((u.i & 0x007FFFFF) >> f2h_baseshifttable[(u.i >> 23) & 0x1FF].shift);
}

#endif
