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

#include "framework.h"

EXPORT int64_t
get_frame_time( const rational *frameRate, int frame ) {
    return ((int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d)) / (int64_t)(frameRate->n) + INT64_C(1);
}

EXPORT int
get_time_frame( const rational *frameRate, int64_t time ) {
    return (time * (int64_t)(frameRate->n)) / (INT64_C(1000000000) * (int64_t)(frameRate->d));
}

EXPORT void video_get_frame_f16( video_source *source, int frame_index, rgba_frame_f16 *frame ) {
    if( !source || !source->funcs ) {
        box2i_set_empty( &frame->current_window );
        return;
    }

    if( source->funcs->get_frame ) {
        source->funcs->get_frame( source->obj, frame_index, frame );
    }
    else if( source->funcs->get_frame_32 ) {
        // Allocate a new frame
        rgba_frame_f32 temp_frame;
        v2i size;

        box2i_get_size( &frame->full_window, &size );
        temp_frame.data = g_slice_alloc( sizeof(rgba_f32) * size.x * size.y );
        temp_frame.full_window = frame->full_window;
        temp_frame.current_window = frame->full_window;

        source->funcs->get_frame_32( source->obj, frame_index, &temp_frame );

        if( !box2i_is_empty( &temp_frame.current_window ) ) {
            // Convert to f16
            int countX = temp_frame.current_window.max.x - temp_frame.current_window.min.x + 1;

            if( countX > 0 ) {
                for( int y = temp_frame.current_window.min.y; y <= temp_frame.current_window.max.y; y++ ) {
                    rgba_f32_to_f16(
                        video_get_pixel_f16( frame, temp_frame.current_window.min.x, y ),
                        video_get_pixel_f32( &temp_frame, temp_frame.current_window.min.x, y ),
                        countX );
                }
            }
        }

        frame->current_window = temp_frame.current_window;

        g_slice_free1( sizeof(rgba_f32) * size.x * size.y, temp_frame.data );
    }
    else if( source->funcs->get_frame_gl ) {
        // Load a GL texture, then transfer it to memory
        gl_ensure_context();

        rgba_frame_gl temp_frame = { .full_window = frame->full_window };
        source->funcs->get_frame_gl( source->obj, frame_index, &temp_frame );

        // Default pixel store values should work just fine
        glGetTexImage( GL_TEXTURE_RECTANGLE, 0, GL_RGBA, GL_HALF_FLOAT_ARB, frame->data );
        glDeleteTextures( 1, &temp_frame.texture );

        frame->current_window = temp_frame.current_window;
    }
    else {
        box2i_set_empty( &frame->current_window );
    }
}

EXPORT void video_get_frame_f32( video_source *source, int frame_index, rgba_frame_f32 *frame ) {
    if( !source || !source->funcs ) {
        box2i_set_empty( &frame->current_window );
        return;
    }

    if( source->funcs->get_frame_32 ) {
        source->funcs->get_frame_32( source->obj, frame_index, frame );
    }
    else if( source->funcs->get_frame ) {
        // Allocate a new frame
        rgba_frame_f16 temp_frame;
        v2i size;

        box2i_get_size( &frame->full_window, &size );
        temp_frame.data = g_slice_alloc( sizeof(rgba_f16) * size.x * size.y );
        temp_frame.full_window = frame->full_window;
        temp_frame.current_window = frame->full_window;

        source->funcs->get_frame( source->obj, frame_index, &temp_frame );

        // Convert to f32
        int countX = temp_frame.current_window.max.x - temp_frame.current_window.min.x + 1;

        for( int y = temp_frame.current_window.min.y; y <= temp_frame.current_window.max.y; y++ ) {
            rgba_f16_to_f32(
                video_get_pixel_f32( frame, temp_frame.current_window.min.x, y ),
                video_get_pixel_f16( &temp_frame, temp_frame.current_window.min.x, y ),
                countX );
        }

        frame->current_window = temp_frame.current_window;

        g_slice_free1( sizeof(rgba_f16) * size.x * size.y, temp_frame.data );
    }
    else if( source->funcs->get_frame_gl ) {
        // Load a GL texture, then transfer it to memory
        gl_ensure_context();

        rgba_frame_gl temp_frame = { .full_window = frame->full_window };
        source->funcs->get_frame_gl( source->obj, frame_index, &temp_frame );

        // Default pixel store values should work just fine
        glGetTexImage( GL_TEXTURE_RECTANGLE, 0, GL_RGBA, GL_FLOAT, frame->data );
        glDeleteTextures( 1, &temp_frame.texture );

        frame->current_window = temp_frame.current_window;
    }
    else {
        box2i_set_empty( &frame->current_window );
    }
}


/***** Audio support ***/

static void
audio_frame_as_source_get_frame( audio_frame *in, audio_frame *out ) {
    audio_copy_frame( out, in, 0 );
}

EXPORT AudioFrameSourceFuncs audio_frame_as_source_funcs = {
    .getFrame = (audio_getFrameFunc) audio_frame_as_source_get_frame,
};

EXPORT void
audio_get_frame( const audio_source *source, audio_frame *frame ) {
    if( !source || !source->funcs || !source->funcs->getFrame ) {
        frame->current_min_sample = 0;
        frame->current_max_sample = -1;
        return;
    }

    source->funcs->getFrame( source->obj, frame );
}


