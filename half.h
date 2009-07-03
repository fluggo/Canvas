
#include <stdint.h>

#if !defined(fluggo_half)
#define fluggo_half

typedef uint16_t half;

extern void (*half_convert_from_float)( const float *in, half *out, int count );
extern void (*half_convert_to_float)( const half *in, float *out, int count );
extern void (*half_convert_from_float_fast)( const float *in, half *out, int count );
extern void (*half_convert_to_float_fast)( const half *in, float *out, int count );
extern void (*half_lookup)( const half *table, const half *in, half *out, int count );

#endif
