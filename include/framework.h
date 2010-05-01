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

#include <Python.h>

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
bool parseRational( PyObject *in, rational *out );
PyObject *makeFraction( rational *in );

PyObject *py_make_box2f( box2f *box );
PyObject *py_make_box2i( box2i *box );
PyObject *py_make_v2f( v2f *v );
PyObject *py_make_v2i( v2i *v );

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
} rgba_f16_frame;

typedef struct {
    rgba_f32 *frameData;
    box2i fullDataWindow;
    box2i currentDataWindow;
    int stride;
} rgba_f32_frame;

typedef struct {
    GLuint texture;
    box2i fullDataWindow;
    box2i currentDataWindow;
} rgba_gl_frame;

typedef void (*video_getFrameFunc)( void *self, int frameIndex, rgba_f16_frame *frame );
typedef void (*video_getFrame32Func)( void *self, int frameIndex, rgba_f32_frame *frame );
typedef void (*video_getFrameGLFunc)( void *self, int frameIndex, rgba_gl_frame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_getFrameFunc getFrame;
    video_getFrame32Func getFrame32;
    video_getFrameGLFunc getFrameGL;
} VideoFrameSourceFuncs;

G_GNUC_PURE static inline rgba_f16 *getPixel_f16( rgba_f16_frame *frame, int x, int y ) {
    return &frame->frameData[(y - frame->fullDataWindow.min.y) * frame->stride + x - frame->fullDataWindow.min.x];
}

G_GNUC_PURE static inline rgba_f32 *getPixel_f32( rgba_f32_frame *frame, int x, int y ) {
    return &frame->frameData[(y - frame->fullDataWindow.min.y) * frame->stride + x - frame->fullDataWindow.min.x];
}

typedef struct {
    void *obj;
    VideoFrameSourceFuncs *funcs;
} video_source;

typedef struct {
    video_source source;
    PyObject *csource;
} VideoSourceHolder;

bool takeVideoSource( PyObject *source, VideoSourceHolder *holder );
void getFrame_f16( video_source *source, int frameIndex, rgba_f16_frame *targetFrame );
void getFrame_f32( video_source *source, int frameIndex, rgba_f32_frame *targetFrame );
void getFrame_gl( video_source *source, int frameIndex, rgba_gl_frame *targetFrame );
void *getCurrentGLContext();

#define gl_checkError()        __gl_checkError(__FILE__, __LINE__)
void __gl_checkError(const char *file, const unsigned long line);

void gl_printShaderErrors( GLhandleARB shader );
void gl_renderToTexture( rgba_gl_frame *frame );
void gl_buildShader( const char *source, GLhandleARB *outShader, GLhandleARB *outProgram );

#define VIDEO_FRAME_SOURCE_FUNCS "_video_frame_source_funcs"

/************* Audio *******/

typedef struct {
    float *frameData;
    int channelCount;
    int fullMinSample, fullMaxSample;
    int currentMinSample, currentMaxSample;
} AudioFrame;

typedef void (*audio_getFrameFunc)( PyObject *self, AudioFrame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    audio_getFrameFunc getFrame;
} AudioFrameSourceFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    AudioFrameSourceFuncs *funcs;
} AudioSourceHolder;

bool takeAudioSource( PyObject *source, AudioSourceHolder *holder );

#define AUDIO_FRAME_SOURCE_FUNCS "_audio_frame_source_funcs"

/*********** Frame functions *****/

typedef enum _CONST_TYPE {
    CONST_TYPE_INT32,
    CONST_TYPE_FLOAT32,
} const_type;

// Frame functions: Given an array of *count* frame indexes in *frames*,
// produce an equivalent array of *outValues* to use (allocated by the caller).
// Each frame index is divided by *div* to support subframe calculations.

typedef void (*framefunc_getValues_i32_func)( PyObject *self, ssize_t count, int64_t *frames, int64_t div, int *outValues );
typedef void (*framefunc_getValues_f32_func)( PyObject *self, ssize_t count, int64_t *frames, int64_t div, float *outValues );
typedef void (*framefunc_getValues_v2i_func)( PyObject *self, ssize_t count, int64_t *frames, int64_t div, v2i *outValues );
typedef void (*framefunc_getValues_v2f_func)( PyObject *self, ssize_t count, int64_t *frames, int64_t div, v2f *outValues );
typedef void (*framefunc_getValues_box2i_func)( PyObject *self, ssize_t count, int64_t *frames, int64_t div, box2i *outValues );
typedef void (*framefunc_getValues_box2f_func)( PyObject *self, ssize_t count, int64_t *frames, int64_t div, box2f *outValues );

typedef struct {
    int flags;
    framefunc_getValues_i32_func getValues_i32;
    framefunc_getValues_f32_func getValues_f32;
    framefunc_getValues_v2i_func getValues_v2i;
    framefunc_getValues_v2f_func getValues_v2f;
    framefunc_getValues_box2i_func getValues_box2i;
    framefunc_getValues_box2f_func getValues_box2f;
} FrameFunctionFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    FrameFunctionFuncs *funcs;
    union {
        int const_i32;
        v2i const_v2i;
        box2i const_box2i;
        int const_i32_array[4];

        float const_f32;
        v2f const_v2f;
        box2f const_box2f;
        float const_f32_array[4];
    } constant;
    const_type constant_type;
} FrameFunctionHolder;

bool frameFunc_takeSource( PyObject *source, FrameFunctionHolder *holder );
int frameFunc_get_i32( FrameFunctionHolder *holder, int64_t frame, int64_t div );
float frameFunc_get_f32( FrameFunctionHolder *holder, int64_t frame, int64_t div );
void frameFunc_get_v2f( FrameFunctionHolder *holder, int64_t frame, int64_t div, v2f *result );
void frameFunc_get_box2i( FrameFunctionHolder *holder, int64_t frame, int64_t div, box2i *result );

#define FRAME_FUNCTION_FUNCS "_frame_function_funcs"

#if defined(__cplusplus)
}
#endif

#endif

