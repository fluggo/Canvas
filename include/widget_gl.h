
#include "framework.h"
#include "clock.h"

#if !defined(FLUGGO_WIDGET_GL_H)
#define FLUGGO_WIDGET_GL_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct __tag_widget_gl_context widget_gl_context;

typedef void (*invalidate_func)( void *closure );

widget_gl_context *widget_gl_new();
void widget_gl_free( widget_gl_context *self );

gboolean widget_gl_get_hard_mode_supported( widget_gl_context *self );
gboolean widget_gl_get_hard_mode_enabled( widget_gl_context *self );
void widget_gl_hard_mode_enable( widget_gl_context *self, gboolean enable );
void widget_gl_get_display_window( widget_gl_context *self, box2i *display_window );
void widget_gl_set_display_window( widget_gl_context *self, box2i *display_window );
void widget_gl_set_video_source( widget_gl_context *self, video_source *source );
void widget_gl_set_presentation_clock( widget_gl_context *self, presentation_clock *clock );
float widget_gl_get_pixel_aspect_ratio( widget_gl_context *self );
void widget_gl_set_pixel_aspect_ratio( widget_gl_context *self, float pixel_aspect_ratio );

void widget_gl_draw( widget_gl_context *self, v2i widget_size );
void widget_gl_set_invalidate_func( widget_gl_context *self, invalidate_func func, void *closure );

#if defined(__cplusplus)
}
#endif

#endif

