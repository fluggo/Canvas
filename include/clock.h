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

#include "framework.h"

#if !defined(FLUGGO_CLOCK_H)
#define FLUGGO_CLOCK_H

#if defined(__cplusplus)
extern "C" {
#endif

#define CLK_LOOP    0x1

typedef struct {
    int64_t playbackMin, playbackMax;
    int64_t loopMin, loopMax;
    int flags;
} ClockRegions;

typedef int64_t (*clock_getPresentationTimeFunc)( void *self );
typedef void (*clock_getSpeedFunc)( void *self, rational *result );
typedef void (*clock_getRegionsFunc)( void *self, ClockRegions *result );
typedef void (*clock_callback_func)( void *data, rational *speed, int64_t time );
typedef void *(*clock_register_callback_func)( void *self, clock_callback_func callback, void *data, GDestroyNotify notify );
typedef void *(*clock_unregister_callback_func)( void *self, void *handle );

typedef struct {
    clock_getPresentationTimeFunc getPresentationTime;
    clock_getSpeedFunc getSpeed;
    clock_getRegionsFunc getRegions;
    clock_register_callback_func register_callback;
    clock_unregister_callback_func unregister_callback;
} PresentationClockFuncs;

typedef struct {
    void *obj;
    PresentationClockFuncs *funcs;
} presentation_clock;

typedef struct {
    presentation_clock source;
    PyObject *csource;
} PresentationClockHolder;

bool takePresentationClock( PyObject *source, PresentationClockHolder *holder );
int64_t gettime();

#define PRESENTATION_CLOCK_FUNCS "_presentation_clock_funcs"

#if defined(__cplusplus)
}
#endif

#endif

