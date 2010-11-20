/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009-10 Brian J. Crowell <brian@fluggo.com>

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

struct workspace_t_tag {
    GStaticMutex mutex;

    // leftsort and rightsort both contain all items in the workspace. leftsort is sorted
    // on item->x, rightsort is sorted backwards on (item->x + item->width).
    GSequence *leftsort, *rightsort;

    // This is a more generic workspace than the last iteration. In audio, we're concerned about
    // the first and last *sample* to be composed at once, where in video we're only concerned about
    // composing a single frame. They're really the same algorithm; audio is the more general case,
    // and video is the case where start_frame == end_frame. Either way, the workspace logic is
    // only around to maintain the composite_list (below) and it's up to the video- or audio-
    // specific logic to compose the results.

    // leftiter and rightiter are references into leftsort and rightsort respectively.
    // They give us a way to quickly walk what should appear next when the current frame moves.

    // leftiter sits on the first item for which item->x > end_frame (that is, the next
    // item to enter the composite_list if end_frame increases).

    // rightiter sits on the last item for which (item->x + item->width) <= start_frame (that is,
    // the next item to enter the composite_list if start_frame decreases).

    // When start_frame increases or end_frame decreases, the composite_list is just scanned for
    // items that don't belong anymore.

    // There are better ways to do this for random-access, most importantly the interval tree.
    // This is quick and dirty and works.

    GSequenceIter *leftiter, *rightiter;
    int start_frame, end_frame;

    GSequence *composite_list;
};

struct workspace_item_t_tag {
    workspace_t *workspace;
    int64_t x, z, width;
    gpointer source, tag;
    GSequenceIter *leftiter, *rightiter, *compiter;
    int64_t offset;
};

G_GNUC_CONST static int cmpl( int64_t a, int64_t b ) {
    if( a > b )
        return 1;

    if( a < b )
        return -1;

    return 0;
}

G_GNUC_PURE static int cmp_left( gconstpointer aptr, gconstpointer bptr, gpointer user_data ) {
    const workspace_item_t *a = (workspace_item_t *) aptr, *b = (workspace_item_t *) bptr;

    int result = cmpl( a->x, b->x );

    if( result != 0 )
        return result;

    return cmpl( a->z, b->z );
}

G_GNUC_PURE static int cmp_right( gconstpointer aptr, gconstpointer bptr, gpointer user_data ) {
    const workspace_item_t *a = (workspace_item_t *) aptr, *b = (workspace_item_t *) bptr;

    int result = cmpl( a->x + a->width, b->x + b->width );

    if( result != 0 )
        return -result;

    return -cmpl( a->z, b->z );
}

G_GNUC_PURE static int cmpz( gconstpointer aptr, gconstpointer bptr, gpointer user_data ) {
    const workspace_item_t *a = (workspace_item_t *) aptr, *b = (workspace_item_t *) bptr;
    return -cmpl( a->z, b->z );
}

EXPORT workspace_t *
workspace_create() {
    workspace_t *result = g_slice_new0( workspace_t );

    g_static_mutex_init( &result->mutex );
    result->leftsort = g_sequence_new( NULL );
    result->rightsort = g_sequence_new( NULL );
    result->leftiter = g_sequence_get_begin_iter( result->leftsort );
    result->rightiter = g_sequence_get_begin_iter( result->rightsort );
    result->start_frame = 0;
    result->end_frame = 0;
    result->composite_list = g_sequence_new( NULL );

    return result;
}

EXPORT gint
workspace_get_length( workspace_t *self ) {
    g_static_mutex_lock( &self->mutex );
    gint result = g_sequence_get_length( self->leftsort );
    g_static_mutex_unlock( &self->mutex );

    return result;
}

/*
    Move leftiter to the correct position for the end_frame.
*/
static void
workspace_fix_leftiter( workspace_t *self ) {
    workspace_item_t *item;
    GSequenceIter *iter = self->leftiter;

    if( g_sequence_iter_is_end( iter ) ) {
        if( g_sequence_iter_is_begin( iter ) )
            return;

        iter = g_sequence_iter_prev( iter );
        item = (workspace_item_t *) g_sequence_get( iter );

        // If the item is potentially "in-range" then it's definitely not next
        if( item->x <= self->end_frame )
            return;

        self->leftiter = iter;
    }

    item = (workspace_item_t *) g_sequence_get( iter );

    if( item->x > self->end_frame ) {
        // Iterate backwards to see if there's one closer to end_frame
        while( !g_sequence_iter_is_begin( iter ) ) {
            iter = g_sequence_iter_prev( iter );
            item = (workspace_item_t *) g_sequence_get( iter );

            if( item->x > self->end_frame )
                self->leftiter = iter;
            else
                break;
        }
    }
    else {
        // Move forwards until we're after end_frame
        while( !g_sequence_iter_is_end( iter ) ) {
            iter = g_sequence_iter_next( iter );

            if( g_sequence_iter_is_end( iter ) )
                break;

            item = (workspace_item_t *) g_sequence_get( iter );

            if( item->x > self->end_frame ) {
                self->leftiter = iter;
                break;
            }
        }
    }
}

/*
    Move rightiter to the correct position for the start_frame.
*/
static void
workspace_fix_rightiter( workspace_t *self ) {
    workspace_item_t *item;
    GSequenceIter *iter = self->rightiter;

    if( g_sequence_iter_is_end( iter ) ) {
        if( g_sequence_iter_is_begin( iter ) )
            return;

        iter = g_sequence_iter_prev( iter );
        item = (workspace_item_t *) g_sequence_get( iter );

        // If the item is potentially "in-range" then it's definitely not next
        if( (item->x + item->width) > self->start_frame )
            return;

        self->rightiter = iter;
    }

    item = (workspace_item_t *) g_sequence_get( iter );

    if( (item->x + item->width) <= self->start_frame ) {
        // Iterate backwards to see if there's one closer to current_frame
        while( !g_sequence_iter_is_begin( iter ) ) {
            iter = g_sequence_iter_prev( iter );
            item = (workspace_item_t *) g_sequence_get( iter );

            if( (item->x + item->width) <= self->start_frame )
                self->rightiter = iter;
            else
                break;

            iter = g_sequence_iter_prev( iter );
        }
    }
    else {
        // Move forwards until we're before current_frame
        while( !g_sequence_iter_is_end( iter ) ) {
            iter = g_sequence_iter_next( iter );

            if( g_sequence_iter_is_end( iter ) )
                break;

            item = (workspace_item_t *) g_sequence_get( iter );

            if( (item->x + item->width) <= self->start_frame ) {
                self->rightiter = iter;
                break;
            }

            iter = g_sequence_iter_next( iter );
        }
    }
}

/*
    Update the composite list for this range.
*/
static void
workspace_move_it( workspace_t *self, int start_frame, int end_frame ) {
    if( start_frame == self->start_frame && end_frame == self->end_frame )
        return;

    // Remove everything from the composite list that doesn't include this range
    GSequenceIter *iter = g_sequence_get_begin_iter( self->composite_list );

    while( !g_sequence_iter_is_end( iter ) ) {
        GSequenceIter *current = iter;
        iter = g_sequence_iter_next( iter );

        workspace_item_t *item = (workspace_item_t *) g_sequence_get( current );

        if( end_frame < item->x || start_frame >= (item->x + item->width) ) {
            g_sequence_remove( current );
            item->compiter = NULL;
        }
    }

    int old_end_frame = self->end_frame, old_start_frame = self->start_frame;
    self->start_frame = start_frame;
    self->end_frame = end_frame;

    if( end_frame > old_end_frame ) {
        // Move forward
        while( !g_sequence_iter_is_end( self->leftiter ) ) {
            workspace_item_t *item = (workspace_item_t *) g_sequence_get( self->leftiter );

            if( end_frame >= item->x ) {
                // See if this frame is in this item
                if( start_frame < (item->x + item->width) )
                    item->compiter = g_sequence_insert_sorted( self->composite_list, item, cmpz, NULL );

                // Move ahead for the next time we ask
                self->leftiter = g_sequence_iter_next( self->leftiter );
            }
            else
                break;        // Nobody to the right will have this frame
        }
    }

    if( start_frame < old_start_frame ) {
        while( !g_sequence_iter_is_end( self->rightiter ) ) {
            workspace_item_t *item = (workspace_item_t *) g_sequence_get( self->rightiter );

            if( start_frame < (item->x + item->width) ) {
                // Add it to the composite list if the frame is in this item
                if( end_frame >= item->x )
                    item->compiter = g_sequence_insert_sorted( self->composite_list, item, cmpz, NULL );

                // Move ahead for the next time we ask
                self->rightiter = g_sequence_iter_next( self->rightiter );
            }
            else
                break;        // Nobody to the left will have this frame
        }
    }

    if( end_frame < old_end_frame )
        workspace_fix_leftiter( self );

    if( start_frame > old_start_frame )
        workspace_fix_rightiter( self );
}

EXPORT workspace_item_t *
workspace_add_item( workspace_t *self, gpointer source, int64_t x, int64_t width, int64_t offset, int64_t z, gpointer tag ) {
    workspace_item_t *item = g_slice_new( workspace_item_t );

    item->workspace = self;
    item->x = x;
    item->z = z;
    item->width = width;
    item->offset = offset;
    item->source = source;
    item->tag = tag;

    g_static_mutex_lock( &self->mutex );

    // Add to general lists
    item->leftiter = g_sequence_insert_sorted( self->leftsort, item, cmp_left, NULL );
    workspace_fix_leftiter( self );

    item->rightiter = g_sequence_insert_sorted( self->rightsort, item, cmp_right, NULL );
    workspace_fix_rightiter( self );

    // If necessary, add to composite_list
    if( self->end_frame >= x && self->start_frame < (x + width) )
        item->compiter = g_sequence_insert_sorted( self->composite_list, item, cmpz, NULL );
    else
        item->compiter = NULL;

    g_static_mutex_unlock( &self->mutex );

    return item;
}

EXPORT workspace_item_t *
workspace_get_item( workspace_t *self, gint index ) {
    g_static_mutex_lock( &self->mutex );
    GSequenceIter *iter = g_sequence_get_iter_at_pos( self->leftsort, index );
    workspace_item_t *result = (workspace_item_t *) g_sequence_get( iter );
    g_static_mutex_unlock( &self->mutex );

    return result;
}

EXPORT void
workspace_get_item_pos( workspace_item_t *item, int64_t *x, int64_t *width, int64_t *z ) {
    if( x )
        *x = item->x;

    if( width )
        *width = item->width;

    if( z )
        *z = item->z;
}

EXPORT int64_t
workspace_get_item_offset( workspace_item_t *item ) {
    return item->offset;
}

EXPORT void
workspace_set_item_offset( workspace_item_t *item, int64_t offset ) {
    item->offset = offset;
}

EXPORT gpointer
workspace_get_item_source( workspace_item_t *item ) {
    return item->source;
}

EXPORT void
workspace_set_item_source( workspace_item_t *item, gpointer source ) {
    item->source = source;
}

EXPORT gpointer
workspace_get_item_tag( workspace_item_t *item ) {
    return item->tag;
}

EXPORT void
workspace_set_item_tag( workspace_item_t *item, gpointer tag ) {
    item->tag = tag;
}

static void
workspace_move_item_x( workspace_item_t *item, int64_t x, int64_t width ) {
    workspace_t *self = item->workspace;

    item->x = x;
    item->width = width;

    g_sequence_sort_changed( item->leftiter, cmp_left, NULL );
    workspace_fix_leftiter( self );

    g_sequence_sort_changed( item->rightiter, cmp_right, NULL );
    workspace_fix_rightiter( self );

    if( (self->end_frame >= x) && (self->start_frame < (x + width)) ) {
        if( !item->compiter )
            item->compiter = g_sequence_insert_sorted( self->composite_list, item, cmpz, NULL );
    }
    else {
        if( item->compiter ) {
            g_sequence_remove( item->compiter );
            item->compiter = NULL;
        }
    }
}

static void
workspace_move_item_z( workspace_item_t *item, int64_t z ) {
    item->z = z;

    if( item->compiter )
        g_sequence_sort_changed( item->compiter, cmpz, NULL );
}

EXPORT void
workspace_update_item( workspace_item_t *item, int64_t *x, int64_t *width, int64_t *z, int64_t *offset, gpointer *source, gpointer *tag ) {
    workspace_t *self = item->workspace;
    g_static_mutex_lock( &self->mutex );

    if( x || width ) {
        int64_t old_x = item->x, old_width = item->width;

        if( !x )
            x = &old_x;

        if( !width )
            width = &old_width;

        workspace_move_item_x( item, *x, *width );
    }

    if( z )
        workspace_move_item_z( item, *z );

    if( offset )
        item->offset = *offset;

    if( source )
        item->source = *source;

    if( tag )
        item->tag = *tag;

    g_static_mutex_unlock( &self->mutex );
}

EXPORT void
workspace_remove_item( workspace_item_t *item ) {
    workspace_t *self = item->workspace;
    g_static_mutex_lock( &self->mutex );

    // Remove from leftsort (delicately)
    bool match_left = (g_sequence_iter_compare( self->leftiter, item->leftiter ) == 0);

    g_sequence_remove( item->leftiter );
    item->leftiter = NULL;

    if( match_left )
        self->leftiter = g_sequence_get_begin_iter( self->leftsort );

    workspace_fix_leftiter( self );

    // Remove from rightsort (also delicately)
    bool match_right = (g_sequence_iter_compare( self->rightiter, item->rightiter ) == 0);

    g_sequence_remove( item->rightiter );
    item->rightiter = NULL;

    if( match_right )
        self->rightiter = g_sequence_get_begin_iter( self->rightsort );

    workspace_fix_rightiter( self );

    // Remove from compiter (grab it!)
    if( item->compiter ) {
        g_sequence_remove( item->compiter );
        item->compiter = NULL;
    }

    item->workspace = NULL;
    g_slice_free( workspace_item_t, item );
    g_static_mutex_unlock( &self->mutex );
}

static void
workspace_get_frame_f32( workspace_t *self, int frame_index, rgba_frame_f32 *frame ) {
    g_static_mutex_lock( &self->mutex );

    // Update the composite list
    workspace_move_it( self, frame_index, frame_index );

    // Now composite everything in it
    if( g_sequence_get_length( self->composite_list ) == 0 ) {
        box2i_set_empty( &frame->current_window );
        g_static_mutex_unlock( &self->mutex );
        return;
    }

    // Start at the *top* and move our way to the *bottom*
    // When we get the opaque hint later, this will save us tons of time
    // (Also, this only works if we have only "over" operations; add, for example,
    // must be done in-order)
    GSequenceIter *iter = g_sequence_iter_prev( g_sequence_get_end_iter( self->composite_list ) );
    workspace_item_t *item = (workspace_item_t *) g_sequence_get( iter );

    video_get_frame_f32( (video_source *) item->source, frame_index - item->x + item->offset, frame );

    if( !g_sequence_iter_is_begin( iter ) ) {
        rgba_frame_f32 tempFrame;
        v2i size;

        box2i_get_size( &frame->full_window, &size );

        tempFrame.data = g_slice_alloc( sizeof(rgba_f32) * size.y * size.x );
        tempFrame.full_window = frame->full_window;

        while( !g_sequence_iter_is_begin( iter ) ) {
            iter = g_sequence_iter_prev( iter );
            item = (workspace_item_t *) g_sequence_get( iter );

            video_get_frame_f32( (video_source *) item->source, frame_index - item->x + item->offset, &tempFrame );
            video_mix_over_f32( frame, &tempFrame, 1.0f );
        }

        g_slice_free1( sizeof(rgba_f32) * size.y * size.x, tempFrame.data );
    }

    g_static_mutex_unlock( &self->mutex );
}

static video_frame_source_funcs workspace_video_funcs = {
    .get_frame_32 = (video_get_frame_32_func) workspace_get_frame_f32
};

EXPORT void
workspace_as_video_source( workspace_t *workspace, video_source *source ) {
    source->obj = workspace;
    source->funcs = &workspace_video_funcs;
}

static void
workspace_audio_get_frame( workspace_t *self, audio_frame *frame ) {
    g_static_mutex_lock( &self->mutex );

    // Update the composite list
    workspace_move_it( self, frame->full_min_sample, frame->full_max_sample );

    // Now composite everything in it
    frame->current_min_sample = 0;
    frame->current_max_sample = -1;

    if( g_sequence_get_length( self->composite_list ) == 0 ) {
        g_static_mutex_unlock( &self->mutex );
        return;
    }

    GSequenceIter *iter = g_sequence_get_begin_iter( self->composite_list );

    while( !g_sequence_iter_is_end( iter ) ) {
        workspace_item_t *item = (workspace_item_t *) g_sequence_get( iter );

        // Construct a ghost of the output frame so as to limit the composite to the current item
        audio_frame in_frame = {
            .full_min_sample = max(frame->full_min_sample, item->x),
            .full_max_sample = min(frame->full_max_sample, item->x + item->width - 1),
            .current_min_sample = max(frame->current_min_sample, item->x),
            .current_max_sample = min(frame->current_max_sample, item->x + item->width - 1),
            .channels = frame->channels
        };

        in_frame.data = frame->data + (in_frame.full_min_sample - frame->full_min_sample) * in_frame.channels;

        // TODO: Workspace items need some sort of opacity/attenuation setting
        audio_mix_add_pull( &in_frame, (audio_source *) item->source, 1.0f, -(item->x + item->offset) );

        frame->current_min_sample = min(frame->current_min_sample, in_frame.current_min_sample);
        frame->current_max_sample = max(frame->current_max_sample, in_frame.current_max_sample);

        iter = g_sequence_iter_next( iter );
    }

    g_static_mutex_unlock( &self->mutex );
}

static AudioFrameSourceFuncs workspace_audio_funcs = {
    .getFrame = (audio_getFrameFunc) workspace_audio_get_frame
};

EXPORT void
workspace_as_audio_source( workspace_t *workspace, audio_source *source ) {
    source->obj = workspace;
    source->funcs = &workspace_audio_funcs;
}

EXPORT void
workspace_free( workspace_t *workspace ) {
    g_sequence_free( workspace->leftsort );
    g_sequence_free( workspace->rightsort );
    g_sequence_free( workspace->composite_list );

    g_static_mutex_free( &workspace->mutex );

    g_slice_free( workspace_t, workspace );
}


