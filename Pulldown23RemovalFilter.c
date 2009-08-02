
#include "framework.h"

static PyObject *pysourceFuncs;

typedef struct {
    PyObject_HEAD

    VideoSourceHolder source;
    int offset;
} py_obj_Pulldown23RemovalFilter;

static int
Pulldown23RemovalFilter_init( py_obj_Pulldown23RemovalFilter *self, PyObject *args, PyObject *kwds ) {
    PyObject *source;

    if( !PyArg_ParseTuple( args, "Oi", &source, &self->offset ) )
        return -1;

    if( !takeVideoSource( source, &self->source ) )
        return -1;

    return 0;
}

static void
Pulldown23RemovalFilter_getFrame( py_obj_Pulldown23RemovalFilter *self, int frameIndex, rgba_f16_frame *frame ) {
    if( self->source.source == NULL ) {
        // No result
        box2i_setEmpty( &frame->currentDataWindow );
        return;
    }

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
        // Mixed fields; we want the odds (field #2) from this frame:
        self->source.funcs->getFrame( self->source.source, baseFrame + 2, frame );

        int height = frame->currentDataWindow.max.y - frame->currentDataWindow.min.y + 1;
        int width = frame->currentDataWindow.max.x - frame->currentDataWindow.min.x + 1;

        // We want the evens (field #1) from this next frame
        // TODO: Cache this temp frame between calls
        rgba_f16_frame tempFrame;
        tempFrame.frameData = slice_alloc( sizeof(rgba_f16) * height * width );
        tempFrame.stride = width;
        tempFrame.fullDataWindow = frame->currentDataWindow;
        tempFrame.currentDataWindow = frame->currentDataWindow;

        self->source.funcs->getFrame( self->source.source, baseFrame + 3, &tempFrame );

        for( int i = (frame->currentDataWindow.min.y & 1) ? 1 : 0; i < height; i += 2 ) {
            memcpy( &frame->frameData[i * frame->stride + frame->currentDataWindow.min.y - frame->fullDataWindow.min.y],
                &tempFrame.frameData[i * frame->stride],
                width * sizeof(rgba_f16) );
        }

        slice_free( sizeof(rgba_f16) * height * width, tempFrame.frameData );
    }
}

static void
Pulldown23RemovalFilter_dealloc( py_obj_Pulldown23RemovalFilter *self ) {
    takeVideoSource( NULL, &self->source );
    self->ob_type->tp_free( (PyObject*) self );
}

static VideoFrameSourceFuncs sourceFuncs = {
    0,
    (video_getFrameFunc) Pulldown23RemovalFilter_getFrame
};

static PyObject *
Pulldown23RemovalFilter_getFuncs( py_obj_Pulldown23RemovalFilter *self, void *closure ) {
    return pysourceFuncs;
}

static PyGetSetDef Pulldown23RemovalFilter_getsetters[] = {
    { "_videoFrameSourceFuncs", (getter) Pulldown23RemovalFilter_getFuncs, NULL, "Video frame source C API." },
    { NULL }
};

static PyTypeObject py_type_Pulldown23RemovalFilter = {
    PyObject_HEAD_INIT(NULL)
    0,            // ob_size
    "fluggo.video.Pulldown23RemovalFilter",    // tp_name
    sizeof(py_obj_Pulldown23RemovalFilter),    // tp_basicsize
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = PyType_GenericNew,
    .tp_dealloc = (destructor) Pulldown23RemovalFilter_dealloc,
    .tp_init = (initproc) Pulldown23RemovalFilter_init,
    .tp_getset = Pulldown23RemovalFilter_getsetters
};

void init_Pulldown23RemovalFilter( PyObject *module ) {
    if( PyType_Ready( &py_type_Pulldown23RemovalFilter ) < 0 )
        return;

    Py_INCREF( (PyObject*) &py_type_Pulldown23RemovalFilter );
    PyModule_AddObject( module, "Pulldown23RemovalFilter", (PyObject *) &py_type_Pulldown23RemovalFilter );

    pysourceFuncs = PyCObject_FromVoidPtr( &sourceFuncs, NULL );
}



