
#include "framework.h"
#include "half.h"

uint8_t gamma45[65536];

static inline float gamma45Func( float input ) {
    return clampf( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

const uint8_t *
video_get_gamma45_ramp() {
    static bool __gamma_init = false;

    if( !__gamma_init ) {
        // Fill in the 0.45 gamma table
        half *h = g_malloc( sizeof(half) * 65536 );
        float *f = g_malloc( sizeof(float) * 65536 );

        for( int i = 0; i < 65536; i++ )
            h[i] = (half) i;

        half_convert_to_float( h, f, 65536 );
        g_free( h );

        for( int i = 0; i < 65536; i++ )
            gamma45[i] = (uint8_t) gamma45Func( f[i] );

        g_free( f );

        __gamma_init = true;
    }

    return gamma45;
}

