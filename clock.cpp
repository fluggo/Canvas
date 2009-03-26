
#define __STDC_CONSTANT_MACROS

#include "clock.h"
#include <time.h>

static int64_t gettime() {
    timespec time;
    clock_gettime( CLOCK_MONOTONIC, &time );

    return ((int64_t) time.tv_sec) * INT64_C(1000000000)+ (int64_t) time.tv_nsec;
}

SystemPresentationClock::SystemPresentationClock() : _seekTime( INT64_C(0) ), _speedNum( 0 ), _speedDenom( 1 ) {
    _baseTime = gettime();
}

int64_t SystemPresentationClock::getPresentationTime() {
    int64_t elapsed = (gettime() - _baseTime) * _speedNum;

    if( _speedDenom == 1 )
        return elapsed + _seekTime;
    else
        return elapsed / _speedDenom + _seekTime;
}

void SystemPresentationClock::set( int speedNum, int speedDenom, int64_t seekTime ) {
    _baseTime = gettime();
    _seekTime = seekTime;
    _speedNum = speedNum;
    _speedDenom = speedDenom;
}

void SystemPresentationClock::getSpeed( int *speedNum, int *speedDenom ) {
    *speedNum = _speedNum;
    *speedDenom = _speedDenom;
}

void SystemPresentationClock::play( int speedNum, int speedDenom ) {
    set( speedNum, speedDenom, getPresentationTime() );
}

void SystemPresentationClock::seek( int64_t time ) {
    int speedNum, speedDenom;
    getSpeed( &speedNum, &speedDenom );

    set( speedNum, speedDenom, time );
}

