
#include <stdint.h>

class IPresentationClock {
public:
    virtual int64_t getPresentationTime() = 0;
};

class SystemPresentationClock : public IPresentationClock {
public:
    SystemPresentationClock();

    virtual int64_t getPresentationTime();

    void set( int speedNum, int speedDenom, int64_t seekTime );
    void getSpeed( int *speedNum, int *speedDenom );
    void play( int speedNum = 1, int speedDenom = 1 );
    void seek( int64_t time );

private:
    int64_t _seekTime, _baseTime;
    int _speedNum, _speedDenom;
};

