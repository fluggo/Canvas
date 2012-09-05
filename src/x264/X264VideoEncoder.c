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

#include "pyframework.h"
#include <libavcodec/avcodec.h>
#include <x264.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.x264.X264VideoEncoder"

typedef struct {
    PyObject_HEAD

    int start_frame, end_frame, current_frame;
    CodedImageSourceHolder source;
    x264_t *encoder;
} py_obj_X264VideoEncoder;

static int
X264VideoEncoder_init( py_obj_X264VideoEncoder *self, PyObject *args, PyObject *kw ) {
    self->source.csource = NULL;

    PyObject *source_obj;
    const char *preset = NULL, *tune = NULL, *profile = NULL;

    static char *kwlist[] = { "source", "start_frame", "end_frame",
        "preset", "tune", "profile", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Oii|sss", kwlist, &source_obj,
            &self->current_frame, &self->end_frame,
            &preset, &tune, &profile ) )
        return -1;

    self->start_frame = self->current_frame;

    // Parse and validate the arguments
    x264_param_t params;

    if( x264_param_default_preset( &params, preset, tune ) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set x264 presets." );
        return -1;
    }

    params.i_width = 720;
    params.i_height = 480;
    params.i_csp = X264_CSP_I420;
    params.i_frame_total = (self->end_frame - self->start_frame + 1);
    params.vui.i_sar_height = 33;   // 16:9
    params.vui.i_sar_width = 40;
    params.vui.i_overscan = 2;      // yes overscan
    params.vui.i_vidformat = 2;     // NTSC
    params.vui.b_fullrange = 0;     // Studio level encoding
    params.vui.i_colorprim = 1;     // Rec. 709 primaries
    params.vui.i_transfer = 1;      // Rec. 709 transfer
    params.vui.i_colmatrix = 1;     // Rec. 709 matrix (incidentally, 0 encodes RGB)
    params.vui.i_chroma_loc = 0;    // MPEG2-style chroma siting
    params.b_interlaced = 1;
    params.rc.i_rc_method = X264_RC_CRF;        // Constant quality
    params.rc.f_rf_constant = 23.0f;            // Ratefactor (quality)
    // Specify max VBV bitrate here
    params.i_fps_num = 30000;
    params.i_fps_den = 1001;
    params.i_timebase_num = 1001;   // Special things to be done here for pulldown
    params.i_timebase_den = 30000;
    params.b_annexb = 1;            // Suitable for standalone file

    x264_param_apply_fastfirstpass( &params );

    if( x264_param_apply_profile( &params, profile ) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set x264 profile." );
        return -1;
    }

    if( !py_coded_image_take_source( source_obj, &self->source ) )
        return -1;

    self->encoder = x264_encoder_open( &params );

    return 0;
}

static void
X264VideoEncoder_dealloc( py_obj_X264VideoEncoder *self ) {
    if( self->encoder )
        x264_encoder_close( self->encoder );

    py_coded_image_take_source( NULL, &self->source );
    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static void
my_packet_free( codec_packet *packet ) {
    g_assert(packet);
    g_slice_free1( packet->length, packet->data );
    g_slice_free( codec_packet, packet );
}

static int
X264VideoEncoder_get_header( py_obj_X264VideoEncoder *self, void *buffer ) {
    if( !self->encoder )
        return 0;

    x264_nal_t *nals;
    int count;

    if( x264_encoder_headers( self->encoder, &nals, &count ) < 0 )
        return 0;

    int size = 0;

    for( int i = 0; i < count; i++ ) {
        size += nals[i].i_payload;
    }

    if( !buffer )
        return size;

    memcpy( buffer, nals[0].p_payload, size );
    return 1;
}

static codec_packet *
X264VideoEncoder_get_next_packet( py_obj_X264VideoEncoder *self ) {
    codec_packet *packet;
    x264_picture_t pict_out;
    x264_nal_t *nals;
    int result, nal_count;

    while( self->current_frame < self->end_frame ) {
        coded_image *image = self->source.source.funcs->getFrame( self->source.source.obj, self->current_frame );

        if( !image ) {
            // TODO: Report an error instead of ending the stream early
            self->current_frame = self->end_frame;
            continue;
        }

        x264_picture_t pict = {
            .i_pts = self->current_frame - self->start_frame,
            .img = {
                .i_csp = X264_CSP_I420,
                .i_plane = 3,
                .i_stride = { image->stride[0], image->stride[1], image->stride[2] },
                .plane = { image->data[0], image->data[1], image->data[2] },
            },
        };

        result = x264_encoder_encode( self->encoder, &nals, &nal_count, &pict, &pict_out );

        if( image->free_func )
            image->free_func( image );

        if( result < 0 ) {
            g_warning( "Failed in x264_encoder_encode." );
            return NULL;
        }

        self->current_frame++;

        if( result == 0 ) {
            g_debug( "No packets returned for frame %d, continuing...", self->current_frame );
            continue;
        }

        int size = 0;

        for( int i = 0; i < nal_count; i++ ) {
            size += nals[i].i_payload;
        }

        packet = g_slice_new0( codec_packet );
        packet->length = size;
        packet->data = g_slice_copy( size, nals[0].p_payload );
        packet->pts = pict_out.i_pts;
        packet->dts = pict_out.i_dts;
        packet->keyframe = pict_out.b_keyframe ? true : false;
        packet->free_func = (GFreeFunc) my_packet_free;

        return packet;
    }

    // Do we have frames waiting to come out of the encoder?
    if( x264_encoder_delayed_frames( self->encoder ) == 0 )
        return NULL;

    result = x264_encoder_encode( self->encoder, &nals, &nal_count, NULL, &pict_out );

    if( result < 0 ) {
        g_warning( "Failed in x264_encoder_encode at end of stream." );
        return NULL;
    }

    self->current_frame++;

    if( result == 0 ) {
        g_debug( "That's weird, they told us there were frames left." );
        return NULL;
    }

    int size = 0;

    for( int i = 0; i < nal_count; i++ ) {
        size += nals[i].i_payload;
    }

    packet = g_slice_new0( codec_packet );
    packet->length = size;
    packet->data = g_slice_copy( size, nals[0].p_payload );
    packet->pts = pict_out.i_pts;
    packet->dts = pict_out.i_dts;
    packet->keyframe = pict_out.b_keyframe ? true : false;
    packet->free_func = (GFreeFunc) my_packet_free;

    return packet;
}

static codec_packet_source_funcs source_funcs = {
    .getHeader = (codec_getHeaderFunc) X264VideoEncoder_get_header,
    .getNextPacket = (codec_getNextPacketFunc) X264VideoEncoder_get_next_packet,
};

static PyObject *pySourceFuncs;

static PyObject *
X264VideoEncoder_getFuncs( py_obj_X264VideoEncoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyObject *
X264VideoEncoder_get_progress( py_obj_X264VideoEncoder *self, void *closure ) {
    // TODO: Include delayed frames
    return PyLong_FromLong( self->current_frame - self->start_frame );
}

static PyObject *
X264VideoEncoder_get_progress_count( py_obj_X264VideoEncoder *self, void *closure ) {
    return PyLong_FromLong( self->end_frame - self->start_frame );
}

static PyGetSetDef X264VideoEncoder_getsetters[] = {
    { CODEC_PACKET_SOURCE_FUNCS, (getter) X264VideoEncoder_getFuncs, NULL, "Codec packet source C API." },
    { "progress", (getter) X264VideoEncoder_get_progress, NULL, "Encoder progress, from zero to progress_count." },
    { "progress_count", (getter) X264VideoEncoder_get_progress_count, NULL, "Number of items to complete. Compare to progress." },
    { NULL }
};

static PyMethodDef X264VideoEncoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_X264VideoEncoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.x264.X264VideoEncoder",
    .tp_basicsize = sizeof(py_obj_X264VideoEncoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodecPacketSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) X264VideoEncoder_dealloc,
    .tp_init = (initproc) X264VideoEncoder_init,
    .tp_getset = X264VideoEncoder_getsetters,
    .tp_methods = X264VideoEncoder_methods
};

void init_X264VideoEncoder( PyObject *module ) {
    if( PyType_Ready( &py_type_X264VideoEncoder ) < 0 )
        return;

    Py_INCREF( &py_type_X264VideoEncoder );
    PyModule_AddObject( module, "X264VideoEncoder", (PyObject *) &py_type_X264VideoEncoder );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODEC_PACKET_SOURCE_FUNCS, NULL );
}



