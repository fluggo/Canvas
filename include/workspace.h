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

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct workspace_t_tag workspace_t;
typedef struct workspace_iter_t_tag workspace_iter_t;
typedef struct workspace_item_t_tag workspace_item_t;

#define VIDEO_MIX_OP_OVER 0

workspace_t *workspace_create_video();
gint workspace_get_length( workspace_t *workspace );
workspace_item_t *workspace_add_item( workspace_t *self, gpointer source, int64_t x, int64_t width, int64_t offset, int64_t z, gpointer tag );
workspace_item_t *workspace_get_item( workspace_t *self, gint index );
void workspace_remove_item( workspace_item_t *item );
void workspace_as_video_source( workspace_t *workspace, video_source *source );
void workspace_free( workspace_t *workspace );
void workspace_get_item_pos( workspace_item_t *item, int64_t *x, int64_t *width, int64_t *z );
int64_t workspace_get_item_offset( workspace_item_t *item );
void workspace_set_item_offset( workspace_item_t *item, int64_t offset );
gpointer workspace_get_item_source( workspace_item_t *item );
void workspace_set_item_source( workspace_item_t *item, gpointer source );
gpointer workspace_get_item_tag( workspace_item_t *item );
void workspace_set_item_tag( workspace_item_t *item, gpointer tag );
void workspace_update_item( workspace_item_t *item, int64_t *x, int64_t *width, int64_t *z, int64_t *offset, gpointer *source, gpointer *tag );
void workspace_as_video_source( workspace_t *workspace, video_source *source );

#if defined(__cplusplus)
}
#endif

