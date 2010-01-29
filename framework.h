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

#include <memory.h>
#include <errno.h>

#define EXPORT __attribute__((visibility("default")))

#if 1
#include <glib.h>
#define slice_alloc(size) g_slice_alloc(size)
#define slice_alloc0(size) g_slice_alloc0(size)
#define slice_free(size, ptr) g_slice_free1(size, ptr)
#else
#define slice_alloc(size) g_malloc(size)
#define slice_alloc0(size) g_malloc0(size)
#define slice_free(size, ptr) g_free(ptr)
#endif

#include <GL/glew.h>
#include <GL/gl.h>

#define NS_PER_SEC    INT64_C(1000000000)

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

static inline int min( int a, int b ) {
    return a < b ? a : b;
}

static inline int max( int a, int b ) {
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

static inline bool box2i_isEmpty( const box2i *box ) {
    return box->max.x < box->min.x || box->max.y < box->min.y;
}

static inline void box2i_intersect( box2i *result, const box2i *first, const box2i *second ) {
    result->min.x = max(first->min.x, second->min.x);
    result->min.y = max(first->min.y, second->min.y);
    result->max.x = min(first->max.x, second->max.x);
    result->max.y = min(first->max.y, second->max.y);
}

static inline void box2i_getSize( const box2i *box, v2i *result ) {
    result->x = (box->max.x < box->min.x) ? 0 : (box->max.x - box->min.x + 1);
    result->y = (box->max.y < box->min.y) ? 0 : (box->max.y - box->min.y + 1);
}

static inline float minf( float a, float b ) {
    return a < b ? a : b;
}

static inline float maxf( float a, float b ) {
    return a > b ? a : b;
}

static inline float clampf( float value, float min, float max ) {
    return minf(maxf(value, min), max);
}

int64_t getFrameTime( const rational *frameRate, int frame );
int getTimeFrame( const rational *frameRate, int64_t time );
bool parseRational( PyObject *in, rational *out );
PyObject *makeFraction( rational *in );

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

typedef void (*video_getFrameFunc)( PyObject *self, int frameIndex, rgba_f16_frame *frame );
typedef void (*video_getFrame32Func)( PyObject *self, int frameIndex, rgba_f32_frame *frame );
typedef void (*video_getFrameGLFunc)( PyObject *self, int frameIndex, rgba_gl_frame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_getFrameFunc getFrame;
    video_getFrame32Func getFrame32;
    video_getFrameGLFunc getFrameGL;
} VideoFrameSourceFuncs;

static inline rgba_f16 *getPixel_f16( rgba_f16_frame *frame, int x, int y ) {
    return &frame->frameData[(y - frame->fullDataWindow.min.y) * frame->stride + x - frame->fullDataWindow.min.x];
}

static inline rgba_f32 *getPixel_f32( rgba_f32_frame *frame, int x, int y ) {
    return &frame->frameData[(y - frame->fullDataWindow.min.y) * frame->stride + x - frame->fullDataWindow.min.x];
}

typedef struct {
    PyObject *source;
    PyObject *csource;
    VideoFrameSourceFuncs *funcs;
} VideoSourceHolder;

bool takeVideoSource( PyObject *source, VideoSourceHolder *holder );
void getFrame_f16( VideoSourceHolder *source, int frameIndex, rgba_f16_frame *targetFrame );
void getFrame_f32( VideoSourceHolder *source, int frameIndex, rgba_f32_frame *targetFrame );
void getFrame_gl( VideoSourceHolder *source, int frameIndex, rgba_gl_frame *targetFrame );
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

typedef void (*framefunc_getValuesFunc)( PyObject *self, int count, long *frames, long frameBase, float *outValues );

typedef struct {
    int flags;
    framefunc_getValuesFunc getValues;
} FrameFunctionFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    FrameFunctionFuncs *funcs;
    float constant;
} FrameFunctionHolder;

bool takeFrameFunc( PyObject *source, FrameFunctionHolder *holder );

#define FRAME_FUNCTION_FUNCS "_frame_function_funcs"

#endif

