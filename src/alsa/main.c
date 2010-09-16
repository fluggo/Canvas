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

void init_AlsaPlayer( PyObject *module );

EXPORT PyMODINIT_FUND
initalsa() {
    PyObject *m = Py_InitModule3( "alsa", module_methods,
        "ALSA support for the Fluggo media processing library." );

    // Make sure process is available and initialized
    if( !PyImport_ImportModule( "fluggo.media.process" ) )
        return;

    init_AlsaPlayer( m );

    if( !g_thread_supported() )
        g_thread_init( NULL );
}

