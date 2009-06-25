
#include "framework.h"

#define CLK_LOOP    0x1

typedef struct {
    int64_t playbackMin, playbackMax;
    int64_t loopMin, loopMax;
    int flags;
} ClockRegions;

typedef int64_t (*clock_getPresentationTimeFunc)( PyObject *self );
typedef void (*clock_getSpeedFunc)( PyObject *self, rational *result );
typedef void (*clock_getRegionsFunc)( PyObject *self, ClockRegions *result );

typedef struct {
    clock_getPresentationTimeFunc getPresentationTime;
    clock_getSpeedFunc getSpeed;
    clock_getRegionsFunc getRegions;
} PresentationClockFuncs;

typedef struct {
    PyObject *source;
    PyObject *csource;
    PresentationClockFuncs *funcs;
} PresentationClockHolder;

NOEXPORT bool takePresentationClock( PyObject *source, PresentationClockHolder *holder );

