
#include "framework.h"

static void
generic_dealloc( PyObject *self ) {
    self->ob_type->tp_free( (PyObject*) self );
}

#define DECLARE_TIMEFUNC(name, deallocFunc)    \
    static TimeFunctionFuncs name##_sourceFuncs = { \
        0, \
        (timefunc_getValuesFunc) name##_getValues \
    }; \
    \
    static PyTypeObject py_type_##name = { \
        PyObject_HEAD_INIT(NULL) \
        0, \
        "fluggo.video." #name, \
        sizeof(py_obj_##name), \
        .tp_flags = Py_TPFLAGS_DEFAULT, \
        .tp_new = PyType_GenericNew, \
        .tp_dealloc = (destructor) deallocFunc, \
        .tp_init = (initproc) name##_init, \
        .tp_getset = name##_getsetters \
    };

#define DECLARE_GETTER(name) \
    static PyObject *name##_pysourceFuncs; \
    \
    static PyObject * \
    name##_getFuncs( PyObject *self, void *closure ) { \
        Py_INCREF(name##_pysourceFuncs); \
        return name##_pysourceFuncs; \
    } \

#define DECLARE_GETSETTER(name) \
    { "_timeFunctionFuncs", (getter) name##_getFuncs, NULL, "Time function C API." }

#define DECLARE_DEFAULT_GETSETTERS(name) \
    static PyGetSetDef name##_getsetters[] = { \
        DECLARE_GETSETTER(name), \
        { NULL } \
    };

#define SETUP_TIMEFUNC(name) \
    if( PyType_Ready( &py_type_##name ) < 0 ) \
        return; \
    \
    Py_INCREF( (PyObject*) &py_type_##name ); \
    PyModule_AddObject( module, #name, (PyObject *) &py_type_##name ); \
    \
    name##_pysourceFuncs = PyCObject_FromVoidPtr( &name##_sourceFuncs, NULL );


typedef struct {
    PyObject_HEAD
    float a, b;
} py_obj_LinearTimeFunc;

static int
LinearTimeFunc_init( py_obj_LinearTimeFunc *self, PyObject *args, PyObject *kwds ) {
    static char *kwlist[] = { "a", "b", NULL };

    if( !PyArg_ParseTupleAndKeywords( args, kwds, "ff", kwlist,
            &self->a, &self->b ) )
        return -1;

    return 0;
}

static void
LinearTimeFunc_getValues( py_obj_LinearTimeFunc *self, int count, long *times, float *outValues ) {
    for( int i = 0; i < count; i++ )
        outValues[i] = (times[i] / (float) NS_PER_SEC) * self->a + self->b;
}

DECLARE_GETTER(LinearTimeFunc)
DECLARE_DEFAULT_GETSETTERS(LinearTimeFunc)
DECLARE_TIMEFUNC(LinearTimeFunc, generic_dealloc)

void init_basictimefuncs( PyObject *module ) {
    SETUP_TIMEFUNC(LinearTimeFunc);
}



