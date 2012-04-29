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

static PyTypeObject *py_v2i, *py_box2i, *py_v2f, *py_box2f, *py_rgba, *py_fraction;


EXPORT PyObject *
py_make_v2i( v2i *v ) {
    return PyObject_CallFunction( (PyObject *) py_v2i, "ii", v->x, v->y );
}

EXPORT bool
py_parse_v2i( PyObject *o, v2i *v ) {
    return PyArg_ParseTuple( o, "ii", &v->x, &v->y );
}

EXPORT PyObject *
py_make_v2f( v2f *v ) {
    return PyObject_CallFunction( (PyObject *) py_v2f, "ff", v->x, v->y );
}

EXPORT bool
py_parse_v2f( PyObject *o, v2f *v ) {
    return PyArg_ParseTuple( o, "ff", &v->x, &v->y );
}

EXPORT PyObject *
py_make_box2i( box2i *box ) {
    return PyObject_CallFunction( (PyObject *) py_box2i, "(ii)(ii)", box->min.x, box->min.y, box->max.x, box->max.y );
}

EXPORT bool
py_parse_box2i( PyObject *o, box2i *box ) {
    return PyArg_ParseTuple( o, "(ii)(ii)", &box->min.x, &box->min.y, &box->max.x, &box->max.y );
}

EXPORT PyObject *
py_make_box2f( box2f *box ) {
    return PyObject_CallFunction( (PyObject *) py_box2f, "(ff)(ff)", box->min.x, box->min.y, box->max.x, box->max.y );
}

EXPORT bool
py_parse_box2f( PyObject *o, box2f *box ) {
    return PyArg_ParseTuple( o, "(ff)(ff)", &box->min.x, &box->min.y, &box->max.x, &box->max.y );
}

EXPORT PyObject *
py_make_rgba_f32( rgba_f32 *v ) {
    return PyObject_CallFunction( (PyObject *) py_rgba, "ffff", v->r, v->g, v->b, v->a );
}

EXPORT bool
py_parse_rgba_f32( PyObject *o, rgba_f32 *v ) {
    return PyArg_ParseTuple( o, "ffff", &v->r, &v->g, &v->b, &v->a );
}

EXPORT bool py_parse_rational( PyObject *in, rational *out ) {
    // Accept integers as rationals
    if( PyLong_Check( in ) ) {
        out->n = PyLong_AsLong( in );
        out->d = 1;

        return true;
    }

    PyObject *numerator = PyObject_GetAttrString( in, "numerator" );

    if( numerator == NULL )
        return false;

    long n = PyLong_AsLong( numerator );
    Py_DECREF(numerator);

    if( n == -1 && PyErr_Occurred() != NULL )
        return false;

    PyObject *denominator = PyObject_GetAttrString( in, "denominator" );

    if( denominator == NULL )
        return false;

    long d = PyLong_AsLong( denominator );
    Py_DECREF(denominator);

    if( d == -1 && PyErr_Occurred() != NULL )
        return false;

    out->n = (int) n;
    out->d = (unsigned int) d;

    return true;
}

EXPORT PyObject *py_make_rational( rational *in ) {
    return PyObject_CallFunction( (PyObject *) py_fraction, "iI", in->n, in->d );
}

void
init_basetypes( PyObject *module ) {
    PyObject *basetypes = PyImport_ImportModule( "fluggo.media.basetypes" );

    if( basetypes == NULL )
        return;

    py_v2i = (PyTypeObject *) PyObject_GetAttrString( basetypes, "v2i" );
    py_v2f = (PyTypeObject *) PyObject_GetAttrString( basetypes, "v2f" );
    py_box2i = (PyTypeObject *) PyObject_GetAttrString( basetypes, "box2i" );
    py_box2f = (PyTypeObject *) PyObject_GetAttrString( basetypes, "box2f" );
    py_rgba = (PyTypeObject *) PyObject_GetAttrString( basetypes, "rgba" );

    Py_CLEAR(basetypes);

    PyObject *fractions = PyImport_ImportModule( "fractions" );

    if( fractions == NULL )
        return;

    py_fraction = (PyTypeObject *) PyObject_GetAttrString( fractions, "Fraction" );
    Py_CLEAR( fractions );
}

