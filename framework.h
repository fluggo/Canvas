
#if !defined(fluggo_framework)
#define fluggo_framework

#include <Python.h>

#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <stdbool.h>
#include "half.h"

#include <memory.h>
#include <errno.h>

#define NOEXPORT __attribute__((visibility("hidden")))

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

static inline void box2i_set( box2i *box, int minX, int minY, int maxX, int maxY ) {
    box->min.x = minX;
    box->min.y = minY;
    box->max.x = maxX;
    box->max.y = maxY;
}

static inline void box2i_setEmpty( box2i *box ) {
    box2i_set( box, 0, 0, -1, -1 );
}

static inline bool box2i_isEmpty( box2i *box ) {
    return box->max.x < box->min.x || box->max.y < box->min.y;
}

static inline void box2i_getSize( box2i *box, v2i *result ) {
    result->x = (box->max.x < box->min.x) ? 0 : (box->max.x - box->min.x + 1);
    result->y = (box->max.y < box->min.y) ? 0 : (box->max.y - box->min.y + 1);
}

static inline int min( int a, int b ) {
    return a < b ? a : b;
}

static inline int max( int a, int b ) {
    return a > b ? a : b;
}

NOEXPORT int64_t getFrameTime( rational *frameRate, int frame );
NOEXPORT int getTimeFrame( rational *frameRate, int64_t time );
NOEXPORT bool parseRational( PyObject *in, rational *out );

/************* Video *******/
typedef struct {
    half r, g, b, a;
} rgba;

typedef struct {
    rgba *frameData;
    box2i fullDataWindow;
    box2i currentDataWindow;
    int stride;
} RgbaFrame;

typedef void (*video_getFrameFunc)( PyObject *self, int64_t frameIndex, RgbaFrame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_getFrameFunc getFrame;
} VideoFrameSourceFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    VideoFrameSourceFuncs *funcs;
} VideoSourceHolder;

NOEXPORT bool takeVideoSource( PyObject *source, VideoSourceHolder *holder );

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

#endif

