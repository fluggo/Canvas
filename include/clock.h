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

#endif

