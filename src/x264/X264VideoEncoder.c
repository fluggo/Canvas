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
#include <structmember.h>
#include <x264.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.x264.X264VideoEncoder"

typedef struct {
    PyObject_HEAD;
    x264_param_t params;
} py_obj_X264EncoderParams;

static int
X264EncoderParams_init( py_obj_X264EncoderParams *self, PyObject *args, PyObject *kw ) {
    PyObject *frame_rate_obj = NULL, *sar_obj = NULL, *timebase_obj = NULL,
        *annexb_obj = NULL, *repeat_headers_obj = NULL, *interlaced_obj = NULL, *cabac_obj = NULL;
    int width = -1, height = -1, qp = -1, bitrate = -1, max_bitrate = -1;
    float crf = -1.0f;
    const char *preset = NULL, *tune = NULL;

    static char *kwlist[] = { "preset", "tune", "frame_rate", "sample_aspect_ratio",
        "timebase", "width", "height", "constant_ratefactor", "constant_quantizer",
        "bitrate", "vbv_max_bitrate", "annex_b", "repeat_headers", "interlaced", "cabac", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "|ssOOOiifiiiOOOO", kwlist, &preset, &tune,
            &frame_rate_obj, &sar_obj, &timebase_obj, &width, &height, &crf, &qp,
            &bitrate, &max_bitrate, &annexb_obj, &repeat_headers_obj, &interlaced_obj, &cabac_obj ) )
        return -1;

    // Parse and validate the arguments
    if( x264_param_default_preset( &self->params, preset, tune ) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to set x264 presets." );
        return -1;
    }

    // We don't do variable frame rates
    self->params.b_vfr_input = 0;

    if( frame_rate_obj ) {
        rational frame_rate;

        if( !py_parse_rational( frame_rate_obj, &frame_rate ) )
            return -1;

        self->params.i_fps_num = frame_rate.n;
        self->params.i_fps_den = frame_rate.d;
        self->params.i_timebase_num = frame_rate.d;
        self->params.i_timebase_den = frame_rate.n;
    }

    if( timebase_obj ) {
        rational timebase;

        if( !py_parse_rational( timebase_obj, &timebase ) )
            return -1;

        self->params.i_timebase_num = timebase.n;
        self->params.i_timebase_den = timebase.d;
    }

    if( sar_obj ) {
        rational sar;

        if( !py_parse_rational( sar_obj, &sar ) )
            return -1;

        self->params.vui.i_sar_width = sar.n;
        self->params.vui.i_sar_height = sar.d;
    }

    if( width != -1 )
        self->params.i_width = width;

    if( height != -1 )
        self->params.i_height = height;

    // Ratecontrol defaults
    if( crf != -1.0f ) {
        // Constant ratefactor
        self->params.rc.i_rc_method = X264_RC_CRF;
        self->params.rc.f_rf_constant = crf;
    }

    if( qp != -1 ) {
        // Constant quantizer
        self->params.rc.i_rc_method = X264_RC_CQP;
        self->params.rc.i_qp_constant = qp;
    }

    if( bitrate != -1 ) {
        self->params.rc.i_rc_method = X264_RC_ABR;
        self->params.rc.i_bitrate = bitrate;
    }

    // VBV
    if( max_bitrate != -1 ) {
        self->params.rc.i_vbv_max_bitrate = max_bitrate;
    }

    if( interlaced_obj )
        self->params.b_interlaced = PyObject_IsTrue( interlaced_obj );

    if( annexb_obj )
        self->params.b_annexb = PyObject_IsTrue( annexb_obj );

    if( repeat_headers_obj )
        self->params.b_repeat_headers = PyObject_IsTrue( repeat_headers_obj );

    self->params.b_cabac = 1;

    if( cabac_obj )
        self->params.b_cabac = PyObject_IsTrue( cabac_obj );

    // For the moment, these can't be changed
    self->params.i_csp = X264_CSP_I420;
    self->params.vui.i_overscan = 2;      // yes overscan
    self->params.vui.i_vidformat = 2;     // NTSC
    self->params.vui.b_fullrange = 0;     // Studio level encoding
    self->params.vui.i_colorprim = 1;     // Rec. 709 primaries
    self->params.vui.i_transfer = 1;      // Rec. 709 transfer
    self->params.vui.i_colmatrix = 1;     // Rec. 709 matrix (incidentally, 0 encodes RGB)
    self->params.vui.i_chroma_loc = 0;    // MPEG2-style chroma siting
    // Specify max VBV bitrate here

    return 0;
}

static PyObject *
X264EncoderParams_apply_fast_first_pass( py_obj_X264EncoderParams *self, PyObject *args ) {
    x264_param_apply_fastfirstpass( &self->params );
    Py_RETURN_NONE;
}

static PyObject *
X264EncoderParams_apply_profile( py_obj_X264EncoderParams *self, PyObject *args ) {
    const char *profile;

    if( !PyArg_ParseTuple( args, "s", &profile ) )
        return NULL;

    x264_param_apply_profile( &self->params, profile );
    Py_RETURN_NONE;
}

static void
X264EncoderParams_dealloc( py_obj_X264EncoderParams *self ) {
    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static PyMethodDef X264EncoderParams_methods[] = {
    { "apply_fast_first_pass", (PyCFunction) X264EncoderParams_apply_fast_first_pass, METH_NOARGS,
        "apply_fast_first_pass(): Sets faster settings if this is a first pass." },
    { "apply_profile", (PyCFunction) X264EncoderParams_apply_profile, METH_VARARGS,
        "apply_profile(profile): Limits the settings to those in the specified profile, which is a string." },
    { NULL }
};

static PyTypeObject py_type_X264EncoderParams = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.x264.X264EncoderParams",
    .tp_basicsize = sizeof(py_obj_X264EncoderParams),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodecPacketSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) X264EncoderParams_dealloc,
    .tp_init = (initproc) X264EncoderParams_init,
    //.tp_getset = X264VideoEncoder_getsetters,
    .tp_methods = X264EncoderParams_methods,
    //.tp_members = X264VideoEncoder_members,
};

typedef struct {
    PyObject_HEAD

    int start_frame, end_frame, current_frame;
    CodedImageSourceHolder source;
    x264_t *encoder;

    PyObject *sps, *pps, *sei;
} py_obj_X264VideoEncoder;

static int
X264VideoEncoder_init( py_obj_X264VideoEncoder *self, PyObject *args, PyObject *kw ) {
    self->source.csource = NULL;
    self->sps = NULL;
    self->pps = NULL;
    self->sei = NULL;

    PyObject *source_obj;
    py_obj_X264EncoderParams *params_obj;

    static char *kwlist[] = { "source", "start_frame", "end_frame",
        "params", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "OiiO!", kwlist, &source_obj,
            &self->current_frame, &self->end_frame,
            &py_type_X264EncoderParams, &params_obj ) )
        return -1;

    self->start_frame = self->current_frame;
    x264_param_t params = params_obj->params;

    params.i_frame_total = (self->end_frame - self->start_frame + 1);
    //params.b_annexb = 0;            // 1=Suitable for standalone file
    //params.b_repeat_headers = 0;

    if( !py_coded_image_take_source( source_obj, &self->source ) )
        return -1;

    self->encoder = x264_encoder_open( &params );

    // Grab the headers here; we're copying x264's example on the IDs
    x264_nal_t *nals;
    int count;

    if( x264_encoder_headers( self->encoder, &nals, &count ) < 0 ) {
        PyErr_Format( PyExc_Exception, "Failed to fetch headers." );
        return -1;
    }

    self->sps = PyBytes_FromStringAndSize( (char*) nals[0].p_payload, nals[0].i_payload );
    self->pps = PyBytes_FromStringAndSize( (char*) nals[1].p_payload, nals[1].i_payload );
    self->sei = PyBytes_FromStringAndSize( (char*) nals[2].p_payload, nals[2].i_payload );

    return 0;
}

static void
X264VideoEncoder_dealloc( py_obj_X264VideoEncoder *self ) {
    if( self->encoder )
        x264_encoder_close( self->encoder );

    py_coded_image_take_source( NULL, &self->source );
    Py_CLEAR( self->sei );
    Py_CLEAR( self->pps );
    Py_CLEAR( self->sps );

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

    while( self->current_frame <= self->end_frame ) {
        coded_image *image = self->source.source.funcs->getFrame( self->source.source.obj, self->current_frame, 0 );

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
        packet->duration = 1;
        packet->keyframe = pict_out.b_keyframe ? true : false;
        packet->discardable = pict_out.i_type == X264_TYPE_B;
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
    packet->duration = 1;
    packet->keyframe = pict_out.b_keyframe ? true : false;
    packet->discardable = pict_out.i_type == X264_TYPE_B;
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

static PyMemberDef X264VideoEncoder_members[] = {
    { "pps", T_OBJECT_EX, G_STRUCT_OFFSET(py_obj_X264VideoEncoder, pps), READONLY, NULL },
    { "sps", T_OBJECT_EX, G_STRUCT_OFFSET(py_obj_X264VideoEncoder, sps), READONLY, NULL },
    { "sei", T_OBJECT_EX, G_STRUCT_OFFSET(py_obj_X264VideoEncoder, sei), READONLY, NULL },
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
    .tp_methods = X264VideoEncoder_methods,
    .tp_members = X264VideoEncoder_members,
};

static void
_make_name_list( PyObject *module, const char *varname, const char * const *names ) {
    g_assert( module );
    g_assert( varname );
    g_assert( names );

    PyObject *list = PyList_New( 0 );

    for( int i = 0; names[i] != NULL; i++ ) {
        PyObject *str = PyUnicode_FromString( names[i] );
        PyList_Append( list, str );
        Py_DECREF( str );
    }

    PyModule_AddObject( module, varname, list );
}

void init_X264VideoEncoder( PyObject *module ) {
    if( PyType_Ready( &py_type_X264VideoEncoder ) < 0 )
        return;

    Py_INCREF( &py_type_X264VideoEncoder );
    PyModule_AddObject( module, "X264VideoEncoder", (PyObject *) &py_type_X264VideoEncoder );

    if( PyType_Ready( &py_type_X264EncoderParams ) < 0 )
        return;

    Py_INCREF( &py_type_X264EncoderParams );
    PyModule_AddObject( module, "X264EncoderParams", (PyObject *) &py_type_X264EncoderParams );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODEC_PACKET_SOURCE_FUNCS, NULL );

    _make_name_list( module, "preset_names", x264_preset_names );
    _make_name_list( module, "tune_names", x264_tune_names );
    _make_name_list( module, "profile_names", x264_profile_names );
}



