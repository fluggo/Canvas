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

#include "framework.h"

/************
    audio_copy_frame
************/

static void
test_copy_frame_basic_expand() {
    float in_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .channels = 1,
    };

    audio_copy_frame( &out, &in, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( in_data[i], ==, out_data[i + 1] );
    }
}

static void
test_copy_frame_basic_offset() {
    float in_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame in = {
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .channels = 1,
    };

    audio_copy_frame( &out, &in, 3 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 1);
    g_assert_cmpint(out.current_max_sample, ==, 3);

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
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 2,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .channels = 1,
    };

    audio_copy_frame( &out, &in, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 2);
    g_assert_cmpint(out.full_max_sample, ==, 6);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 2,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .channels = 3,
    };

    audio_copy_frame( &out, &in, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 2);
    g_assert_cmpint(out.full_max_sample, ==, 6);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .channels = 1,
    };

    audio_copy_frame_attenuate( &out, &in, 0.5f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 2,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .channels = 1,
    };

    audio_copy_frame_attenuate( &out, &in, 0.5f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 2);
    g_assert_cmpint(out.full_max_sample, ==, 6);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = in_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 2,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .channels = 3,
    };

    audio_copy_frame_attenuate( &out, &in, 0.5f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 2);
    g_assert_cmpint(out.full_max_sample, ==, 6);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = a_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = dummy_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 4, .current_max_sample = 3,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

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
        .data = dummy_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 0.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( test_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_empty_out() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame a = {
        .data = a_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 5, .current_max_sample = 4,
        .channels = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_zero_out() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame a = {
        .data = a_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_mix_add( &out, 0.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_basic_offset() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };

    audio_frame a = {
        .data = a_data,
        .full_min_sample = 1, .full_max_sample = 5,
        .current_min_sample = 1, .current_max_sample = 5,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 3, .current_max_sample = 7,
        .channels = 1,
    };

    audio_mix_add( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 1);
    g_assert_cmpint(out.current_max_sample, ==, 7);

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
        .data = a_data,
        .full_min_sample = 6, .full_max_sample = 10,
        .current_min_sample = 6, .current_max_sample = 10,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 3, .current_max_sample = 7,
        .channels = 1,
    };

    audio_mix_add( &out, 2.0f, &a, 0.5f, 5 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 1);
    g_assert_cmpint(out.current_max_sample, ==, 7);

    g_assert_cmpfloat( out_data[0], ==, 0.25f );
    g_assert_cmpfloat( out_data[1], ==, 0.5f );
    g_assert_cmpfloat( out_data[2], ==, 11.0f );
    g_assert_cmpfloat( out_data[3], ==, 9.5f );
    g_assert_cmpfloat( out_data[4], ==, 8.0f );
    g_assert_cmpfloat( out_data[5], ==, 4.0f );
    g_assert_cmpfloat( out_data[6], ==, 2.0f );
}


/************
    audio_mix_add_pull
************/

static void
test_add_pull_basic() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float b_data[5] = { 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };
    float out_data[7] = { 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 9.0f };

    audio_frame a_frame = {
        .data = a_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );
    audio_mix_add_pull( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i] + b_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_pull_basic_empty_in() {
    float dummy_data[5] = { 12.0f, 12.0f, 12.0f, 12.0f, 12.0f };
    float test_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 9.0f };

    audio_frame a_frame = {
        .data = dummy_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 4, .current_max_sample = 3,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );
    audio_mix_add_pull( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( test_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_pull_basic_zero_in() {
    float dummy_data[5] = { 12.0f, 12.0f, 12.0f, 12.0f, 12.0f };
    float test_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 9.0f };

    audio_frame a_frame = {
        .data = dummy_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );
    audio_mix_add_pull( &out, 1.0f, &a, 0.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( test_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_pull_basic_empty_out() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame a_frame = {
        .data = a_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 5, .current_max_sample = 4,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );
    audio_mix_add_pull( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_pull_basic_zero_out() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f };

    audio_frame a_frame = {
        .data = a_data,
        .full_min_sample = 2, .full_max_sample = 6,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 2, .current_max_sample = 6,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );

    audio_mix_add_pull( &out, 0.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 2);
    g_assert_cmpint(out.current_max_sample, ==, 6);

    for( int i = 0; i < 5; i++ ) {
        g_assert_cmpfloat( a_data[i], ==, out_data[i + 1] );
    }
}

static void
test_add_pull_basic_offset() {
    float a_data[5] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };

    audio_frame a_frame = {
        .data = a_data,
        .full_min_sample = 1, .full_max_sample = 5,
        .current_min_sample = 1, .current_max_sample = 5,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 3, .current_max_sample = 7,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );

    audio_mix_add_pull( &out, 1.0f, &a, 1.0f, 0 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 1);
    g_assert_cmpint(out.current_max_sample, ==, 7);

    g_assert_cmpfloat( out_data[0], ==, 0.0f );
    g_assert_cmpfloat( out_data[1], ==, 1.0f );
    g_assert_cmpfloat( out_data[2], ==, 7.0f );
    g_assert_cmpfloat( out_data[3], ==, 7.0f );
    g_assert_cmpfloat( out_data[4], ==, 7.0f );
    g_assert_cmpfloat( out_data[5], ==, 2.0f );
    g_assert_cmpfloat( out_data[6], ==, 1.0f );
}

static void
test_add_pull_basic_offset_attenuate() {
    float a_data[5] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f };
    float out_data[7] = { 9.0f, 9.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f };

    audio_frame a_frame = {
        .data = a_data,
        .full_min_sample = 6, .full_max_sample = 10,
        .current_min_sample = 6, .current_max_sample = 10,
        .channels = 1,
    };

    audio_frame out = {
        .data = out_data,
        .full_min_sample = 1, .full_max_sample = 7,
        .current_min_sample = 3, .current_max_sample = 7,
        .channels = 1,
    };

    audio_source a = AUDIO_FRAME_AS_SOURCE( &a_frame );
    audio_mix_add_pull( &out, 2.0f, &a, 0.5f, 5 );

    // Check it
    g_assert_cmpint(out.full_min_sample, ==, 1);
    g_assert_cmpint(out.full_max_sample, ==, 7);
    g_assert_cmpint(out.current_min_sample, ==, 1);
    g_assert_cmpint(out.current_max_sample, ==, 7);

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
    g_test_add_func( "/audio/mix/add_pull/basic", test_add_pull_basic );
    g_test_add_func( "/audio/mix/add_pull/basic_empty_in", test_add_pull_basic_empty_in );
    g_test_add_func( "/audio/mix/add_pull/basic_empty_out", test_add_pull_basic_empty_out );
    g_test_add_func( "/audio/mix/add_pull/basic_zero_in", test_add_pull_basic_zero_in );
    g_test_add_func( "/audio/mix/add_pull/basic_zero_out", test_add_pull_basic_zero_out );
    g_test_add_func( "/audio/mix/add_pull/basic_offset", test_add_pull_basic_offset );
    g_test_add_func( "/audio/mix/add_pull/basic_offset_attenuate", test_add_pull_basic_offset_attenuate );
}

