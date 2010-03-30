
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

/*
    Determine whether hard mode is supported.

    self -- The widget_gl_context.

    This will return false until widget_gl_draw has been called for the first time.
*/
gboolean widget_gl_get_hard_mode_supported( widget_gl_context *self );
gboolean widget_gl_get_hard_mode_enabled( widget_gl_context *self );
void widget_gl_hard_mode_enable( widget_gl_context *self, gboolean enable );
void widget_gl_get_display_window( widget_gl_context *self, box2i *display_window );
void widget_gl_set_display_window( widget_gl_context *self, box2i *display_window );
void widget_gl_play( widget_gl_context *self );
void widget_gl_stop( widget_gl_context *self );
void widget_gl_set_video_source( widget_gl_context *self, video_source *source );
void widget_gl_set_presentation_clock( widget_gl_context *self, presentation_clock *clock );

/*
    Paint the widget with the current GL context.

    self -- The widget_gl_context.
    widget_size -- The current size of the widget.

    Call this function inside your widget's expose function after
    setting up the GL context. This function will set up everything else.
*/
void widget_gl_draw( widget_gl_context *self, v2i widget_size );

/*
    Set the function to be called when the widget needs to be repainted.

    self -- The widget_gl_context.
    func -- The function to be called.
    closure -- Argument to pass to func.

    The proper thing to do when *func* is called is probably invalidate
    the widget.
*/
void widget_gl_set_invalidate_func( widget_gl_context *self, invalidate_func func, void *closure );

#if defined(__cplusplus)
}
#endif

#endif

