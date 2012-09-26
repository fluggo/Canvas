/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2012 Brian J. Crowell <brian@fluggo.com>

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
#include <faac.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.faac.AACAudioEncoder"

typedef struct {
    PyObject_HEAD

    unsigned long input_samples, max_output_bytes;
    int start_sample, current_sample, end_sample, channels,
        next_output_sample;

    AudioSourceHolder source;
    faacEncHandle encoder;
} py_obj_AACAudioEncoder;

static int
AACAudioEncoder_init( py_obj_AACAudioEncoder *self, PyObject *args, PyObject *kw ) {
    self->source.csource = NULL;
    self->encoder = NULL;
    self->next_output_sample = 0;

    PyObject *source_obj, *adts_obj = NULL;
    int sample_rate, bitrate_per_channel = -1;
    unsigned long input_samples;

    static char *kwlist[] = { "source", "start_sample", "end_sample",
        "sample_rate", "channels", "bitrate_per_channel", "wrap_adts", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "Oiiii|iO", kwlist, &source_obj,
            &self->start_sample, &self->end_sample,
            &sample_rate, &self->channels, &bitrate_per_channel, &adts_obj ) )
        return -1;

    self->current_sample = self->start_sample;

    if( sample_rate <= 0 ) {
        PyErr_Format( PyExc_ValueError, "sample_rate needs to be positive." );
        return -1;
    }

    if( self->channels <= 0 ) {
        PyErr_Format( PyExc_ValueError, "channels needs to be positive." );
        return -1;
    }

    if( !py_audio_take_source( source_obj, &self->source ) )
        return -1;

    self->encoder = faacEncOpen( sample_rate, self->channels,
        &input_samples, &self->max_output_bytes );

    if( !self->encoder ) {
        PyErr_Format( PyExc_Exception, "Failed to open libfaac encoder." );
        return -1;
    }

    self->input_samples = input_samples / self->channels;

    faacEncConfigurationPtr config =
        faacEncGetCurrentConfiguration( self->encoder );

    config->inputFormat = FAAC_INPUT_FLOAT;
    config->mpegVersion = MPEG4;
    config->aacObjectType = MAIN;
    config->outputFormat = 0;       // WE do not do ADTS by default

    if( bitrate_per_channel != -1 )
        config->bitRate = bitrate_per_channel;

    if( adts_obj && PyObject_IsTrue( adts_obj ) )
        config->outputFormat = 1;

    if( !faacEncSetConfiguration( self->encoder, config ) ) {
        PyErr_Format( PyExc_Exception, "Failed to set libfaac encoder configuration." );
        return -1;
    }

    return 0;
}

static void
AACAudioEncoder_dealloc( py_obj_AACAudioEncoder *self ) {
    if( self->encoder )
        faacEncClose( self->encoder );

    py_audio_take_source( NULL, &self->source );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

typedef struct {
    codec_packet packet;
    int max_output_bytes;
} my_codec_packet;

static void
my_packet_free( my_codec_packet *packet ) {
    g_assert(packet);
    g_slice_free1( packet->max_output_bytes, packet->packet.data );
    g_slice_free( my_codec_packet, packet );
}

static int
AACAudioEncoder_get_header( py_obj_AACAudioEncoder *self, void *buffer ) {
    if( !self->encoder ) {
        g_warning( "get_header called, but encoder is invalid." );
        return 0;
    }

    uint8_t *received_buffer = NULL;
    unsigned long size = 0;

    int result = faacEncGetDecoderSpecificInfo( self->encoder, &received_buffer, &size );

    if( result < 0 ) {
        g_warning( "faacEncGetDecoderSpecificInfo failed" );
        return 0;
    }

    if( !buffer ) {
        free( received_buffer );
        return size;
    }

    memcpy( buffer, received_buffer, size );
    free( received_buffer );
    return size;
}

static codec_packet *
AACAudioEncoder_get_next_packet( py_obj_AACAudioEncoder *self ) {
    my_codec_packet *packet;
    int result;

    uint8_t *data = g_slice_alloc( self->max_output_bytes );

    while( self->current_sample <= self->end_sample ) {
        audio_frame frame = {
            .data = g_slice_alloc( sizeof(float) * self->channels * self->input_samples ),
            .channels = self->channels,
            .full_min_sample = self->current_sample,
            .full_max_sample = min(self->current_sample + self->input_samples - 1, self->end_sample),
        };

        int sample_count = frame.full_max_sample - frame.full_min_sample + 1;

        audio_get_frame( &self->source.source, &frame );

        g_debug( "Current min/max: %i %i (full %i %i)",
            frame.current_min_sample, frame.current_max_sample,
            frame.full_min_sample, frame.full_max_sample );

        // It doesn't matter if no data came back; we are honor-bound to encode
        // silence if there's no data
        if( frame.current_min_sample > frame.current_max_sample ) {
            // Silence
            for( int i = 0; i < sample_count * self->channels; i++ )
                frame.data[i] = 0.0f;
        }
        else {
            // Silence area before and after valid region
            int last_silence_sample = frame.current_min_sample - frame.full_min_sample - 1;

            for( int i = 0; i < last_silence_sample * self->channels; i++ )
                frame.data[i] = 0.0f;

            int first_silence_sample = frame.current_max_sample - frame.full_min_sample + 1;

            for( int i = first_silence_sample * self->channels;
                    i < sample_count * self->channels; i++ )
                frame.data[i] = 0.0f;
        }

        // libfaac expects float data at 16-bit levels
        for( int i = (frame.current_min_sample - frame.full_min_sample) * self->channels;
                i < (frame.current_max_sample - frame.full_min_sample + 1) * self->channels; i++ ) {
            frame.data[i] *= (float) 0x7FFF;
        }

        result = faacEncEncode( self->encoder, (int32_t*)(void*) frame.data,
            sample_count * self->channels,
            data, self->max_output_bytes );

        g_slice_free1( sizeof(float) * self->channels * self->input_samples, frame.data );

        if( result < 0 ) {
            g_warning( "Failed in faacEncEncode." );
            g_slice_free1( self->max_output_bytes, data );
            return NULL;
        }

        self->current_sample += self->input_samples;

        if( result == 0 ) {
            g_debug( "No packets returned for [%d, %d], continuing...", frame.full_min_sample, frame.full_max_sample );
            continue;
        }

        packet = g_slice_new0( my_codec_packet );
        packet->packet.length = result;
        packet->packet.data = data;
        packet->packet.pts = self->next_output_sample;
        packet->packet.dts = self->next_output_sample;  // BJC: Beats me
        packet->packet.keyframe = true;
        packet->packet.free_func = (GFreeFunc) my_packet_free;
        packet->packet.duration = self->input_samples;
        packet->max_output_bytes = self->max_output_bytes;

        self->next_output_sample += self->input_samples;

        return (codec_packet *) packet;
    }

    // Do we have frames waiting to come out of the encoder?
    result = faacEncEncode( self->encoder, NULL, 0, data, self->max_output_bytes );

    if( result == 0 )
        return NULL;

    if( result < 0 ) {
        g_warning( "Failed in faacEncEncode at end of stream." );
        return NULL;
    }

    if( result == 0 ) {
        g_debug( "That's weird, they told us there were frames left." );
        return NULL;
    }

    packet = g_slice_new0( my_codec_packet );
    packet->packet.length = result;
    packet->packet.data = data;
    packet->packet.pts = self->next_output_sample;
    packet->packet.dts = self->next_output_sample;  // BJC: Beats me
    packet->packet.keyframe = true;
    packet->packet.free_func = (GFreeFunc) my_packet_free;
    // TODO: Set duration
    packet->max_output_bytes = self->max_output_bytes;

    self->next_output_sample += self->input_samples;

    return (codec_packet *) packet;
}

static codec_packet_source_funcs source_funcs = {
    .getHeader = (codec_getHeaderFunc) AACAudioEncoder_get_header,
    .getNextPacket = (codec_getNextPacketFunc) AACAudioEncoder_get_next_packet,
};

static PyObject *pySourceFuncs;

static PyObject *
AACAudioEncoder_getFuncs( py_obj_AACAudioEncoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyObject *
AACAudioEncoder_get_progress( py_obj_AACAudioEncoder *self, void *closure ) {
    return PyLong_FromLong( self->next_output_sample - self->start_sample );
}

static PyObject *
AACAudioEncoder_get_progress_count( py_obj_AACAudioEncoder *self, void *closure ) {
    return PyLong_FromLong( self->end_sample - self->start_sample + 1 );
}

static PyGetSetDef AACAudioEncoder_getsetters[] = {
    { CODEC_PACKET_SOURCE_FUNCS, (getter) AACAudioEncoder_getFuncs, NULL, "Codec packet source C API." },
    { "progress", (getter) AACAudioEncoder_get_progress, NULL, "Encoder progress, from zero to progress_count." },
    { "progress_count", (getter) AACAudioEncoder_get_progress_count, NULL, "Number of items to complete. Compare to progress." },
    { NULL }
};

static PyMethodDef AACAudioEncoder_methods[] = {
    { NULL }
};

static PyMemberDef AACAudioEncoder_members[] = {
    { NULL }
};

static PyTypeObject py_type_AACAudioEncoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.faac.AACAudioEncoder",
    .tp_basicsize = sizeof(py_obj_AACAudioEncoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodecPacketSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) AACAudioEncoder_dealloc,
    .tp_init = (initproc) AACAudioEncoder_init,
    .tp_getset = AACAudioEncoder_getsetters,
    .tp_methods = AACAudioEncoder_methods,
    .tp_members = AACAudioEncoder_members,
};

void init_AACAudioEncoder( PyObject *module ) {
    if( PyType_Ready( &py_type_AACAudioEncoder ) < 0 )
        return;

    Py_INCREF( &py_type_AACAudioEncoder );
    PyModule_AddObject( module, "AACAudioEncoder", (PyObject *) &py_type_AACAudioEncoder );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODEC_PACKET_SOURCE_FUNCS, NULL );
}



