
#include "framework.h"

using namespace Imf;

Pulldown23RemovalFilter::Pulldown23RemovalFilter( IFrameSource *source, int offset, bool oddFirst )
    : _source( source ), _offset( offset ), _oddFirst( oddFirst ) {
}

void Pulldown23RemovalFilter::GetFrame( int64_t frame, Array2D<Rgba> &array ) {
    // Cadence offsets:

    // 0 AA BB BC CD DD (0->0, 1->1, 3->4), (2->2b3a)
    // 1 BB BC CD DD EE (0->0, 2->3, 3->4), (1->1b2a)
    // 2 BC CD DD EE FF (1->2, 2->3, 3->4), (0->0b1a)
    // 3 CD DD EE FF FG (0->1, 1->2, 2->3), (3->4b5a) (same as 4 with 1st frame discarded)
    // 4 DD EE FF FG GH (0->0, 1->1, 2->2), (3->3b4a)

    int frameOffset;

    if( _offset == 4 )
        frameOffset = (frame + 3) & 3;
    else
        frameOffset = (frame + _offset) & 3;

    int64_t baseFrame = ((frame + _offset) >> 2) * 5 - _offset;

    // Solid frames
    if( frameOffset == 0 ) {
        _source->GetFrame( baseFrame, array );
    }
    else if( frameOffset == 1 ) {
        _source->GetFrame( baseFrame + 1, array );
    }
    else if( frameOffset == 3 ) {
        _source->GetFrame( baseFrame + 4, array );
    }
    else {
        // Mixed fields
        _source->GetFrame( baseFrame + 2, array );

        Array2D<Rgba> temp( 480, 720 );
        _source->GetFrame( baseFrame + 3, temp );

        for( int i = (_oddFirst ? 0 : 1); i < 480; i += 2 )
            memcpy( array[i], temp[i], 720 * sizeof(Rgba) );
    }
}

