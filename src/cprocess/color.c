/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2010 Brian J. Crowell <brian@fluggo.com>

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

#include "framework.h"

typedef struct __tag_xyz {
    float x, y, z, dummy;
} xyz;

/*
static inline void
mult_xyz_xyz( xyz *a, const xyz *b ) {
    a->x *= b->x;
    a->y *= b->y;
    a->z *= b->z;
}
*/

static inline void
mult_mat_xyz( const xyz *a, const xyz *b, const xyz *c, rgba_f32 *v ) {
    rgba_f32 r = {
        v->r * a->x + v->g * b->x + v->b * c->x,
        v->r * a->y + v->g * b->y + v->b * c->y,
        v->r * a->z + v->g * b->z + v->b * c->z,
        v->a };
    *v = r;
}

/*
BJC: These are untested. Because I went to the trouble of researching them,
I'm leaving them here, in case I care to use them again.

static inline void
cross( xyz *out, xyz *a, xyz *b ) {
    out->x = a->y * b->z - a->z * b->y;
    out->y = a->z * b->x - a->x * b->z;
    out->z = a->x * b->y - a->y * a->x;
}

static inline float
dot( xyz *a, xyz *b ) {
    return a->x * b->x + a->y * b->y + a->z * b->z;
}

static inline bool
invert( xyz *a, xyz *b, xyz *c ) {
    // Yay, inverting a 3x3 matrix!
    // a, b, and c are already column vectors, so we can jump to:
    xyz r[3];
    xyz temp;

    cross( &temp, b, c );
    float det = dot( a, &temp );

    if( det == 0.0f )
        return false;

    det = 1.0f / det;
    cross( &r[0], b, c );
    cross( &r[1], c, a );
    cross( &r[2], a, b );

    a->x = r[0].x * det;
    a->y = r[1].x * det;
    a->z = r[2].x * det;
    b->x = r[0].y * det;
    b->y = r[1].y * det;
    b->z = r[2].y * det;
    c->x = r[0].z * det;
    c->y = r[1].z * det;
    c->z = r[2].z * det;

    return true;
}
*/

/*
    Function: video_color_rgb_to_xyz_sdtv
    Converts an SDTV frame from SDTV RGB (SMPTE C primaries, D65 whitepoint, Rec. 709 transfer function)
    into linear XYZ.

    Parameters:
    frame - Frame to convert in-place.

    Remarks:
    This is a transitional function. Eventally this functionality will be in a more
    general algorithm.
*/
EXPORT void
video_color_rgb_to_xyz_sdtv( rgba_frame_f16 *frame ) {
    // D65 (from Poynton, p. 239)
//    const xyz d65 = { 0.3127f / 0.3290f, 0.3290f / 0.3290f, 0.3582f / 0.3290f };

    // SMPTE-C chromaticities (from Poynton, p. 239)
/*    const xyz
        r = { 0.630f * d65.x, 0.340f * d65.y, 0.030f * d65.z },
        g = { 0.310f * d65.x, 0.595f * d65.y, 0.095f * d65.z },
        b = { 0.155f * d65.x, 0.070f * d65.y, 0.775f * d65.z };*/

    const xyz
        r = { 0.3936f, 0.2124f, 0.0187f },
        g = { 0.3652f, 0.7010f, 0.1119f },
        b = { 0.1916f, 0.0865f, 0.9582f };

    box2i *box = &frame->current_window;
    int width = box->max.x - box->min.x + 1;
    rgba_f32 *f = g_slice_alloc( sizeof(rgba_f32) * width );

    for( int y = box->min.y; y <= box->max.y; y++ ) {
        rgba_f16 *h = video_get_pixel_f16( frame, box->min.x, y );

        video_transfer_rec709_to_linear_scene( &h->r, &h->r, width * 4 );
        rgba_f16_to_f32( f, h, width );

        for( int x = 0; x < width; x++ )
            mult_mat_xyz( &r, &g, &b, &f[x] );

        rgba_f32_to_f16( h, f, width );
    }

    g_slice_free1( sizeof(rgba_f32) * width, f );
}
//(0.433350, 0.395264, 0.372803)->(1.042124, -0.000769, 0.194427)

EXPORT void
video_color_xyz_to_srgb( rgba_frame_f16 *frame ) {
    // XYZ->sRGB (Wikipedia)
    const xyz
        r = {  3.2410f, -0.9692f,  0.0556f },
        g = { -1.5374f,  1.8760f, -0.2040f },
        b = { -0.4986f,  0.0416f,  1.0570f };

    box2i *box = &frame->current_window;
    int width = box->max.x - box->min.x + 1;
    rgba_f32 *f = g_slice_alloc( sizeof(rgba_f32) * width );

    for( int y = box->min.y; y <= box->max.y; y++ ) {
        rgba_f16 *h = video_get_pixel_f16( frame, box->min.x, y );

        rgba_f16_to_f32( f, h, width );

        for( int x = 0; x < width; x++ )
            mult_mat_xyz( &r, &g, &b, &f[x] );

        rgba_f32_to_f16( h, f, width );
        video_transfer_linear_to_sRGB( &h->r, &h->r, width * 4 );
    }

    g_slice_free1( sizeof(rgba_f32) * width, f );
}



