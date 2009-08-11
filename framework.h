
#if !defined(fluggo_framework)
#define fluggo_framework

#include <Python.h>

#include <stdint.h>
#include <stdbool.h>
#include "half.h"

#include <memory.h>
#include <errno.h>

#define NOEXPORT __attribute__((visibility("hidden")))

#if 1
#include <glib.h>
#define slice_alloc(size) g_slice_alloc(size)
#define slice_alloc0(size) g_slice_alloc0(size)
#define slice_free(size, ptr) g_slice_free1(size, ptr)
#else
#define slice_alloc(size) malloc(size)
#define slice_alloc0(size) calloc(1, size)
#define slice_free(size, ptr) free(ptr)
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

static inline void box2i_getSize( const box2i *box, v2i *result ) {
    result->x = (box->max.x < box->min.x) ? 0 : (box->max.x - box->min.x + 1);
    result->y = (box->max.y < box->min.y) ? 0 : (box->max.y - box->min.y + 1);
}

static inline int min( int a, int b ) {
    return a < b ? a : b;
}

static inline int max( int a, int b ) {
    return a > b ? a : b;
}

static inline float min_f32( float a, float b ) {
    return a < b ? a : b;
}

static inline float max_f32( float a, float b ) {
    return a > b ? a : b;
}

NOEXPORT int64_t getFrameTime( const rational *frameRate, int frame );
NOEXPORT int getTimeFrame( const rational *frameRate, int64_t time );
NOEXPORT bool parseRational( PyObject *in, rational *out );

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
    int targetTexture;
    box2f fullDataWindow;
    box2f currentDataWindow;
} GLFrame;

typedef void (*video_getFrameFunc)( PyObject *self, int frameIndex, rgba_f16_frame *frame );
typedef void (*video_getFrame32Func)( PyObject *self, int frameIndex, rgba_f32_frame *frame );
typedef void (*video_getGLFrameFunc)( PyObject *self, int frameIndex, GLFrame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_getFrameFunc getFrame;
    video_getFrame32Func getFrame32;
    video_getGLFrameFunc getGLFrame;
} VideoFrameSourceFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    VideoFrameSourceFuncs *funcs;
} VideoSourceHolder;

NOEXPORT bool takeVideoSource( PyObject *source, VideoSourceHolder *holder );
NOEXPORT void getFrame_f16( VideoSourceHolder *source, int frameIndex, rgba_f16_frame *targetFrame );
NOEXPORT void getFrame_f32( VideoSourceHolder *source, int frameIndex, rgba_f32_frame *targetFrame );

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

NOEXPORT bool takeAudioSource( PyObject *source, AudioSourceHolder *holder );

/*********** Time functions *****/

typedef void (*timefunc_getValuesFunc)( PyObject *self, int count, long *times, float *outValues );

typedef struct {
    int flags;
    timefunc_getValuesFunc getValues;
} TimeFunctionFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    TimeFunctionFuncs *funcs;
    float constant;
} TimeFunctionHolder;

NOEXPORT bool takeTimeFunction( PyObject *source, TimeFunctionHolder *holder );

#endif

