

extern "C" {
#include <avformat.h>
#include <swscale.h>
}

#include <memory>
#include <errno.h>

#include <Iex.h>
#include <ImathMath.h>
#include <ImathFun.h>
#include <ImfRgbaFile.h>
#include <ImfHeader.h>
#include <ImfArray.h>
#include <halfFunction.h>

class IFrameSource {
public:
    virtual void GetFrame( int64_t frame, Imf::Array2D<Imf::Rgba> &array ) = 0;
};

class AVFileReader : public IFrameSource {
public:
    AVFileReader( const char *filename );
    ~AVFileReader();

    virtual void GetFrame( int64_t frame, Imf::Array2D<Imf::Rgba> &array );

private:
    AVFormatContext *_context;
    AVCodecContext *_codecContext;
    AVCodec *_codec;
    int _firstVideoStream;
    AVFrame *_inputFrame, *_rgbFrame;
    uint8_t *_rgbBuffer;
    float _colorMatrix[3][3];
    struct SwsContext *_scaler;

};


