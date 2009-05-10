
#include <Python.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
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
    Imf::Array2D<Imf::Rgba> frameData;
    Imath::Box2i fullDataWindow;
    Imath::Box2i currentDataWindow;
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

