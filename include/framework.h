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

#if !defined(fluggo_framework)
#define fluggo_framework

#include <stdint.h>
#include <stdbool.h>
#include "half.h"

#define EXPORT __attribute__((visibility("default")))

#include <glib.h>

#include <GL/glew.h>
#include <GL/gl.h>

#define NS_PER_SEC    INT64_C(1000000000)

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    int n;
    unsigned int d;
} rational;

typedef struct {
    int x, y;
} v2i;

typedef struct {
    v2i min, max;
} box2i;

typedef struct {
    float x, y;
} v2f;

typedef struct {
    v2f min, max;
} box2f;

G_GNUC_CONST static inline int min( int a, int b ) {
    return a < b ? a : b;
}

G_GNUC_CONST static inline int max( int a, int b ) {
    return a > b ? a : b;
}

static inline void box2i_set( box2i *box, int minX, int minY, int maxX, int maxY ) {
    box->min.x = minX;
    box->min.y = minY;
    box->max.x = maxX;
    box->max.y = maxY;
}

static inline void box2i_setEmpty( box2i *box ) {
    box2i_set( box, 0, 0, -1, -1 );
}

G_GNUC_PURE static inline bool box2i_isEmpty( const box2i *box ) {
    return box->max.x < box->min.x || box->max.y < box->min.y;
}

static inline void box2i_intersect( box2i *result, const box2i *first, const box2i *second ) {
    result->min.x = max(first->min.x, second->min.x);
    result->min.y = max(first->min.y, second->min.y);
    result->max.x = min(first->max.x, second->max.x);
    result->max.y = min(first->max.y, second->max.y);
}

static inline void box2i_union( box2i *result, const box2i *first, const box2i *second ) {
    result->min.x = min(first->min.x, second->min.x);
    result->min.y = min(first->min.y, second->min.y);
    result->max.x = max(first->max.x, second->max.x);
    result->max.y = max(first->max.y, second->max.y);
}

static inline void box2i_normalize( box2i *result ) {
    int temp;

    if( result->min.x > result->max.x ) {
        temp = result->min.x - 1;
        result->min.x = result->max.x + 1;
        result->max.x = temp;
    }

    if( result->min.y > result->max.y ) {
        temp = result->min.y - 1;
        result->min.y = result->max.y + 1;
        result->max.y = temp;
    }
}

static inline void box2i_getSize( const box2i *box, v2i *result ) {
    result->x = (box->max.x < box->min.x) ? 0 : (box->max.x - box->min.x + 1);
    result->y = (box->max.y < box->min.y) ? 0 : (box->max.y - box->min.y + 1);
}

G_GNUC_CONST static inline float minf( float a, float b ) {
    return a < b ? a : b;
}

G_GNUC_CONST static inline float maxf( float a, float b ) {
    return a > b ? a : b;
}

G_GNUC_CONST static inline float clampf( float value, float min, float max ) {
    return minf(maxf(value, min), max);
}

int64_t getFrameTime( const rational *frameRate, int frame );
int getTimeFrame( const rational *frameRate, int64_t time );

/************* Video *******/
typedef struct {
    half r, g, b, a;
} rgba_f16;

typedef struct {
    uint8_t r, g, b, a;
} rgba_u8;

typedef struct {
    float r, g, b, a;
} rgba_f32;

typedef struct {
    rgba_f16 *frameData;
    box2i fullDataWindow;
    box2i currentDataWindow;
    int stride;
} rgba_frame_f16;

typedef struct {
    rgba_f32 *frameData;
    box2i fullDataWindow;
    box2i currentDataWindow;
    int stride;
} rgba_frame_f32;

typedef struct {
    GLuint texture;
    box2i fullDataWindow;
    box2i currentDataWindow;
} rgba_frame_gl;

typedef void (*video_getFrameFunc)( void *self, int frameIndex, rgba_frame_f16 *frame );
typedef void (*video_getFrame32Func)( void *self, int frameIndex, rgba_frame_f32 *frame );
typedef void (*video_getFrameGLFunc)( void *self, int frameIndex, rgba_frame_gl *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_getFrameFunc getFrame;
    video_getFrame32Func getFrame32;
    video_getFrameGLFunc getFrameGL;
} VideoFrameSourceFuncs;

G_GNUC_PURE static inline rgba_f16 *getPixel_f16( rgba_frame_f16 *frame, int x, int y ) {
    return &frame->frameData[(y - frame->fullDataWindow.min.y) * frame->stride + x - frame->fullDataWindow.min.x];
}

G_GNUC_PURE static inline rgba_f32 *getPixel_f32( rgba_frame_f32 *frame, int x, int y ) {
    return &frame->frameData[(y - frame->fullDataWindow.min.y) * frame->stride + x - frame->fullDataWindow.min.x];
}

typedef struct {
    void *obj;
    VideoFrameSourceFuncs *funcs;
} video_source;

void video_getFrame_f16( video_source *source, int frameIndex, rgba_frame_f16 *targetFrame );
void video_getFrame_f32( video_source *source, int frameIndex, rgba_frame_f32 *targetFrame );
void video_getFrame_gl( video_source *source, int frameIndex, rgba_frame_gl *targetFrame );
const uint8_t *video_get_gamma45_ramp();

void *getCurrentGLContext();

#define gl_checkError()        __gl_checkError(__FILE__, __LINE__)
void __gl_checkError(const char *file, const unsigned long line);

void gl_printShaderErrors( GLhandleARB shader );
void gl_renderToTexture( rgba_frame_gl *frame );
void gl_buildShader( const char *source, GLhandleARB *outShader, GLhandleARB *outProgram );

/************* Audio *******/

typedef struct {
    float *frameData;
    int channelCount;
    int fullMinSample, fullMaxSample;
    int currentMinSample, currentMaxSample;
} AudioFrame;

typedef void (*audio_getFrameFunc)( void *self, AudioFrame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    audio_getFrameFunc getFrame;
} AudioFrameSourceFuncs;

typedef struct {
    void *obj;
    AudioFrameSourceFuncs *funcs;
} audio_source;

/************ Codec packet source ******/

#define PACKET_TS_NONE      INT64_C(0x8000000000000000)

typedef struct {
    void *data;
    int length;
    int64_t pts, dts;

    GFreeFunc free_func;
} codec_packet;

typedef codec_packet *(*codec_getNextPacketFunc)( void *self );
typedef bool (*codec_seekFunc)( void *self, int64_t frame );

/*
    codec.getHeader:
    Get the header data found in the source, if any.

    buffer: Buffer in which to store header data. May be NULL.

    If buffer is NULL, returns the size of the header in bytes, or
    zero if there is no header.
    If buffer is not NULL, returns 1 on success and 0 on failure.
*/
typedef int (*codec_getHeaderFunc)( void *self, void *buffer );

typedef struct {
    int flags;          // Reserved, should be zero
    codec_getNextPacketFunc getNextPacket;
    codec_seekFunc seek;
    codec_getHeaderFunc getHeader;
} codec_packet_source_funcs;

typedef struct {
    void *obj;
    codec_packet_source_funcs *funcs;
} codec_packet_source;

/************ Coded image source ******/

#define CODED_IMAGE_MAX_PLANES 4

typedef struct {
    void *data[CODED_IMAGE_MAX_PLANES];
    int stride[CODED_IMAGE_MAX_PLANES];
    int line_count[CODED_IMAGE_MAX_PLANES];

    GFreeFunc free_func;
} coded_image;

/*
    Function: coded_image_alloc
    Allocates a coded_image, along with the storage for all of its planes.

    Parameters:
    strides - Pointer to an array containing the stride of each plane. Strides can be zero.
    line_counts - Pointer to an array containing the number of lines in each plane. Can also be zero.
    count - Number of planes in strides and line_counts. Should be less than or equal to CODED_IMAGE_MAX_PLANES.

    Returns:
    A pointer to a new coded_image, if successful.
    The planes will be uninitialized.
    Call free_func on the resulting image to free it.
*/
G_GNUC_MALLOC coded_image *coded_image_alloc( const int *strides, const int *line_counts, int count );

/*
    Function: coded_image_alloc
    Allocates a coded_image and its planes and zeroes the memory.

    Parameters:
    strides - Pointer to an array containing the stride of each plane. Strides can be zero.
    line_counts - Pointer to an array containing the number of lines in each plane. Can also be zero.
    count - Number of planes in strides and line_counts. Should be less than or equal to CODED_IMAGE_MAX_PLANES.

    Returns:
    A pointer to a new coded_image, if successful.
    The planes will be initialized with zeroes.
    Call free_func on the resulting image to free it.
*/
G_GNUC_MALLOC coded_image *coded_image_alloc0( const int *strides, const int *line_counts, int count );

typedef coded_image *(*coded_image_getFrameFunc)( void *self, int frame );

typedef struct {
    int flags;
    coded_image_getFrameFunc getFrame;
} coded_image_source_funcs;

typedef struct {
    void *obj;
    coded_image_source_funcs *funcs;
} coded_image_source;

#if defined(__cplusplus)
}
#endif

#endif

