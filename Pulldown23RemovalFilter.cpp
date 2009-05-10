
#include "framework.h"

using namespace Imf;

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    VideoSourceHolder source;
    int offset;
    bool oddFirst;
} py_obj_Pulldown23RemovalFilter;

static int
Pulldown23RemovalFilter_init( py_obj_Pulldown23RemovalFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source, *oddFirst;

    if( !PyArg_ParseTuple( args, "OiO", &source, &self->offset, &oddFirst ) )
        return -1;

    self->oddFirst = (bool) PyObject_IsTrue( oddFirst );

    if( takeVideoSource( source, &self->source ) < 0 )
        return -1;

    return 0;
}

static void
Pulldown23RemovalFilter_getFrame( py_obj_Pulldown23RemovalFilter *self, int64_t frameIndex, RgbaFrame *frame ) {
    // Cadence offsets:

    // 0 AA BB BC CD DD (0->0, 1->1, 3->4), (2->2b3a)
    // 1 BB BC CD DD EE (0->0, 2->3, 3->4), (1->1b2a)
    // 2 BC CD DD EE FF (1->2, 2->3, 3->4), (0->0b1a)
    // 3 CD DD EE FF FG (0->1, 1->2, 2->3), (3->4b5a) (same as 4 with 1st frame discarded)
    // 4 DD EE FF FG GH (0->0, 1->1, 2->2), (3->3b4a)

    int frameOffset;

    if( self->offset == 4 )
        frameOffset = (frameIndex + 3) & 3;
    else
        frameOffset = (frameIndex + self->offset) & 3;

    int64_t baseFrame = ((frameIndex + self->offset) >> 2) * 5 - self->offset;

    // Solid frames
    if( frameOffset == 0 ) {
        self->source.funcs->getFrame( self->source.source, baseFrame, frame );
    }
    else if( frameOffset == 1 ) {
        self->source.funcs->getFrame( self->source.source, baseFrame + 1, frame );
    }
    else if( frameOffset == 3 ) {
        self->source.funcs->getFrame( self->source.source, baseFrame + 4, frame );
    }
    else {
        // Mixed fields
        self->source.funcs->getFrame( self->source.source, baseFrame + 2, frame );

        int height = frame->currentDataWindow.max.y - frame->currentDataWindow.min.y + 1;
        int width = frame->currentDataWindow.max.x - frame->currentDataWindow.min.x + 1;

        RgbaFrame tempFrame;
        tempFrame.frameData.resizeErase( height, width );
        tempFrame.fullDataWindow = frame->currentDataWindow;
        tempFrame.currentDataWindow = frame->currentDataWindow;

        self->source.funcs->getFrame( self->source.source, baseFrame + 3, &tempFrame );

        for( int i = (self->oddFirst ? 0 : 1); i < height; i += 2 )
            memcpy( &frame->frameData[i][0], &tempFrame.frameData[i][0], width * sizeof(Rgba) );
    }
}

static void
Pulldown23RemovalFilter_dealloc( py_obj_Pulldown23RemovalFilter *self ) {
    Py_CLEAR( self->source.source );
    Py_CLEAR( self->source.csource );

    self->ob_type->tp_free( (PyObject*) self );
}

static PyTypeObject py_type_Pulldown23RemovalFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.Pulldown23RemovalFilter",    // tp_name
    sizeof(py_obj_Pulldown23RemovalFilter)    // tp_basicsize
};

static VideoFrameSourceFuncs sourceFuncs = {
    0,
    (video_getFrameFunc) Pulldown23RemovalFilter_getFrame
};

static PyObject *
Pulldown23RemovalFilter_getFuncs( py_obj_Pulldown23RemovalFilter */*self*/, void */*closure*/ ) {
    return pysourceFuncs;
}

static PyGetSetDef Pulldown23RemovalFilter_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) Pulldown23RemovalFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

void init_Pulldown23RemovalFilter( PyObject *module ) {
    py_type_Pulldown23RemovalFilter.tp_flags = Py_TPFLAGS_DEFAULT;
    py_type_Pulldown23RemovalFilter.tp_new = PyType_GenericNew;
    py_type_Pulldown23RemovalFilter.tp_dealloc = (destructor) Pulldown23RemovalFilter_dealloc;
    py_type_Pulldown23RemovalFilter.tp_init = (initproc) Pulldown23RemovalFilter_init;
    py_type_Pulldown23RemovalFilter.tp_getset = Pulldown23RemovalFilter_getsetters;

    if( PyType_Ready( &py_type_Pulldown23RemovalFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_Pulldown23RemovalFilter );
    PyModule_AddObject( module, "Pulldown23RemovalFilter", (PyObject *) &py_type_Pulldown23RemovalFilter );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



