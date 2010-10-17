
#include "framework.h"
#include <math.h>
#include "half.h"

uint8_t *gamma45;

static inline G_GNUC_CONST float
gamma45Func( float input ) {
    return clampf( powf( input, 0.45f ) * 255.0f, 0.0f, 255.0f );
}

const uint8_t *
video_get_gamma45_ramp() {
    static gsize __gamma_init = 0;

    if( g_once_init_enter( &__gamma_init ) ) {
        // Fill in the 0.45 gamma table
        half *h = g_malloc( sizeof(half) * HALF_COUNT );
        float *f = g_malloc( sizeof(float) * HALF_COUNT );

        for( int i = 0; i < HALF_COUNT; i++ )
            h[i] = (half) i;

        half_convert_to_float( f, h, HALF_COUNT );
        g_free( h );
        gamma45 = g_malloc( HALF_COUNT );

        for( int i = 0; i < HALF_COUNT; i++ )
            gamma45[i] = (uint8_t) gamma45Func( f[i] );

        g_free( f );

        g_once_init_leave( &__gamma_init, 1 );
    }

    return gamma45;
}

// Rec. 709 transfer functions in float.
// Rec. 709 is:
//
// 4.5L for 0 <= L < 0.018
// 1.099(L^0.45) - 0.099 for 0.018 <= L <= 1
//
// We go ahead and compute it out of this range.

static inline G_GNUC_CONST float
rec709_to_linear( float in ) {
    const float __transition = 4.5f * 0.018f;

    if( in < __transition )
        return in / 4.5f;

    return powf( (in + 0.099f) / 1.099f, 1.0f / 0.45f );
}

static inline G_GNUC_CONST float
linear_to_rec709( float in ) {
    const float __transition = 0.018f;

    if( in < __transition )
        return in * 4.5f;

    return 1.099f * powf( in, 0.45f ) - 0.099f;
}

/*
    Function: video_transfer_rec709_to_linear_scene
    Converts the given half buffer from gamma-encoded to linear using the
    Rec. 709 transfer function with scene intent.

    Parameters:
    in - Pointer to an array of half values to convert.
    out - Pointer to an array to receive the result.
    count - Number of half values to convert.

    Remarks:
    Scene intent means that this function attempts to reconstruct what the
    camera saw. Use <video_transfer_rec709_to_linear_display> for the effect
    of the final display.
*/
EXPORT void
video_transfer_rec709_to_linear_scene( half *out, const half *in, size_t count ) {
    static half *__rec709_to_linear = NULL;
    static gsize __init = 0;

    if( g_once_init_enter( &__init ) ) {
        __rec709_to_linear = g_malloc( sizeof(half) * HALF_COUNT );

        half *h = g_malloc( sizeof(half) * HALF_COUNT );
        float *f = g_malloc( sizeof(float) * HALF_COUNT );

        for( int i = 0; i < HALF_COUNT; i++ )
            h[i] = (half) i;

        half_convert_to_float( f, h, HALF_COUNT );
        g_free( h );

        for( int i = 0; i < HALF_COUNT; i++ )
            f[i] = rec709_to_linear( f[i] );

        half_convert_from_float( __rec709_to_linear, f, HALF_COUNT );
        g_free( f );

        g_once_init_leave( &__init, 1 );
    }

    half_lookup( __rec709_to_linear, out, in, count );
}


/*
    Function: video_transfer_rec709_to_linear_display
    Converts the given half buffer from gamma-encoded to linear using the
    Rec. 709 transfer function with display intent.

    Parameters:
    in - Pointer to an array of half values to convert.
    out - Pointer to an array to receive the result.
    count - Number of half values to convert.

    Remarks:
    Display intent means that this function attempts to reconstruct what a CRT
    would display. Use <video_transfer_rec709_to_linear_scene> to reconstruct
    the scene's values.
*/
EXPORT void
video_transfer_rec709_to_linear_display( half *out, const half *in, size_t count ) {
    static half *__rec709_to_linear = NULL;
    static gsize __init = 0;

    if( g_once_init_enter( &__init ) ) {
        __rec709_to_linear = g_malloc( sizeof(half) * HALF_COUNT );

        half *h = g_malloc( sizeof(half) * HALF_COUNT );
        float *f = g_malloc( sizeof(float) * HALF_COUNT );

        for( int i = 0; i < HALF_COUNT; i++ )
            h[i] = (half) i;

        half_convert_to_float( f, h, HALF_COUNT );
        g_free( h );

        for( int i = 0; i < HALF_COUNT; i++ ) {
            if( f[i] < 0.0f )
                f[i] = 0.0f;
            else
                f[i] = powf( f[i], 2.5f );
        }

        half_convert_from_float( __rec709_to_linear, f, HALF_COUNT );
        g_free( f );

        g_once_init_leave( &__init, 1 );
    }

    half_lookup( __rec709_to_linear, out, in, count );
}

/*
    Function: video_transfer_linear_to_rec709
    Converts the given half buffer from linear to gamma-encoded using the
    Rec. 709 transfer function.

    Parameters:
    in - Pointer to an array of half values to convert.
    out - Pointer to an array to receive the result.
    count - Number of half values to convert.
*/
EXPORT void
video_transfer_linear_to_rec709( half *out, const half *in, size_t count ) {
    static half *__linear_to_rec709 = NULL;
    static gsize __init = 0;

    if( g_once_init_enter( &__init ) ) {
        __linear_to_rec709 = g_malloc( sizeof(half) * HALF_COUNT );

        half *h = g_malloc( sizeof(half) * HALF_COUNT );
        float *f = g_malloc( sizeof(float) * HALF_COUNT );

        for( int i = 0; i < HALF_COUNT; i++ )
            h[i] = (half) i;

        half_convert_to_float( f, h, HALF_COUNT );
        g_free( h );

        for( int i = 0; i < HALF_COUNT; i++ )
            f[i] = linear_to_rec709( f[i] );

        half_convert_from_float( __linear_to_rec709, f, HALF_COUNT );
        g_free( f );

        g_once_init_leave( &__init, 1 );
    }

    half_lookup( __linear_to_rec709, out, in, count );
}


static inline G_GNUC_CONST float
linear_to_sRGB( float in ) {
    // This formula comes to us courtesy of Wikipedia
    const float __transition = 0.0031308f;
    const float a = 0.055;

    if( in <= __transition )
        return in * 12.92f;

    return (1.0f + a) * powf( in, 1.0f / 2.4f ) - a;
}

/*
    Function: video_transfer_linear_to_sRGB
    Converts the given half buffer from linear to gamma-encoded using the
    sRGB transfer function.

    Parameters:
    in - Pointer to an array of half values to convert.
    out - Pointer to an array to receive the result.
    count - Number of half values to convert.
*/
EXPORT void
video_transfer_linear_to_sRGB( half *out, const half *in, size_t count ) {
    static half *__linear_to_sRGB = NULL;
    static gsize __init = 0;

    if( g_once_init_enter( &__init ) ) {
        __linear_to_sRGB = g_malloc( sizeof(half) * HALF_COUNT );

        half *h = g_malloc( sizeof(half) * HALF_COUNT );
        float *f = g_malloc( sizeof(float) * HALF_COUNT );

        for( int i = 0; i < HALF_COUNT; i++ )
            h[i] = (half) i;

        half_convert_to_float( f, h, HALF_COUNT );
        g_free( h );

        for( int i = 0; i < HALF_COUNT; i++ )
            f[i] = linear_to_sRGB( f[i] );

        half_convert_from_float( __linear_to_sRGB, f, HALF_COUNT );
        g_free( f );

        g_once_init_leave( &__init, 1 );
    }

    half_lookup( __linear_to_sRGB, out, in, count );
}

