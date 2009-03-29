
#define __STDC_CONSTANT_MACROS

#include "clock.h"
#include <time.h>

using namespace Imf;

static int64_t gettime() {
    timespec time;
    clock_gettime( CLOCK_MONOTONIC, &time );

    return ((int64_t) time.tv_sec) * INT64_C(1000000000)+ (int64_t) time.tv_nsec;
}

SystemPresentationClock::SystemPresentationClock() : _seekTime( INT64_C(0) ), _speed() {
    _baseTime = gettime();
}

int64_t SystemPresentationClock::getPresentationTime() {
    int64_t elapsed = (gettime() - _baseTime) * _speed.n;

    if( _speed.d == 1 )
        return elapsed + _seekTime;
    else
        return elapsed / _speed.d + _seekTime;
}

void SystemPresentationClock::set( Rational speed, int64_t seekTime ) {
    _baseTime = gettime();
    _seekTime = seekTime;
    _speed = speed;
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

