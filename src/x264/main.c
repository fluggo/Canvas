/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2012 Brian J. Crowell <brian@fluggo.com>

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
#include <x264.h>

void init_X264VideoEncoder( PyObject *module );

EXPORT PyMODINIT_FUNC
PyInit_x264() {
    static PyModuleDef mdef = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "x264",
        .m_doc = "x264 support for the Fluggo media processing library.",

        // TODO: Consider making use of this; see Python docs
        .m_size = -1,
        //.m_methods = module_methods,

        // TODO: Consider supporting module cleanup
    };

    PyObject *m = PyModule_Create( &mdef );

    // Make sure process is available and initialized
    if( !PyImport_ImportModule( "fluggo.media.process" ) )
        return NULL;

    init_X264VideoEncoder( m );

    return m;
}

