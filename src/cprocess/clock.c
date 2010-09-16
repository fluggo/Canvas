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

#include <stdint.h>
#include <time.h>

#if defined(WINNT)
#include <glib.h>
#include <windows.h>
#endif

int64_t gettime() {
#if defined(WINNT)
    static gsize __init = 0;
    static LARGE_INTEGER resolution;

    if( g_once_init_enter( &__init ) ) {
        if( !QueryPerformanceFrequency( &resolution ) )
            g_error( g_win32_error_message( GetLastError() ) );

        g_once_init_leave( &__init, 1 );
    }

    LARGE_INTEGER counter;

    if( !QueryPerformanceCounter( &counter ) )
        g_error( g_win32_error_message( GetLastError() ) );

    return counter.QuadPart * INT64_C(1000000000) / resolution.QuadPart;
#else
    struct timespec time;
    clock_gettime( CLOCK_MONOTONIC, &time );

    return ((int64_t) time.tv_sec) * INT64_C(1000000000) + (int64_t) time.tv_nsec;
#endif
}

