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

#include "workspace.h"
#include "video_mix.h"

struct workspace_t_tag {
    GStaticMutex mutex;

    // leftsort and rightsort both contain all items in the workspace. leftsort is sorted
    // on item->x, rightsort is sorted backwards on (item->x + item->width).
    GSequence *leftsort, *rightsort;

    // leftiter and rightiter are references into leftsort and rightsort respectively.
    // They give us a way to quickly walk what should appear next when the current frame moves.

    // leftiter sits on the first item for which item->x > current_frame.
    // rightiter sits on the last item for which (item->x + item->width) <= current_frame.

    // There are better ways to do this for random-access, most importantly the interval tree.
    // This is quick and dirty and works.

    GSequenceIter *leftiter, *rightiter;
    int current_frame;

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
workspace_create_video() {
    workspace_t *result = g_slice_new0( workspace_t );

    g_static_mutex_init( &result->mutex );
    result->leftsort = g_sequence_new( NULL );
    result->rightsort = g_sequence_new( NULL );
    result->leftiter = g_sequence_get_begin_iter( result->leftsort );
    result->rightiter = g_sequence_get_begin_iter( result->rightsort );
    result->current_frame = 0;
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
    Move leftiter to the correct position for the current_frame.
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

        if( item->x <= self->current_frame )
            return;

        self->leftiter = iter;
    }

    item = (workspace_item_t *) g_sequence_get( iter );

    if( item->x > self->current_frame ) {
        // Iterate backwards to see if there's one closer to current_frame
        iter = g_sequence_iter_prev( iter );

        while( !g_sequence_iter_is_begin( iter ) ) {
            item = (workspace_item_t *) g_sequence_get( iter );

            if( item->x > self->current_frame )
                self->leftiter = iter;
            else
                break;

            iter = g_sequence_iter_prev( iter );
        }
    }
    else {
        // Move forwards until we're after current_frame
        iter = g_sequence_iter_next( iter );

        while( !g_sequence_iter_is_end( iter ) ) {
            item = (workspace_item_t *) g_sequence_get( iter );

            if( item->x > self->current_frame ) {
                self->leftiter = iter;
                break;
            }

            iter = g_sequence_iter_next( iter );
        }
    }
}

/*
    Move rightiter to the correct position for the current_frame.
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

        if( (item->x + item->width) > self->current_frame )
            return;

        self->rightiter = iter;
    }

    item = (workspace_item_t *) g_sequence_get( iter );

    if( (item->x + item->width) <= self->current_frame ) {
        // Iterate backwards to see if there's one closer to current_frame
        iter = g_sequence_iter_prev( iter );

        while( !g_sequence_iter_is_begin( iter ) ) {
            item = (workspace_item_t *) g_sequence_get( iter );

            if( (item->x + item->width) <= self->current_frame )
                self->rightiter = iter;
            else
                break;

            iter = g_sequence_iter_prev( iter );
        }
    }
    else {
        // Move forwards until we're before current_frame
        iter = g_sequence_iter_next( iter );

        while( !g_sequence_iter_is_end( iter ) ) {
            item = (workspace_item_t *) g_sequence_get( iter );

            if( (item->x + item->width) <= self->current_frame ) {
                self->rightiter = iter;
                break;
            }

            iter = g_sequence_iter_next( iter );
        }
    }
}

/*
    Update the composite list for this frame.
*/
static void
workspace_move_it( workspace_t *self, int frame ) {
    if( frame == self->current_frame )
        return;

    // Remove everything from the composite list that doesn't include this frame
    GSequenceIter *iter = g_sequence_get_begin_iter( self->composite_list );

    while( !g_sequence_iter_is_end( iter ) ) {
        GSequenceIter *current = iter;
        iter = g_sequence_iter_next( iter );

        workspace_item_t *item = (workspace_item_t *) g_sequence_get( current );

        if( frame < item->x || frame >= (item->x + item->width) ) {
            g_sequence_remove( current );
            item->compiter = NULL;
        }
    }

    int old_frame = self->current_frame;
    self->current_frame = frame;

    if( frame > old_frame ) {
        // Move forward
        while( !g_sequence_iter_is_end( self->leftiter ) ) {
            workspace_item_t *item = (workspace_item_t *) g_sequence_get( self->leftiter );

            if( frame >= item->x ) {
                // See if this frame is in this item
                if( frame < (item->x + item->width) )
                    item->compiter = g_sequence_insert_sorted( self->composite_list, item, cmpz, NULL );

                // Move ahead for the next time we ask
                self->leftiter = g_sequence_iter_next( self->leftiter );
            }
            else
                break;        // Nobody to the right will have this frame
        }

        workspace_fix_rightiter( self );
    }
    else {
        while( !g_sequence_iter_is_end( self->rightiter ) ) {
            workspace_item_t *item = (workspace_item_t *) g_sequence_get( self->rightiter );

            if( frame < (item->x + item->width) ) {
                // Add it to the composite list if the frame is in this item
                if( frame >= item->x )
                    item->compiter = g_sequence_insert_sorted( self->composite_list, item, cmpz, NULL );

                // Move ahead for the next time we ask
                self->rightiter = g_sequence_iter_next( self->rightiter );
            }
            else
                break;        // Nobody to the left will have this frame
        }

        workspace_fix_leftiter( self );
    }
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
    if( self->current_frame >= x && self->current_frame < (x + width) )
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

    if( (self->current_frame >= x) && (self->current_frame < (x + width)) ) {
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

    g_sequence_remove( item->leftiter );
    g_sequence_remove( item->rightiter );

    if( item->compiter )
        g_sequence_remove( item->compiter );

    g_static_mutex_unlock( &self->mutex );
}

static void
workspace_get_frame_f32( workspace_t *self, int frame_index, rgba_frame_f32 *frame ) {
    g_static_mutex_lock( &self->mutex );

    // Update the composite list
    workspace_move_it( self, frame_index );

    // Now composite everything in it
    if( g_sequence_get_length( self->composite_list ) == 0 ) {
        box2i_setEmpty( &frame->currentDataWindow );
        g_static_mutex_unlock( &self->mutex );
        return;
    }

    // Start at the *top* and move our way to the *bottom*
    // When we get the opaque hint later, this will save us tons of time
    // (Also, this only works if we have only "over" operations; add, for example,
    // must be done in-order)
    GSequenceIter *iter = g_sequence_iter_prev( g_sequence_get_end_iter( self->composite_list ) );
    workspace_item_t *item = (workspace_item_t *) g_sequence_get( iter );

    getFrame_f32( (video_source *) item->source, frame_index - item->x + item->offset, frame );

    if( !g_sequence_iter_is_begin( iter ) ) {
        rgba_frame_f32 tempFrame;
        v2i size;

        box2i_getSize( &frame->fullDataWindow, &size );

        tempFrame.frameData = g_slice_alloc( sizeof(rgba_f32) * size.y * size.x );
        tempFrame.fullDataWindow = frame->fullDataWindow;
        tempFrame.stride = size.x;

        while( !g_sequence_iter_is_begin( iter ) ) {
            iter = g_sequence_iter_prev( iter );
            item = (workspace_item_t *) g_sequence_get( iter );

            getFrame_f32( (video_source *) item->source, frame_index - item->x + item->offset, &tempFrame );
            video_mix_over_f32( frame, frame, &tempFrame, 1.0f, 1.0f );
        }

        g_slice_free1( sizeof(rgba_f32) * size.y * size.x, tempFrame.frameData );
    }

    g_static_mutex_unlock( &self->mutex );
}

static VideoFrameSourceFuncs workspace_video_funcs = {
    .getFrame32 = (video_getFrame32Func) workspace_get_frame_f32
};

EXPORT void
workspace_as_video_source( workspace_t *workspace, video_source *source ) {
    source->obj = workspace;
    source->funcs = &workspace_video_funcs;
}

EXPORT void
workspace_free( workspace_t *workspace ) {
    g_sequence_free( workspace->leftsort );
    g_sequence_free( workspace->rightsort );
    g_sequence_free( workspace->composite_list );

    g_static_mutex_free( &workspace->mutex );

    g_slice_free( workspace_t, workspace );
}


