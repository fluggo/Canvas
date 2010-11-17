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
    video_source *source;
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

typedef void (*framefunc_get_values_func)( PyObject *self, ssize_t count, double *frames, double (*out_values)[4] );

typedef struct {
    int flags;
    framefunc_get_values_func get_values;
} FrameFunctionFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    FrameFunctionFuncs *funcs;
    double constant[4];
} FrameFunctionHolder;

bool py_framefunc_take_source( PyObject *source, FrameFunctionHolder *holder );
int framefunc_get_i32( FrameFunctionHolder *holder, double frame );
float framefunc_get_f32( FrameFunctionHolder *holder, double frame );
void framefunc_get_v2f( v2f *result, FrameFunctionHolder *holder, double frame );
void framefunc_get_box2i( box2i *result, FrameFunctionHolder *holder, double frame );
void framefunc_get_rgba_f32( rgba_f32 *result, FrameFunctionHolder *holder, double frame );

extern PyTypeObject py_type_FrameFunction;

#define FRAME_FUNCTION_FUNCS "_frame_function_funcs"

/**** Clock **/

typedef struct {
    presentation_clock source;
    PyObject *csource;
} PresentationClockHolder;

bool py_presentation_clock_take_source( PyObject *source, PresentationClockHolder *holder );

extern PyTypeObject py_type_PresentationClock;

#define PRESENTATION_CLOCK_FUNCS "_presentation_clock_funcs"

/**** Codec packets **/

typedef struct {
    codec_packet_source source;
    PyObject *csource;
} CodecPacketSourceHolder;

bool py_codec_packet_take_source( PyObject *source, CodecPacketSourceHolder *holder );

extern PyTypeObject py_type_CodecPacketSource;

#define CODEC_PACKET_SOURCE_FUNCS "_codec_packet_source_funcs"

/**** Coded images **/

typedef struct {
    coded_image_source source;
    PyObject *csource;
} CodedImageSourceHolder;

bool py_coded_image_take_source( PyObject *source, CodedImageSourceHolder *holder );

extern PyTypeObject py_type_CodedImageSource;

#define CODED_IMAGE_SOURCE_FUNCS "_coded_image_source_funcs"

#if defined(__cplusplus)
}
#endif

#endif

