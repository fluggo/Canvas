
#include "framework.h"

typedef int64_t (*clock_getPresentationTimeFunc)( PyObject *self );
typedef void (*clock_getSpeedFunc)( PyObject *self, rational *result );

typedef struct {
    clock_getPresentationTimeFunc getPresentationTime;
    clock_getSpeedFunc getSpeed;
} PresentationClockFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    PresentationClockFuncs *funcs;
} PresentationClockHolder;

NOEXPORT bool takePresentationClock( PyObject *source, PresentationClockHolder *holder );

