
#include <Python.h>

extern "C" {
#include <avformat.h>
#include <swscale.h>
}

#include <memory>
#include <errno.h>

#include <Iex.h>
#include <ImathMath.h>
#include <ImathFun.h>
#include <ImfRgbaFile.h>
#include <ImfHeader.h>
#include <ImfRational.h>
#include <ImfArray.h>
#include <halfFunction.h>

#define NOEXPORT __attribute__((visibility("hidden")))

typedef struct {
    Imf::Rgba *base;
    Imath::Box2i originalDataWindow;
    Imath::Box2i currentDataWindow;
    int stride;
} VideoFrame;

typedef void (*video_getFrameFunc)( PyObject *self, int64_t frameIndex, VideoFrame *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_getFrameFunc getFrame;
} VideoFrameSourceFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    VideoFrameSourceFuncs *funcs;
} VideoSourceHolder;

NOEXPORT int takeVideoSource( PyObject *source, VideoSourceHolder *holder );

