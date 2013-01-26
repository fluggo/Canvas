/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2013 Brian J. Crowell <brian@fluggo.com>

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

/*
    BJC: Future codewalkers, you're welcome to come in and patch this to working.
    It's a good part of the ways there, but what I didn't understand about the
    thoroughly undocumented libdv project is that the output is always 4:2:2 YUY2--
    even though it doesn't appear to have support for DVCPRO50. This means it's
    taking 4:1:1 inputs and *doubling* the chroma pixels to make up space. The
    code shows it also doing specious things with little/big-endian systems.

    Since I'm looking at libdv for the extra data pack and audio support, I'll
    skip video support from this library for now.
*/
#if 0

#include "pyframework.h"
#include <libdv/dv.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.libdv.DVVideoDecoder"

typedef struct {
    PyObject_HEAD

    CodecPacketSourceHolder source;
    dv_decoder_t *decoder;
    int64_t next_frame;

    GStaticMutex mutex;
} py_obj_DVVideoDecoder;

static int
DVVideoDecoder_init( py_obj_DVVideoDecoder *self, PyObject *args, PyObject *kw ) {
    // Zero all pointers (so we know later what needs deleting)
    self->decoder = NULL;
    self->source = (CodecPacketSourceHolder) {{ NULL }};

    PyObject *source_obj;

    static char *kwlist[] = { "source", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kw, "O", kwlist, &source_obj ) )
        return -1;

    if( !py_codec_packet_take_source( source_obj, &self->source ) )
        return -1;

    g_static_mutex_init( &self->mutex );

    if( !(self->decoder = dv_decoder_new( FALSE, FALSE, FALSE )) ) {
        PyErr_NoMemory();
        return -1;
    }

    self->next_frame = 0;

    return 0;
}

static void
DVVideoDecoder_dealloc( py_obj_DVVideoDecoder *self ) {
    py_codec_packet_take_source( NULL, &self->source );
    g_static_mutex_free( &self->mutex );

    dv_decoder_free( self->decoder );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

static coded_image *
DVVideoDecoder_get_frame( py_obj_DVVideoDecoder *self, int frame, int quality_hint ) {
    g_static_mutex_lock( &self->mutex );

    if( self->source.source.funcs->seek && frame != self->next_frame ) {
        if( !self->source.source.funcs->seek( self->source.source.obj, frame ) ) {
            g_static_mutex_unlock( &self->mutex );
            return NULL;
        }
    }

    self->next_frame = frame;
    codec_packet *packet = NULL;

    for( ;; ) {
        if( packet && packet->free_func ) {
            packet->free_func( packet );
            packet = NULL;
        }

        packet = self->source.source.funcs->getNextPacket( self->source.source.obj );

        if( !packet )
            break;

        if( packet->pts < frame ) {
            g_debug( "Too early (%" PRId64 " vs %d)", packet->pts, frame );
            continue;
        }

        // Packet length should be 120000 for NTSC, 144000 for PAL
        if( packet->length < 120000 ) {
            g_warning( "Packet for DV frame too short; expected 120000 bytes but got %d.", packet->length );
            break;
        }

        // Check for system changes
        int result = dv_parse_header( self->decoder, (uint8_t *) packet->data );

        if( result == -1 ) {
            g_warning( "Error parsing header in DV frame." );
            break;
        }

        // Do double-checks on the packet length
        if( dv_is_PAL( self->decoder ) ) {
            if( packet->length < 144000 ) {
                g_warning( "Discarding DV frame; detected PAL frame, but packet length was %d (should be 144000).", packet->length );
                break;
            }
            else if( packet->length > 144000 ) {
                g_warning( "Detected PAL DV frame, but packet length was %d (should be 144000).", packet->length );
            }
        }
        else if( packet->length > 120000 ) {
            g_warning( "Detected NTSC DV frame, but packet length was %d (should be 120000).", packet->length );
        }

        // Parse additional packs; this would be needed for timecode
        dv_parse_packs( self->decoder, (uint8_t *) packet->data );

        // Now produce video
        int ywidth = 720, yheight;

        if( dv_system_50_fields( self->decoder ) )
            yheight = 576;
        else
            yheight = 480;

        int cwidth = ywidth;
        int cheight = yheight;

        if( self->decoder->sampling == e_dv_sample_411 ) {
            cwidth >>= 2;
        }
        else if( self->decoder->sampling == e_dv_sample_422 ) {
            cwidth >>= 1;
        }
        else if( self->decoder->sampling == e_dv_sample_420 ) {
            cheight >>= 1;
            cwidth >>= 1;
        }
        else {
            g_warning( "Invalid sampling type." );
            break;
        }

        switch( quality_hint ) {
            // Monochrome DC components
            case 1:
                dv_set_quality( self->decoder, DV_QUALITY_DC );
                break;

            // Monochrome AC 1 components
            case 2:
                dv_set_quality( self->decoder, DV_QUALITY_AC_1 );
                break;

            // Monochrome AC 2 components
            case 3:
                dv_set_quality( self->decoder, DV_QUALITY_AC_2 );
                break;

            // Color AC_2 components
            default:
                dv_set_quality( self->decoder, DV_QUALITY_AC_2 | DV_QUALITY_COLOR );
                break;
        }

        // libdv is not like libavcodec, which produces planar Y'CbCr.
        // libdv produces YUY2, which is Y' Cb Y' Cr Y' Cb Y' Cr.
        int strides[1] = { ywidth, cwidth, cwidth };
        int line_counts[3] = { yheight, cheight, cheight };
        coded_image *image = coded_image_alloc( strides, line_counts, 3 );

        dv_decode_full_frame( self->decoder, (uint8_t *) packet->data,
            e_dv_color_yuv, (uint8_t **) image->data, strides );

        self->next_frame = packet->pts + 1;

        if( packet && packet->free_func ) {
            packet->free_func( packet );
            packet = NULL;
        }

        g_static_mutex_unlock( &self->mutex );

        return image;
    }

    // Error return
    g_static_mutex_unlock( &self->mutex );

    if( packet && packet->free_func ) {
        packet->free_func( packet );
        packet = NULL;
    }

    return NULL;
}

static coded_image_source_funcs source_funcs = {
    .getFrame = (coded_image_getFrameFunc) DVVideoDecoder_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
DVVideoDecoder_getFuncs( py_obj_DVVideoDecoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef DVVideoDecoder_getsetters[] = {
    { CODED_IMAGE_SOURCE_FUNCS, (getter) DVVideoDecoder_getFuncs, NULL, "Coded image source C API." },
    { NULL }
};

static PyMethodDef DVVideoDecoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_DVVideoDecoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libdv.DVVideoDecoder",
    .tp_basicsize = sizeof(py_obj_DVVideoDecoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_CodedImageSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) DVVideoDecoder_dealloc,
    .tp_init = (initproc) DVVideoDecoder_init,
    .tp_getset = DVVideoDecoder_getsetters,
    .tp_methods = DVVideoDecoder_methods
};

void init_DVVideoDecoder( PyObject *module ) {
    if( PyType_Ready( &py_type_DVVideoDecoder ) < 0 )
        return;

    Py_INCREF( &py_type_DVVideoDecoder );
    PyModule_AddObject( module, "DVVideoDecoder", (PyObject *) &py_type_DVVideoDecoder );

    pySourceFuncs = PyCapsule_New( &source_funcs,
        CODED_IMAGE_SOURCE_FUNCS, NULL );
}

#endif

