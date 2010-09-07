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

#if !defined(fluggo_pyframework)
#define fluggo_pyframework

#include <Python.h>
#include "framework.h"

#if defined(__cplusplus)
extern "C" {
#endif

bool py_parse_rational( PyObject *in, rational *out );
PyObject *py_make_rational( rational *in );

PyObject *py_make_rgba_f32( rgba_f32 *color );
PyObject *py_make_box2f( box2f *box );
PyObject *py_make_box2i( box2i *box );
PyObject *py_make_v2f( v2f *v );
PyObject *py_make_v2i( v2i *v );

bool py_parse_rgba_f32( PyObject *obj, rgba_f32 *color );
bool py_parse_box2f( PyObject *obj, box2f *box );
bool py_parse_box2i( PyObject *obj, box2i *box );
bool py_parse_v2f( PyObject *obj, v2f *v );
bool py_parse_v2i( PyObject *obj, v2i *v );

/**** Video **/

typedef struct {
    video_source source;
    PyObject *csource;
} VideoSourceHolder;

bool py_video_take_source( PyObject *source, VideoSourceHolder *holder );

extern PyTypeObject py_type_VideoSource;

#define VIDEO_FRAME_SOURCE_FUNCS "_video_frame_source_funcs"

/**** Audio **/

typedef struct {
    audio_source source;
    PyObject *csource;
} AudioSourceHolder;

bool py_audio_take_source( PyObject *source, AudioSourceHolder *holder );

extern PyTypeObject py_type_AudioSource;

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

bool py_frameFunc_takeSource( PyObject *source, FrameFunctionHolder *holder );
int frameFunc_get_i32( FrameFunctionHolder *holder, int64_t frame, int64_t div );
float frameFunc_get_f32( FrameFunctionHolder *holder, int64_t frame, int64_t div );
void frameFunc_get_v2f( FrameFunctionHolder *holder, int64_t frame, int64_t div, v2f *result );
void frameFunc_get_box2i( FrameFunctionHolder *holder, int64_t frame, int64_t div, box2i *result );

#define FRAME_FUNCTION_FUNCS "_frame_function_funcs"

/**** Clock **/

typedef struct {
    presentation_clock source;
    PyObject *csource;
} PresentationClockHolder;

bool takePresentationClock( PyObject *source, PresentationClockHolder *holder );

extern PyTypeObject py_type_PresentationClock;

#define PRESENTATION_CLOCK_FUNCS "_presentation_clock_funcs"

/**** Codec packets **/

typedef struct {
    codec_packet_source source;
    PyObject *csource;
} CodecPacketSourceHolder;

bool py_codecPacket_takeSource( PyObject *source, CodecPacketSourceHolder *holder );

extern PyTypeObject py_type_CodecPacketSource;

#define CODEC_PACKET_SOURCE_FUNCS "_codec_packet_source_funcs"

/**** Coded images **/

typedef struct {
    coded_image_source source;
    PyObject *csource;
} CodedImageSourceHolder;

bool py_codedImage_takeSource( PyObject *source, CodedImageSourceHolder *holder );

extern PyTypeObject py_type_CodedImageSource;

#define CODED_IMAGE_SOURCE_FUNCS "_coded_image_source_funcs"

#if defined(__cplusplus)
}
#endif

#endif

