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

#include "audio_mix.h"

/************
    audio_copy_frame
************/

static void
test_copy_frame_basic_expand() {
    float in_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .channelCount = 1,
    };

    audio_copy_frame( &out, &in, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i], ==, out_data[i + 1] );
    }
}

static void
test_copy_frame_basic_offset() {
    float in_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .channelCount = 1,
    };

    audio_copy_frame( &out, &in, 3 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 1);
    g_assert_cmpint(out.currentMaxSample, ==, 3);

    for( int i = 2; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i], ==, out_data[i + 1 - 3] );
    }
}

static void
test_copy_frame_stereo_reduce_channels() {
    float in_data[10] = {
        0.0f, 1.0f,
        2.0f, 3.0f,
        4.0f, 5.0f,
        6.0f, 7.0f,
        8.0f, 9.0f };
    float out_data[5] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 2,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .channelCount = 1,
    };

    audio_copy_frame( &out, &in, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 2);
    g_assert_cmpint(out.fullMaxSample, ==, 6);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i * 2], ==, out_data[i] );
    }
}

static void
test_copy_frame_stereo_expand_channels() {
    float in_data[10] = {
        0.0f, 1.0f,
        2.0f, 3.0f,
        4.0f, 5.0f,
        6.0f, 7.0f,
        8.0f, 9.0f };

    float out_data[15];

    for( int i = 0; i < 15; i++ )
        out_data[i] = -15.0f;

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 2,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .channelCount = 3,
    };

    audio_copy_frame( &out, &in, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 2);
    g_assert_cmpint(out.fullMaxSample, ==, 6);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i * 2], ==, out_data[i * 3] );
        g_assert_cmpfloat( in_data[i * 2 + 1], ==, out_data[i * 3 + 1] );
        g_assert_cmpfloat( 0.0f, ==, out_data[i * 3 + 2] );
    }
}


/************
    audio_copy_frame_attenuate
************/

static void
test_copy_frame_attenuate_basic_expand() {
    float in_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .channelCount = 1,
    };

    audio_copy_frame_attenuate( &out, &in, 0.5f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i] * 0.5f, ==, out_data[i + 1] );
    }
}

static void
test_copy_frame_attenuate_stereo_reduce_channels() {
    float in_data[10] = {
        0.0f, 1.0f,
        2.0f, 3.0f,
        4.0f, 5.0f,
        6.0f, 7.0f,
        8.0f, 9.0f };
    float out_data[5] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 2,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .channelCount = 1,
    };

    audio_copy_frame_attenuate( &out, &in, 0.5f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 2);
    g_assert_cmpint(out.fullMaxSample, ==, 6);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i * 2] * 0.5f, ==, out_data[i] );
    }
}

static void
test_copy_frame_attenuate_stereo_expand_channels() {
    float in_data[10] = {
        0.0f, 1.0f,
        2.0f, 3.0f,
        4.0f, 5.0f,
        6.0f, 7.0f,
        8.0f, 9.0f };

    float out_data[15];

    for( int i = 0; i < 15; i++ )
        out_data[i] = -15.0f;

    audio_frame in = {
        .frameData = in_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 2,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .channelCount = 3,
    };

    audio_copy_frame_attenuate( &out, &in, 0.5f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 2);
    g_assert_cmpint(out.fullMaxSample, ==, 6);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i * 2] * 0.5f, ==, out_data[i * 3] );
        g_assert_cmpfloat( in_data[i * 2 + 1] * 0.5f, ==, out_data[i * 3 + 1] );
        g_assert_cmpfloat( 0.0f, ==, out_data[i * 3 + 2] );
    }
}


/************
    audio_mix_add
************/

static void
test_add_basic() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float b_data[5] = { 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };
    float out_data[7] = { 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 9.0f };

    audio_frame a = {
        .frameData = a_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i] + b_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_empty_in() {
    float dummy_data[5] = { 12.0f, 12.0f, 12.0f, 12.0f, 12.0f };
    float test_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 9.0f };

    audio_frame a = {
        .frameData = dummy_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 4, .currentMaxSample = 3,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( test_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_zero_in() {
    float dummy_data[5] = { 12.0f, 12.0f, 12.0f, 12.0f, 12.0f };
    float test_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 9.0f };

    audio_frame a = {
        .frameData = dummy_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 0.0f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( test_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_empty_out() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame a = {
        .frameData = a_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 5, .currentMaxSample = 4,
        .channelCount = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_zero_out() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame a = {
        .frameData = a_data,
        .fullMinSample = 2, .fullMaxSample = 6,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 2, .currentMaxSample = 6,
        .channelCount = 1,
    };

    audio_mix_add( &out, 0.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 2);
    g_assert_cmpint(out.currentMaxSample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_offset() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };

    audio_frame a = {
        .frameData = a_data,
        .fullMinSample = 1, .fullMaxSample = 5,
        .currentMinSample = 1, .currentMaxSample = 5,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 3, .currentMaxSample = 7,
        .channelCount = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 1);
    g_assert_cmpint(out.currentMaxSample, ==, 7);

    g_assert_cmpfloat( out_data[0], ==, 0.0f );
    g_assert_cmpfloat( out_data[1], ==, 1.0f );
    g_assert_cmpfloat( out_data[2], ==, 7.0f );
    g_assert_cmpfloat( out_data[3], ==, 7.0f );
    g_assert_cmpfloat( out_data[4], ==, 7.0f );
    g_assert_cmpfloat( out_data[5], ==, 2.0f );
    g_assert_cmpfloat( out_data[6], ==, 1.0f );
}

static void
test_add_basic_offset_attenuate() {
    float a_data[5] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };

    audio_frame a = {
        .frameData = a_data,
        .fullMinSample = 6, .fullMaxSample = 10,
        .currentMinSample = 6, .currentMaxSample = 10,
        .channelCount = 1,
    };

    audio_frame out = {
        .frameData = out_data,
        .fullMinSample = 1, .fullMaxSample = 7,
        .currentMinSample = 3, .currentMaxSample = 7,
        .channelCount = 1,
    };

    audio_mix_add( &out, 2.0f, &a, 0.5f, 5 );

    // Check it
    g_assert_cmpint(out.fullMinSample, ==, 1);
    g_assert_cmpint(out.fullMaxSample, ==, 7);
    g_assert_cmpint(out.currentMinSample, ==, 1);
    g_assert_cmpint(out.currentMaxSample, ==, 7);

    g_assert_cmpfloat( out_data[0], ==, 0.25f );
    g_assert_cmpfloat( out_data[1], ==, 0.5f );
    g_assert_cmpfloat( out_data[2], ==, 11.0f );
    g_assert_cmpfloat( out_data[3], ==, 9.5f );
    g_assert_cmpfloat( out_data[4], ==, 8.0f );
    g_assert_cmpfloat( out_data[5], ==, 4.0f );
    g_assert_cmpfloat( out_data[6], ==, 2.0f );
}


void
test_setup_audio_mix() {
    g_test_add_func( "/audio/mix/copy_frame/basic_expand", test_copy_frame_basic_expand );
    g_test_add_func( "/audio/mix/copy_frame/basic_offset", test_copy_frame_basic_offset );
    g_test_add_func( "/audio/mix/copy_frame/stereo_reduce_channels", test_copy_frame_stereo_reduce_channels );
    g_test_add_func( "/audio/mix/copy_frame/stereo_expand_channels", test_copy_frame_stereo_expand_channels );
    g_test_add_func( "/audio/mix/copy_frame_attenuate/basic_expand", test_copy_frame_attenuate_basic_expand );
    g_test_add_func( "/audio/mix/copy_frame_attenuate/stereo_reduce_channels", test_copy_frame_attenuate_stereo_reduce_channels );
    g_test_add_func( "/audio/mix/copy_frame_attenuate/stereo_expand_channels", test_copy_frame_attenuate_stereo_expand_channels );
    g_test_add_func( "/audio/mix/add/basic", test_add_basic );
    g_test_add_func( "/audio/mix/add/basic_empty_in", test_add_basic_empty_in );
    g_test_add_func( "/audio/mix/add/basic_empty_out", test_add_basic_empty_out );
    g_test_add_func( "/audio/mix/add/basic_zero_in", test_add_basic_zero_in );
    g_test_add_func( "/audio/mix/add/basic_zero_out", test_add_basic_zero_out );
    g_test_add_func( "/audio/mix/add/basic_offset", test_add_basic_offset );
    g_test_add_func( "/audio/mix/add/basic_offset_attenuate", test_add_basic_offset_attenuate );
}

