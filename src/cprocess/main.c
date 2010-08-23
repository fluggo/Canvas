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
getFrameTime( const rational *frameRate, int frame ) {
    return ((int64_t) frame * INT64_C(1000000000) * (int64_t)(frameRate->d)) / (int64_t)(frameRate->n) + INT64_C(1);
}

EXPORT int
getTimeFrame( const rational *frameRate, int64_t time ) {
    return (time * (int64_t)(frameRate->n)) / (INT64_C(1000000000) * (int64_t)(frameRate->d));
}

EXPORT void video_getFrame_f16( video_source *source, int frameIndex, rgba_frame_f16 *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrame ) {
        source->funcs->getFrame( source->obj, frameIndex, targetFrame );
        return;
    }

    if( !source->funcs->getFrame32 ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    // Allocate a new frame
    rgba_frame_f32 tempFrame;
    v2i size;

    box2i_getSize( &targetFrame->fullDataWindow, &size );
    tempFrame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.x * size.y );
    tempFrame.fullDataWindow = targetFrame->fullDataWindow;
    tempFrame.currentDataWindow = targetFrame->fullDataWindow;
    tempFrame.stride = size.x;

    source->funcs->getFrame32( source->obj, frameIndex, &tempFrame );

    if( !box2i_isEmpty( &tempFrame.currentDataWindow ) ) {
        // Convert to f16
        int offsetX = tempFrame.currentDataWindow.min.x - tempFrame.fullDataWindow.min.x;
        int countX = tempFrame.currentDataWindow.max.x - tempFrame.currentDataWindow.min.x + 1;

        if( countX > 0 ) {
            for( int y = tempFrame.currentDataWindow.min.y - tempFrame.fullDataWindow.min.y;
                y <= tempFrame.currentDataWindow.max.y - tempFrame.fullDataWindow.min.y; y++ ) {

                half_convert_from_float(
                    &tempFrame.frameData[y * tempFrame.stride + offsetX].r,
                    &targetFrame->frameData[y * targetFrame->stride + offsetX].r,
                    countX * 4 );
            }
        }
    }

    targetFrame->currentDataWindow = tempFrame.currentDataWindow;

    g_slice_free1( sizeof(rgba_f32) * size.x * size.y, tempFrame.frameData );
}

EXPORT void video_getFrame_f32( video_source *source, int frameIndex, rgba_frame_f32 *targetFrame ) {
    if( !source || !source->funcs ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    if( source->funcs->getFrame32 ) {
        source->funcs->getFrame32( source->obj, frameIndex, targetFrame );
        return;
    }

    if( !source->funcs->getFrame ) {
        box2i_setEmpty( &targetFrame->currentDataWindow );
        return;
    }

    // Allocate a new frame
    rgba_frame_f16 tempFrame;
    v2i size;

    box2i_getSize( &targetFrame->fullDataWindow, &size );
    tempFrame.frameData = g_slice_alloc( sizeof(rgba_f16) * size.x * size.y );
    tempFrame.fullDataWindow = targetFrame->fullDataWindow;
    tempFrame.currentDataWindow = targetFrame->fullDataWindow;
    tempFrame.stride = size.x;

    source->funcs->getFrame( source->obj, frameIndex, &tempFrame );

    // Convert to f32
    int offsetX = tempFrame.currentDataWindow.min.x - tempFrame.fullDataWindow.min.x;
    int countX = tempFrame.currentDataWindow.max.x - tempFrame.currentDataWindow.min.x + 1;

    for( int y = tempFrame.currentDataWindow.min.y - tempFrame.fullDataWindow.min.y;
        y <= tempFrame.currentDataWindow.max.y - tempFrame.fullDataWindow.min.y; y++ ) {

        half_convert_to_float(
            &tempFrame.frameData[y * tempFrame.stride + offsetX].r,
            &targetFrame->frameData[y * targetFrame->stride + offsetX].r,
            countX * 4 );
    }

    targetFrame->currentDataWindow = tempFrame.currentDataWindow;

    g_slice_free1( sizeof(rgba_f16) * size.x * size.y, tempFrame.frameData );
}

EXPORT void
audio_get_frame( const audio_source *source, audio_frame *frame ) {
    if( !source || !source->funcs || !source->funcs->getFrame ) {
        frame->currentMinSample = 0;
        frame->currentMaxSample = -1;
        return;
    }

    source->funcs->getFrame( source->obj, frame );
}


