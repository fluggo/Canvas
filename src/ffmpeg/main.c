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

#include "pyframework.h"
#include <structmember.h>

PyObject *py_writeVideo( PyObject *self, PyObject *args, PyObject *kw );

static PyMethodDef module_methods[] = {
    { "write_video", (PyCFunction) py_writeVideo, METH_VARARGS | METH_KEYWORDS,
        "TBD" },
    { NULL }
};

void init_FFVideoSource( PyObject *module );
void init_FFVideoDecoder( PyObject *module );
void init_FFVideoEncoder( PyObject *module );
void init_FFAudioSource( PyObject *module );
void init_FFDemuxer( PyObject *module );
void init_FFMuxer( PyObject *module );
void init_FFContainer( PyObject *module );

EXPORT PyMODINIT_FUNC
initffmpeg() {
    PyObject *m = Py_InitModule3( "ffmpeg", module_methods,
        "FFmpeg support for the Fluggo media processing library." );

    // Make sure process is available and initialized
    if( !PyImport_ImportModule( "fluggo.media.process" ) )
        return;

    init_FFVideoSource( m );
    init_FFVideoDecoder( m );
    init_FFVideoEncoder( m );
    init_FFAudioSource( m );
    init_FFDemuxer( m );
    init_FFMuxer( m );
    init_FFContainer( m );

    if( !g_thread_supported() )
        g_thread_init( NULL );
}

