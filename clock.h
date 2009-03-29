
#include <stdint.h>
#include <ImfRational.h>

class IPresentationClock {
public:
    virtual int64_t getPresentationTime() = 0;
    virtual Imf::Rational getSpeed() = 0;
};

class SystemPresentationClock : public IPresentationClock {
public:
    SystemPresentationClock();

    virtual int64_t getPresentationTime();

    void set( Imf::Rational speed, int64_t seekTime );
    virtual Imf::Rational getSpeed();
    void play( Imf::Rational speed = Imf::Rational( 1, 1 ) );
    void seek( int64_t time );

private:
    int64_t _seekTime, _baseTime;
    Imf::Rational _speed;
};

