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
#include <libdv/dv.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "fluggo.media.libdv.DVAudioDecoder"

typedef struct {
    // Timestamp on input packet, which will be in video frames
    int64_t timestamp;

    // Sample at the start of the packet
    int64_t sample;
} timestamp_to_sample;

static void
destroy_tts( timestamp_to_sample *ptr ) {
    g_slice_free( timestamp_to_sample, ptr );
}

G_GNUC_PURE static gint
compare_tts( timestamp_to_sample *a, timestamp_to_sample *b, gpointer user_data ) {
    return a->timestamp - b->timestamp;
}

typedef struct {
    PyObject_HEAD

    CodecPacketSourceHolder source;
    dv_decoder_t *decoder;

    audio_frame input_frame;
    int16_t audio_buffers[4][DV_AUDIO_MAX_SAMPLES];
    float out_audio_buffers[DV_AUDIO_MAX_SAMPLES * 4];
    bool context_open, trust_timestamps;
    GSequence *timestamp_to_sample;
    int32_t max_sample_seen;

    int64_t current_pts;
    bool current_pts_valid;

    GStaticMutex mutex;
} py_obj_DVAudioDecoder;

static int
DVAudioDecoder_init( py_obj_DVAudioDecoder *self, PyObject *args, PyObject *kw ) {
    // Zero all pointers (so we know later what needs deleting)
    self->decoder = NULL;
    self->timestamp_to_sample = NULL;
    self->trust_timestamps = false;
    self->max_sample_seen = INT32_MIN;
    self->current_pts_valid = false;
    self->input_frame = (audio_frame) { NULL };

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

    // This is like trust_timestamps = false in the libav decoder,
    // except that timestamps on DV audio are near useless anyways
    self->timestamp_to_sample = g_sequence_new( (GDestroyNotify) destroy_tts );

    return 0;
}

static void
codec_packet_free( codec_packet **packet ) {
    if( *packet && (*packet)->free_func ) {
        (*packet)->free_func( *packet );
        *packet = NULL;
    }
}

static void
DVAudioDecoder_dealloc( py_obj_DVAudioDecoder *self ) {
    if( self->timestamp_to_sample ) {
        g_sequence_free( self->timestamp_to_sample );
        self->timestamp_to_sample = NULL;
    }

    py_codec_packet_take_source( NULL, &self->source );
    g_static_mutex_free( &self->mutex );

    dv_decoder_free( self->decoder );

    Py_TYPE(self)->tp_free( (PyObject*) self );
}

/*static void
convert_samples( float *out, int out_channels, int16_t *in, int in_channels, int offset, int duration ) {
    for( int sample = 0; sample < duration; sample++ ) {
        for( int channel = 0; channel < out_channels; channel++ ) {
            out[out_channels * sample + channel] =
                (channel < in_channels) ? (float)(in)[in_channels * (offset + sample) + channel] * (1.0f / INT16_MAX) : 0.0f;
        }
    }
}*/

static void
DVAudioDecoder_get_frame( py_obj_DVAudioDecoder *self, audio_frame *frame ) {
    g_static_mutex_lock( &self->mutex );

    // BJC: What does trust_timestamps mean? Well, some containers have timestamps
    // that are less precise than their sample rate. DV, for example, has timestamps
    // with a resolution of 1/30000, but a sample rate of 48000 Hz. The packet
    // source will do its best to convert the timestamps into samples, but it
    // does not have enough information to be accurate to the sample.
    //
    // That's fine for pure playback scenarios. We've got to be able to produce
    // frames from any point in the stream accurate to the sample. So, by default,
    // unless someone gives us permission to trust that the timestamps are accurate
    // with trust_timestamps, we make sure to decode our way through the stream
    // and build a map of what timestamps correspond to what actual samples.

    bool do_seek = false;
    frame->current_max_sample = -1;
    frame->current_min_sample = 0;

    // Seek to the spot if we need to (and if we can)
    if( self->source.source.funcs->seek ) {
        do_seek = true;

        if( self->current_pts_valid ) {
            // If our last read frame was before (but not too far before) the
            // current output frame, read up to it
            do_seek = frame->full_min_sample < self->current_pts ||
                frame->full_min_sample >= (self->current_pts + 10000);

            // But don't do it if our last frame contains the seek point
            if( do_seek &&
                    self->input_frame.data &&
                    self->input_frame.full_max_sample + 1 == self->current_pts &&
                    frame->full_min_sample >= self->input_frame.full_min_sample &&
                    frame->full_min_sample <= self->current_pts ) {
                g_debug( "Previous frame contains our read point" );
                do_seek = false;
            }

            if( do_seek )
                g_debug( "Looks like we need to seek..." );
        }

        if( do_seek && frame->full_min_sample > self->max_sample_seen ) {
            g_debug( "Skip seek, we don't trust the timestamps there yet" );
            do_seek = false;
        }

        if( do_seek ) {
            // Look up the target frame so we know exactly where to seek to
            int64_t target_frame = dv_system_50_fields( self->decoder ) ?
                (25 * (int64_t) frame->full_min_sample / dv_get_frequency( self->decoder )) :
                (30000 * (int64_t) frame->full_min_sample / (dv_get_frequency( self->decoder ) * 1001));

            if( target_frame != 0 )
                target_frame--;

            timestamp_to_sample tts = { .timestamp = target_frame };

            GSequenceIter *iter = g_sequence_lookup( self->timestamp_to_sample,
                &tts, (GCompareDataFunc) compare_tts, NULL );

            if( iter ) {
                timestamp_to_sample *ttsptr = g_sequence_get( iter );

                while( ttsptr->sample > frame->full_min_sample && !g_sequence_iter_is_begin( iter ) ) {
                    iter = g_sequence_iter_prev( iter );
                    ttsptr = g_sequence_get( iter );
                }

                iter = g_sequence_iter_next( iter );
                timestamp_to_sample *nexttts = g_sequence_get( iter );

                while( nexttts && nexttts->sample > frame->full_max_sample && !g_sequence_iter_is_end( iter ) ) {
                    ttsptr = nexttts;
                    iter = g_sequence_iter_next( iter );
                    nexttts = g_sequence_get( iter );
                }

                target_frame = ttsptr->timestamp;
            }
            else {
                g_warning( "Could not find timestamp lookup for correct seek to %d, seeking to %"PRId64" instead", frame->full_min_sample, target_frame );
            }

            g_debug( "Deciding to seek to frame %"PRId64", current sample %" PRId64 " vs. target sample %d", target_frame, self->current_pts, frame->full_min_sample );

            if( !self->source.source.funcs->seek( self->source.source.obj, target_frame ) ) {
                g_warning( "Failed to seek to frame %"PRId64, target_frame );
                g_static_mutex_unlock( &self->mutex );
                return;
            }

            self->current_pts_valid = false;
        }
    }

    // Use data from the last frame decoded, if we can
    if( self->input_frame.data && self->input_frame.full_min_sample <= frame->full_max_sample &&
        self->input_frame.full_max_sample >= frame->full_min_sample ) {

        // Decode into the current frame
        g_debug( "Using last frame from (%d->%d, %d->%d)",
            self->input_frame.full_min_sample, frame->full_min_sample,
            self->input_frame.full_max_sample, frame->full_max_sample );

        audio_overwrite_frame( frame, &self->input_frame, 0 );

        // We could be done...
        if( frame->current_min_sample == frame->full_min_sample && frame->current_max_sample == frame->full_max_sample ) {
            g_debug( "Going home early" );
            g_static_mutex_unlock( &self->mutex );
            return;
        }
    }

    codec_packet *packet = NULL;

    for( ;; ) {
        // Fetch the next packet
        codec_packet_free( &packet );

        packet = self->source.source.funcs->getNextPacket( self->source.source.obj );

        if( !packet ) {
            // End of stream!
            g_static_mutex_unlock( &self->mutex );
            return;
        }

        self->input_frame = (audio_frame) { NULL };

        g_debug( "Got packet start %"PRId64, packet->pts );

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
        int new_recording = dv_is_new_recording( self->decoder, (uint8_t *) packet->data );

        if( new_recording ) {
            g_log( G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "New recording at frame %"PRId64, packet->pts );
        }

        // Decode the data
        int16_t *bufptrs[4] = {
            &self->audio_buffers[0][0],
            &self->audio_buffers[1][0],
            &self->audio_buffers[2][0],
            &self->audio_buffers[3][0],
        };

        if( !dv_decode_full_audio( self->decoder, (uint8_t *) packet->data, bufptrs ) ) {
            g_warning( "Could not decode the audio at frame %"PRId64, packet->pts );
            self->current_pts_valid = false;

            int64_t target_sample = dv_system_50_fields( self->decoder ) ?
                (packet->pts * dv_get_frequency( self->decoder ) / 25) :
                (packet->pts * dv_get_frequency( self->decoder ) * 1001 / 30000);

            timestamp_to_sample dead_packet = {
                .timestamp = packet->pts,
                .sample = target_sample };

            GSequenceIter *dead_iter = g_sequence_lookup( self->timestamp_to_sample,
                &dead_packet, (GCompareDataFunc) compare_tts, NULL );

            if( !dead_iter ) {
                timestamp_to_sample *ttsptr = g_slice_dup( timestamp_to_sample, &dead_packet );
                g_sequence_insert_sorted( self->timestamp_to_sample,
                    ttsptr, (GCompareDataFunc) compare_tts, NULL );
            }

            continue;
        }

        int block_failure = self->decoder->audio->block_failure;
        int sample_failure = self->decoder->audio->sample_failure;

        if( block_failure || sample_failure ) {
            g_warning( "%d audio block failures at frame %"PRId64" (%d sample failures)", block_failure, packet->pts, sample_failure );
        }

        // Patch up timestamps if necessary
        int64_t packet_start = packet->pts;
        int packet_duration = dv_get_num_samples( self->decoder );

        g_debug( "Got duration %d for frame %"PRId64, packet_duration, packet_start );

        // Look up entry in our table
        timestamp_to_sample tts = {
            .timestamp = packet->pts };

        GSequenceIter *iter = g_sequence_lookup( self->timestamp_to_sample,
            &tts, (GCompareDataFunc) compare_tts, NULL );

        if( iter == NULL ) {
            // We don't have this entry, make one
            if( do_seek )
                g_warning( "Seek brought us to a point not covered in the timestamp table." );

            if( !do_seek && self->current_pts_valid && !new_recording ) {
                // We can trust our previous PTS
                packet_start = self->current_pts;
            }
            else {
                // We will use the PTS from the packet;
                // the idea is that this will only really stick for the first
                // packet, since on the next pass, we will override it.

                // We do have to try to turn it into a sample number, though
                // We assume that the frequency does not change throughout the file;
                // I don't feel like lifting that assumption

                if( dv_system_50_fields( self->decoder ) )
                    packet_start = packet_start * dv_get_frequency( self->decoder ) / 25;
                else
                    packet_start = packet_start * dv_get_frequency( self->decoder ) * 1001 / 30000;
            }

            tts.sample = packet_start;
            int64_t expected_sample = tts.timestamp * dv_get_frequency( self->decoder ) * 1001 / 30000;

            g_debug( "Adding to timestamp table %"PRId64" (%"PRId64"->%"PRId64", %s %"PRIdMAX")",
                tts.timestamp, expected_sample, tts.sample,
                (expected_sample == tts.sample) ? "even" :
                    ((expected_sample > tts.sample) ? "ahead" : "behind"),
                imaxabs(tts.sample - expected_sample) );

            timestamp_to_sample *ttsptr = g_slice_dup( timestamp_to_sample, &tts );

            g_sequence_insert_sorted( self->timestamp_to_sample,
                ttsptr, (GCompareDataFunc) compare_tts, NULL );
        }
        else {
            // This packet is covered, let's use the start and duration we recorded
            timestamp_to_sample *ttsptr = g_sequence_get( iter );

            packet_start = ttsptr->sample;

            g_debug( "Re-substituting start %"PRId64" from timestamp table", packet_start );
        }

        // After this, we haven't seeked to this spot anymore
        do_seek = false;

        int dv_channels = dv_get_num_channels( self->decoder );
        self->input_frame = (audio_frame) {
            .data = self->out_audio_buffers,
            .channels = 4,
            .current_min_sample = packet_start,
            .full_min_sample = packet_start,
            .current_max_sample = packet_start + packet_duration - 1,
            .full_max_sample = packet_start + packet_duration - 1,
        };

        for( int i = 0; i < packet_duration; i++ ) {
            for( int channel = 0; channel < 4; channel++ ) {
                *audio_get_sample( &self->input_frame, packet_start + i, channel ) =
                    (channel < dv_channels) ? ((float) self->audio_buffers[channel][i] / INT16_MAX) : 0.0f;
            }
        }

        self->current_pts = packet_start + packet_duration;
        self->current_pts_valid = true;
        self->max_sample_seen = max(self->max_sample_seen, packet_start + packet_duration - 1);

        g_debug( "We'll take that (%"PRId64", %"PRId64")", packet_start, packet_start + packet_duration - 1 );

        audio_overwrite_frame( frame, &self->input_frame, 0 );

        if( self->input_frame.full_max_sample >= frame->full_max_sample ) {
            g_debug( "Enough: (%d, %d) vs (%d, %d)", frame->current_min_sample, frame->current_max_sample, frame->full_min_sample, frame->full_max_sample );
            g_static_mutex_unlock( &self->mutex );
            return;
        }
        else {
            g_debug( "Not enough: (%d, %d) vs (%d, %d)", frame->current_min_sample, frame->current_max_sample, frame->full_min_sample, frame->full_max_sample );
        }
    }

    // Error return
    g_static_mutex_unlock( &self->mutex );
    codec_packet_free( &packet );

    return;
}

static AudioFrameSourceFuncs source_funcs = {
    .getFrame = (audio_getFrameFunc) DVAudioDecoder_get_frame,
};

static PyObject *pySourceFuncs;

static PyObject *
DVAudioDecoder_getFuncs( py_obj_DVAudioDecoder *self, void *closure ) {
    Py_INCREF(pySourceFuncs);
    return pySourceFuncs;
}

static PyGetSetDef DVAudioDecoder_getsetters[] = {
    { AUDIO_FRAME_SOURCE_FUNCS, (getter) DVAudioDecoder_getFuncs, NULL, "Audio frame source C API." },
    { NULL }
};

static PyMethodDef DVAudioDecoder_methods[] = {
    { NULL }
};

static PyTypeObject py_type_DVAudioDecoder = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fluggo.media.libdv.DVAudioDecoder",
    .tp_basicsize = sizeof(py_obj_DVAudioDecoder),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_base = &py_type_AudioSource,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) DVAudioDecoder_dealloc,
    .tp_init = (initproc) DVAudioDecoder_init,
    .tp_getset = DVAudioDecoder_getsetters,
    .tp_methods = DVAudioDecoder_methods
};

bool init_DVAudioDecoder( PyObject *module ) {
    if( PyType_Ready( &py_type_DVAudioDecoder ) < 0 )
        return false;

    Py_INCREF( &py_type_DVAudioDecoder );
    PyModule_AddObject( module, "DVAudioDecoder", (PyObject *) &py_type_DVAudioDecoder );

    pySourceFuncs = PyCapsule_New( &source_funcs, AUDIO_FRAME_SOURCE_FUNCS, NULL );
    return true;
}



