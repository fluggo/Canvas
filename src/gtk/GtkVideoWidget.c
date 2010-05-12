/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009 Brian J. Crowell <brian@fluggo.com>

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

#include <stdint.h>

#include <pygobject.h>
#include <pygtk/pygtk.h>
#include "pyframework.h"
#include <structmember.h>
#include "widget_gl.h"

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <gdk/gdkkeysyms.h>

typedef struct {
    PyObject_HEAD

    GdkGLConfig *glConfig;
    GtkWidget *drawingArea;
    PyObject *drawingAreaObj;
    VideoSourceHolder frameSource;
    PresentationClockHolder clock;

    widget_gl_context *context;
} py_obj_GtkVideoWidget;

static gboolean
expose( GtkWidget *widget, GdkEventExpose *event, py_obj_GtkVideoWidget *self ) {
    GdkGLContext *glcontext = gtk_widget_get_gl_context( self->drawingArea );
    GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable( self->drawingArea );

    if( !gdk_gl_drawable_gl_begin( gldrawable, glcontext ) )
        return FALSE;

    v2i widget_size = {
        .x = self->drawingArea->allocation.width,
        .y = self->drawingArea->allocation.height };

    widget_gl_draw( self->context, widget_size );

    // Flush buffers
    if( gdk_gl_drawable_is_double_buffered( gldrawable ) )
        gdk_gl_drawable_swap_buffers( gldrawable );
    else
        glFlush();

    gdk_gl_drawable_gl_end( gldrawable );

    return TRUE;
}

static void
GtkVideoWidget_invalidate_func( py_obj_GtkVideoWidget *self ) {
    gdk_window_invalidate_rect( self->drawingArea->window, &self->drawingArea->allocation, FALSE );
}

static int
GtkVideoWidget_init( py_obj_GtkVideoWidget *self, PyObject *args, PyObject *kwds ) {
    PyObject *pyclock;
    PyObject *frameSource = NULL;

    if( !PyArg_ParseTuple( args, "O|O", &pyclock, &frameSource ) )
        return -1;

    Py_CLEAR( self->drawingAreaObj );

    self->context = widget_gl_new();
    widget_gl_set_invalidate_func( self->context, (invalidate_func) GtkVideoWidget_invalidate_func, self );

    if( !takePresentationClock( pyclock, &self->clock ) ) {
        widget_gl_free( self->context );
        return -1;
    }

    widget_gl_set_presentation_clock( self->context, &self->clock.source );

    if( !py_video_takeSource( frameSource, &self->frameSource ) ) {
        widget_gl_free( self->context );
        return -1;
    }

    widget_gl_set_video_source( self->context, &self->frameSource.source );

    self->glConfig = gdk_gl_config_new_by_mode ( (GdkGLConfigMode) (GDK_GL_MODE_RGB    |
                                        GDK_GL_MODE_DEPTH  |
                                        GDK_GL_MODE_DOUBLE));
    if( self->glConfig == NULL )    {
        g_print( "*** Cannot find the double-buffered visual.\n" );
        g_print( "*** Trying single-buffered visual.\n" );

        /* Try single-buffered visual */
        self->glConfig = gdk_gl_config_new_by_mode ((GdkGLConfigMode) (GDK_GL_MODE_RGB   |
                                        GDK_GL_MODE_DEPTH));
        if( self->glConfig == NULL ) {
            g_print( "*** No appropriate OpenGL-capable visual found.\n" );
            exit( 1 );
        }
    }

    box2i display_window;
    box2i_set( &display_window, 0, 0, 319, 239 );

    widget_gl_set_display_window( self->context, &display_window );

    self->drawingArea = gtk_drawing_area_new();

    gtk_widget_set_gl_capability( self->drawingArea,
                                self->glConfig,
                                NULL,
                                TRUE,
                                GDK_GL_RGBA_TYPE );

    self->drawingAreaObj = pygobject_new( (GObject*) self->drawingArea );

    g_signal_connect( G_OBJECT(self->drawingArea), "expose_event", G_CALLBACK(expose), self );

    return 0;
}

static void
GtkVideoWidget_dealloc( py_obj_GtkVideoWidget *self ) {
    Py_CLEAR( self->drawingAreaObj );

    if( self->drawingArea != NULL )
        gtk_widget_destroy( GTK_WIDGET(self->drawingArea) );

    widget_gl_free( self->context );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyObject *
GtkVideoWidget_widgetObj( py_obj_GtkVideoWidget *self ) {
    Py_INCREF( self->drawingAreaObj );
    return self->drawingAreaObj;
}

static PyObject *
GtkVideoWidget_getDisplayWindow( py_obj_GtkVideoWidget *self ) {
    box2i window;
    widget_gl_get_display_window( self->context, &window );

    return Py_BuildValue( "(iiii)", window.min.x, window.min.y,
        window.max.x, window.max.y );
}

static PyObject *
GtkVideoWidget_setDisplayWindow( py_obj_GtkVideoWidget *self, PyObject *args ) {
    box2i window;

    if( !PyArg_ParseTuple( args, "(iiii)", &window.min.x, &window.min.y, &window.max.x, &window.max.y ) )
        return NULL;

    if( box2i_isEmpty( &window ) ) {
        PyErr_SetString( PyExc_Exception, "An empty window was passed to setDisplayWindow." );
        return NULL;
    }

    widget_gl_set_display_window( self->context, &window );

    Py_RETURN_NONE;
}

static PyObject *
GtkVideoWidget_getSource( py_obj_GtkVideoWidget *self ) {
    if( self->frameSource.source.obj == NULL )
        Py_RETURN_NONE;

    Py_INCREF((PyObject *) self->frameSource.source.obj);
    return (PyObject *) self->frameSource.source.obj;
}

static PyObject *
GtkVideoWidget_setSource( py_obj_GtkVideoWidget *self, PyObject *args, void *closure ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "O", &source ) )
        return NULL;

    bool result = py_video_takeSource( source, &self->frameSource );
    widget_gl_set_video_source( self->context, &self->frameSource.source );

    if( !result )
        return NULL;

    Py_RETURN_NONE;
}

static PyObject *
GtkVideoWidget_set_hard_mode_enable( py_obj_GtkVideoWidget *self, PyObject *args ) {
    PyObject *enable;

    if( !PyArg_ParseTuple( args, "O", &enable ) )
        return NULL;

    int i = PyObject_IsTrue( enable );

    if( i == -1 )
        return NULL;

    widget_gl_hard_mode_enable( self->context, (gboolean) i );
    Py_RETURN_NONE;
}

static PyObject *
GtkVideoWidget_get_hard_mode_enable( py_obj_GtkVideoWidget *self ) {
    if( widget_gl_get_hard_mode_enabled( self->context ) )
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyObject *
GtkVideoWidget_get_hard_mode_supported( py_obj_GtkVideoWidget *self ) {
    if( widget_gl_get_hard_mode_supported( self->context ) )
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyMethodDef GtkVideoWidget_methods[] = {
    { "drawing_area", (PyCFunction) GtkVideoWidget_widgetObj, METH_NOARGS,
        "Return the drawing area used for video output." },
    { "display_window", (PyCFunction) GtkVideoWidget_getDisplayWindow, METH_NOARGS,
        "Get the display window as a tuple of min and max coordinates (minX, minY, maxX, maxY)." },
    { "source", (PyCFunction) GtkVideoWidget_getSource, METH_NOARGS,
        "Get the video source." },
    { "set_display_window", (PyCFunction) GtkVideoWidget_setDisplayWindow, METH_VARARGS,
        "Set the display window using a tuple of min and max coordinates (minX, minY, maxX, maxY)." },
    { "set_source", (PyCFunction) GtkVideoWidget_setSource, METH_VARARGS,
        "Set the video source." },
    { "hardware_accel_supported", (PyCFunction) GtkVideoWidget_get_hard_mode_supported, METH_NOARGS,
        "Return true if hardware acceleration is supported." },
    { "hardware_accel", (PyCFunction) GtkVideoWidget_get_hard_mode_enable, METH_NOARGS,
        "Return true if hardware acceleration is enabled. If supported, it's enabled by default." },
    { "set_hardware_accel", (PyCFunction) GtkVideoWidget_set_hard_mode_enable, METH_VARARGS,
        "Enable or disable hardware acceleration. This has no effect if hardware acceleration isn't supported." },
    { NULL }
};

static PyTypeObject py_type_GtkVideoWidget = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.gtk.VideoWidget",    // tp_name
    sizeof(py_obj_GtkVideoWidget),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) GtkVideoWidget_dealloc,
    .tp_init = (initproc) GtkVideoWidget_init,
    .tp_methods = GtkVideoWidget_methods
};

EXPORT PyMODINIT_FUNC
initgtk() {
    PyObject *m = Py_InitModule3( "gtk", NULL,
        "GTK support for the Fluggo media processing library for Python." );

    int argc = 1;
    char *arg = "dummy";
    char **argv = &arg;

    if( PyType_Ready( &py_type_GtkVideoWidget ) < 0 )
        return;

    Py_INCREF( &py_type_GtkVideoWidget );
    PyModule_AddObject( m, "VideoWidget", (PyObject *) &py_type_GtkVideoWidget );

    init_pygobject();
    init_pygtk();
    gtk_gl_init( &argc, &argv );

    if( !g_thread_supported() )
        g_thread_init( NULL );
}

