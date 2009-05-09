
#define __STDC_CONSTANT_MACROS

#include <gtk/gtk.h>
#include "clock.h"
#include <time.h>

using namespace Imf;

static int64_t gettime() {
    timespec time;
    clock_gettime( CLOCK_MONOTONIC, &time );

    return ((int64_t) time.tv_sec) * INT64_C(1000000000)+ (int64_t) time.tv_nsec;
}

SystemPresentationClock::SystemPresentationClock() : _seekTime( INT64_C(0) ), _speed() {
    _mutex = g_mutex_new();
    _baseTime = gettime();
}

int64_t SystemPresentationClock::getPresentationTime() {
    g_mutex_lock( _mutex );
    int64_t elapsed = (gettime() - _baseTime) * _speed.n;
    int64_t seekTime = _seekTime;
    unsigned int d = _speed.d;
    g_mutex_unlock( _mutex );

    if( d == 1 )
        return elapsed + seekTime;
    else
        return elapsed / d + seekTime;
}

void SystemPresentationClock::set( Rational speed, int64_t seekTime ) {
    g_mutex_lock( _mutex );
    _baseTime = gettime();
    _seekTime = seekTime;
    _speed = speed;
    g_mutex_unlock( _mutex );
}

Rational SystemPresentationClock::getSpeed() {
    return _speed;
}

void SystemPresentationClock::play( Rational speed ) {
    set( speed, getPresentationTime() );
}

void SystemPresentationClock::seek( int64_t time ) {
    set( _speed, time );
}

