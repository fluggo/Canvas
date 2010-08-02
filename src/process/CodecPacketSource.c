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

static PyObject *py_type_packet;

EXPORT bool
py_codecPacket_takeSource( PyObject *source, CodecPacketSourceHolder *holder ) {
    Py_CLEAR( holder->source.obj );
    Py_CLEAR( holder->csource );
    holder->source.funcs = NULL;

    if( source == NULL || source == Py_None )
        return true;

    Py_INCREF( source );
    holder->source.obj = source;
    holder->csource = PyObject_GetAttrString( source, CODEC_PACKET_SOURCE_FUNCS );

    if( holder->csource == NULL ) {
        Py_CLEAR( holder->source.obj );
        PyErr_SetString( PyExc_Exception, "The source didn't have an acceptable " CODEC_PACKET_SOURCE_FUNCS " attribute." );
        return false;
    }

    holder->source.funcs = (codec_packet_source_funcs*) PyCObject_AsVoidPtr( holder->csource );

    return true;
}

static PyObject *
CodecPacketSource_get_next_packet( PyObject *self, PyObject *args ) {
    CodecPacketSourceHolder holder = { { NULL } };

    if( !py_codecPacket_takeSource( self, &holder ) )
        return NULL;

    if( !holder.source.funcs->getNextPacket ) {
        py_codecPacket_takeSource( NULL, &holder );
        PyErr_SetNone( PyExc_NotImplementedError );
        return NULL;
    }

    codec_packet *packet = holder.source.funcs->getNextPacket( holder.source.obj );
    py_codecPacket_takeSource( NULL, &holder );

    if( !packet )
        Py_RETURN_NONE;

    PyObject *data = PyByteArray_FromStringAndSize( packet->data, packet->length );
    int64_t pts = packet->pts;
    int64_t dts = packet->dts;

    if( packet->free_func )
        packet->free_func( packet );

    if( !data )
        return NULL;

    PyObject *ptsObj, *dtsObj;

    if( dts == PACKET_TS_NONE ) {
        dtsObj = Py_None;
        Py_INCREF(dtsObj);
    }
    else
        dtsObj = PyLong_FromLongLong( dts );

    if( pts == PACKET_TS_NONE ) {
        ptsObj = Py_None;
        Py_INCREF(ptsObj);
    }
    else
        ptsObj = PyLong_FromLongLong( pts );

    PyObject *result = PyObject_CallFunctionObjArgs( py_type_packet, data, dtsObj, ptsObj, NULL );
    Py_CLEAR( data );
    Py_CLEAR( dtsObj );
    Py_CLEAR( ptsObj );

    return result;
}

static PyMethodDef CodecPacketSource_methods[] = {
    { "get_next_packet", (PyCFunction) CodecPacketSource_get_next_packet, METH_NOARGS,
        "Get the next codec packet from the source." },
    { NULL }
};

PyTypeObject py_type_CodecPacketSource = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.media.process.CodecPacketSource",    // tp_name
    0,    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_methods = CodecPacketSource_methods,
};

void init_CodecPacketSource( PyObject *module ) {
    PyObject *collections = PyImport_ImportModule( "collections" );

    if( collections == NULL )
        return;

    PyObject *namedtuple = PyObject_GetAttrString( collections, "namedtuple" );
    Py_CLEAR( collections );

    py_type_packet = PyObject_CallFunction( namedtuple, "ss", "CodecPacket", "data dts pts" );

    Py_CLEAR( namedtuple );

    if( PyType_Ready( &py_type_CodecPacketSource ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_CodecPacketSource );
    PyModule_AddObject( module, "CodecPacketSource", (PyObject *) &py_type_CodecPacketSource );
    PyModule_AddObject( module, "CodecPacket", py_type_packet );
}


