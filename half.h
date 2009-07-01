
#include <stdint.h>

#if !defined(fluggo_half)
#define fluggo_half

typedef uint16_t half;

extern void (*convert_f2h)( const float *in, half *out, int count );
extern void (*convert_h2f)( const half *in, float *out, int count );
extern void (*half_lookup)( const half *table, const half *in, half *out, int count );

#endif
