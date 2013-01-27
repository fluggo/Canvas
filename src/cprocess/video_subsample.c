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

#include <string.h>
#include "framework.h"

static void
free_coded_image( coded_image *image ) {
    g_assert(image);

    for( int i = 0; i < CODED_IMAGE_MAX_PLANES; i++ ) {
        if( image->data[i] )
            g_slice_free1( image->stride[i] * image->line_count[i], image->data[i] );
    }

    g_slice_free( coded_image, image );
}

static coded_image *
coded_image_alloc_impl( const int *strides, const int *line_counts, int count, bool zero ) {
    g_assert(strides);
    g_assert(line_counts);
    g_assert(count <= CODED_IMAGE_MAX_PLANES);

    coded_image *result = g_slice_new0( coded_image );

    for( int i = 0; i < count; i++ ) {
        g_assert(strides[i] >= 0);
        g_assert(line_counts[i] >= 0);

        result->stride[i] = strides[i];
        result->line_count[i] = line_counts[i];

        if( strides[i] == 0 || line_counts[i] == 0 )
            continue;

        if( zero )
            result->data[i] = g_slice_alloc0( strides[i] * line_counts[i] );
        else
            result->data[i] = g_slice_alloc( strides[i] * line_counts[i] );
    }

    result->free_func = (GFreeFunc) free_coded_image;

    return result;
}

EXPORT coded_image *
coded_image_alloc( const int *strides, const int *line_counts, int count ) {
    return coded_image_alloc_impl( strides, line_counts, count, false );
}

EXPORT coded_image *
coded_image_alloc0( const int *strides, const int *line_counts, int count ) {
    return coded_image_alloc_impl( strides, line_counts, count, true );
}


typedef struct {
    float cb, cr;
} cbcr_f32;

float
studio_float_to_chroma8( float chroma ) {
    return chroma * 224.0f + 128.0f;
}

float
studio_float_to_luma8( float luma ) {
    return luma * 219.0f + 16.0f;
}

/*
    Function: video_subsample_dv
    Subsamples to planar standard-definition NTSC DV:

    720x480 YCbCr
    4:1:1 subsampling, co-sited with left pixel
    Rec 709 matrix
    Rec 709 transfer function
*/
EXPORT coded_image *
video_subsample_dv( rgba_frame_f16 *frame ) {
    const int full_width = 720, full_height = 480;

    // RGB->Rec. 709 YPbPr matrix in Poynton, p. 315:
    const float colorMatrix[3][3] = {
        {  0.2126f,    0.7152f,    0.0722f   },
        { -0.114572f, -0.385428f,  0.5f      },
        {  0.5f,      -0.454153f, -0.045847f }
    };

    // Offset the frame so that line zero is part of the first field
    const v2i picOffset = { 0, -1 };

    // Set up subsample support
    const int subX = 4;
    const float subOffsetX = 0.0f;

    const int strides[3] = { full_width, full_width / subX, full_width / subX };
    const int line_counts[3] = { full_height, full_height, full_height };

    // Set up the current window
    box2i window = {
        { max( picOffset.x, frame->current_window.min.x ),
          max( picOffset.y, frame->current_window.min.y ) },
        { min( full_width + picOffset.x - 1, frame->current_window.max.x ),
          min( full_height + picOffset.y - 1, frame->current_window.max.y ) }
    };
    int window_width = window.max.x - window.min.x + 1;

    coded_image *planar = coded_image_alloc0( strides, line_counts, 3 );

    // BJC: What follows is the horizontal-subsample-only case
    fir_filter triangleFilter = { NULL };
    filter_createTriangle( 1.0f / (float) subX, subOffsetX, &triangleFilter );

    // Temp rows aligned to the input window [window.min.x, window.max.x]
    rgba_f32 *tempRow = g_slice_alloc( sizeof(rgba_f32) * window_width );
    cbcr_f32 *tempChroma = g_slice_alloc( sizeof(cbcr_f32) * window_width );

    // Turn into half RGB
    for( int row = window.min.y - picOffset.y; row <= window.max.y - picOffset.y; row++ ) {
        uint8_t *yrow = (uint8_t*) planar->data[0] + (row * planar->stride[0]);
        uint8_t *cbrow = (uint8_t*) planar->data[1] + (row * planar->stride[1]);
        uint8_t *crrow = (uint8_t*) planar->data[2] + (row * planar->stride[2]);

        rgba_f16 *in = video_get_pixel_f16( frame, window.min.x, row + picOffset.y );

        video_transfer_linear_to_rec709( &in->r, &in->r, (sizeof(rgba_f16) / sizeof(half)) * window_width );
        rgba_f16_to_f32( tempRow, in, window_width );

        for( int x = 0; x < window_width; x++ ) {
            float y = studio_float_to_luma8(
                tempRow[x].r * colorMatrix[0][0] +
                tempRow[x].g * colorMatrix[0][1] +
                tempRow[x].b * colorMatrix[0][2] );
            tempChroma[x].cb =
                tempRow[x].r * colorMatrix[1][0] +
                tempRow[x].g * colorMatrix[1][1] +
                tempRow[x].b * colorMatrix[1][2];
            tempChroma[x].cr =
                tempRow[x].r * colorMatrix[2][0] +
                tempRow[x].g * colorMatrix[2][1] +
                tempRow[x].b * colorMatrix[2][2];

            yrow[x + window.min.x] = (uint8_t) y;
        }

        for( int tx = window.min.x / subX; tx <= window.max.x / subX; tx++ ) {
            float cb = 0.0f, cr = 0.0f;

            for( int sx = max(window.min.x, tx * subX - triangleFilter.center);
                sx <= min(window.max.x, tx * subX + (triangleFilter.width - triangleFilter.center - 1)); sx++ ) {

                cb += tempChroma[sx - window.min.x].cb * triangleFilter.coeff[sx - tx * subX + triangleFilter.center];
                cr += tempChroma[sx - window.min.x].cr * triangleFilter.coeff[sx - tx * subX + triangleFilter.center];
            }

            cbrow[tx] = (uint8_t) studio_float_to_chroma8( cb );
            crrow[tx] = (uint8_t) studio_float_to_chroma8( cr );
        }
    }

    filter_free( &triangleFilter );
    g_slice_free1( sizeof(rgba_f32) * window_width, tempRow );
    g_slice_free1( sizeof(cbcr_f32) * window_width, tempChroma );

    return planar;
}

/*
    MPEG2 subsampling: Really this is just a quick stand-in that does MPEG2-style
    4:2:0 subsampling with Rec. 709 components on interlaced video. There are a
    lot of things that should be configurable in this.

    The luma-shader pretty much just shifts the picture into place and does the color
    transforms.
*/
static const char *vertex_shader_text =
"#version 120\n"
"uniform ivec2 tex_offset;\n"
"uniform ivec2 frame_size;\n"
"uniform ivec2 frame_offset;\n"
"attribute vec2 position;\n"
"varying vec2 tex_coord;\n"
"varying vec2 frame_coord;\n"
"\n"
"void main() {\n"
"    gl_Position = vec4(position * vec2(2.0, 2.0) + vec2(-1.0, -1.0), 0.0, 1.0);\n"
"    tex_coord = position * vec2(frame_size) + frame_offset + tex_offset;\n"
"    frame_coord = position * vec2(frame_size) + frame_offset;\n"
"}\n";

static const char *subsample_mpeg2_luma_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"uniform sampler2DRect tex;\n"
"varying vec2 tex_coord;\n"
"uniform mat3 rgb2yuv;\n"

/*
    const float __transition = 0.018f;

    if( in < __transition )
        return in * 4.5f;

    return 1.099f * powf( in, 0.45f ) - 0.099f;
*/
"vec3 linear_to_rec709( vec3 color ) {"
"    return mix("
"        color * 4.5f,"
"        1.099f * pow( color, vec3(0.45f) ) - 0.099f,"
"        step(0.018f, color));"
"}"

"void main() {"
"    float y = (linear_to_rec709(texture2DRect( tex, tex_coord ).rgb) * rgb2yuv).r;"
"    y = y * (219.0f / 255.0f) + (16.0f / 255.0f);"

"    gl_FragColor.r = y;"
"}";

static const char *subsample_mpeg2_chroma_shader_text =
"#version 120\n"
"#extension GL_ARB_texture_rectangle : enable\n"
"varying vec2 tex_coord;\n"
"varying vec2 frame_coord;\n"
"uniform sampler2DRect tex;\n"
"uniform mat3 rgb2yuv;\n"
"uniform float result0;\n"

/*
    const float __transition = 0.018f;

    if( in < __transition )
        return in * 4.5f;

    return 1.099f * powf( in, 0.45f ) - 0.099f;
*/
"vec3 linear_to_rec709( vec3 color ) {"
"    return mix("
"        color * 4.5f,"
"        1.099f * pow( color, vec3(0.45f) ) - 0.099f,"
"        step(0.018f, color));"
"}"

"void main() {"
// luma_coord points to the even-numbered luma sample above the current chroma sample
// in the source texture.
"    vec2 luma_coord = (tex_coord - vec2(0.5)) * vec2(2.0) + vec2(0.5);"
// frame_coord is going to be at the texel locations; that is, (0.5, 0.5)
// for the first texel and so on. Given luma_coord, these vertical offsets will yield
// the luma samples near the chroma or far from it.
// Example: Chroma at (0,0) has luma_coord=(0,0) (really 0.5,0.5) is near luma at (0,0) and far from luma at (0,2).
// Chroma at (0,1) has luma_coord=(0,2), but being odd, is near luma at (0,3) and far from luma at (0,1).
// Near is 0.0 for even lines and 1.0 for odd. Far is 2.0 for even and -1.0 for odd.
"    float near_offset = sin(frame_coord.y * 3.14156) * -0.5 + 0.5;"
"    float far_offset = sin(frame_coord.y * 3.14156) * 1.5 + 0.5;"

// Simple dumb linear combination
"    vec2 cbcr ="
"        (3.0/16.0) * (linear_to_rec709(texture2DRect(tex, luma_coord + vec2(-1.0, near_offset)).rgb) * rgb2yuv).gb + "
"        (6.0/16.0) * (linear_to_rec709(texture2DRect(tex, luma_coord + vec2( 0.0, near_offset)).rgb) * rgb2yuv).gb + "
"        (3.0/16.0) * (linear_to_rec709(texture2DRect(tex, luma_coord + vec2( 1.0, near_offset)).rgb) * rgb2yuv).gb + "
"        (1.0/16.0) * (linear_to_rec709(texture2DRect(tex, luma_coord + vec2(-1.0,  far_offset)).rgb) * rgb2yuv).gb + "
"        (2.0/16.0) * (linear_to_rec709(texture2DRect(tex, luma_coord + vec2( 0.0,  far_offset)).rgb) * rgb2yuv).gb + "
"        (1.0/16.0) * (linear_to_rec709(texture2DRect(tex, luma_coord + vec2( 1.0,  far_offset)).rgb) * rgb2yuv).gb;"
"    cbcr = cbcr * (224.0f / 255.0f) + (128.0f / 255.0f);"

"    gl_FragData[0].r = mix(cbcr.r, cbcr.g, step(0.5f, result0));"
"    gl_FragData[1].r = cbcr.g;"
"}";

typedef struct {
    struct {
        GLuint program;
        GLint tex_uniform, rgb2yuv_uniform, result0_uniform,
            frame_offset_uniform, tex_offset_uniform, frame_size_uniform;
        GLint position_attrib;
    } luma, chroma;

    GLuint framebuffer;
    GLuint vertex_buffer;
} gl_shader_state;

static void destroy_shader( gl_shader_state *shader ) {
    // We assume that we're in the right GL context
    glDeleteProgram( shader->luma.program );
    glDeleteProgram( shader->chroma.program );
    glDeleteBuffers( 1, &shader->vertex_buffer );
    glDeleteFramebuffersEXT( 1, &shader->framebuffer );
    g_free( shader );
}

EXPORT coded_image *
video_subsample_mpeg2_gl( rgba_frame_gl *frame ) {
    GQuark shader_quark = g_quark_from_static_string( "cprocess::video_subsample::subsample_mpeg2_shaders" );

    GLint max_draw_buffers, max_color_attachments;
    glGetIntegerv( GL_MAX_COLOR_ATTACHMENTS, &max_color_attachments );
    glGetIntegerv( GL_MAX_DRAW_BUFFERS, &max_draw_buffers );

    v2i frame_size;
    box2i_get_size( &frame->full_window, &frame_size );

    void *context = getCurrentGLContext();
    gl_shader_state *shader = (gl_shader_state *) g_dataset_id_get_data( context, shader_quark );

    if( !shader ) {
        // Time to create the program for this context, any time now
        shader = g_new0( gl_shader_state, 1 );

        GLuint vshader = gl_compile_shader( GL_VERTEX_SHADER,
            vertex_shader_text, "MPEG2 subsample vertex shader" );

        GLuint luma_shader = gl_compile_shader( GL_FRAGMENT_SHADER,
            subsample_mpeg2_luma_shader_text, "MPEG2 luma subsample shader" );
        GLuint luma_shaders[2] = { vshader, luma_shader };
        shader->luma.program = gl_link_program( luma_shaders, 2, "MPEG2 luma subsample program" );
        glDeleteShader( luma_shader );

        shader->luma.tex_uniform = glGetUniformLocation( shader->luma.program, "tex" );
        shader->luma.rgb2yuv_uniform = glGetUniformLocation( shader->luma.program, "rgb2yuv" );
        shader->luma.frame_offset_uniform = glGetUniformLocation( shader->luma.program, "frame_offset" );
        shader->luma.frame_size_uniform = glGetUniformLocation( shader->luma.program, "frame_size" );
        shader->luma.tex_offset_uniform = glGetUniformLocation( shader->luma.program, "tex_offset" );
        shader->luma.position_attrib = glGetAttribLocation( shader->luma.program, "position" );

        GLuint chroma_shader = gl_compile_shader( GL_FRAGMENT_SHADER,
            subsample_mpeg2_chroma_shader_text, "MPEG2 chroma subsample shader" );
        GLuint chroma_shaders[2] = { vshader, chroma_shader };
        shader->chroma.program = gl_link_program( chroma_shaders, 2, "MPEG2 chroma subsample program" );
        glDeleteShader( chroma_shader );
        glDeleteShader( vshader );

        shader->chroma.tex_uniform = glGetUniformLocation( shader->chroma.program, "tex" );
        shader->chroma.rgb2yuv_uniform = glGetUniformLocation( shader->chroma.program, "rgb2yuv" );
        shader->chroma.result0_uniform = glGetUniformLocation( shader->chroma.program, "result0" );
        shader->chroma.frame_offset_uniform = glGetUniformLocation( shader->chroma.program, "frame_offset" );
        shader->chroma.frame_size_uniform = glGetUniformLocation( shader->chroma.program, "frame_size" );
        shader->chroma.tex_offset_uniform = glGetUniformLocation( shader->chroma.program, "tex_offset" );
        shader->chroma.position_attrib = glGetAttribLocation( shader->chroma.program, "position" );

        glGenFramebuffersEXT( 1, &shader->framebuffer );

        // Set up a static buffer with four corners for us to work with
        glGenBuffers( 1, &shader->vertex_buffer );

        v2f positions[4] = {
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 1.0f }
        };

        glBindBuffer( GL_ARRAY_BUFFER, shader->vertex_buffer );
        glBufferData( GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        g_dataset_id_set_data_full( context, shader_quark, shader, (GDestroyNotify) destroy_shader );
    }

    GLuint textures[3];
    glGenTextures( 3, textures );

    GLuint luma_tex = textures[0], cb_tex = textures[1], cr_tex = textures[2];

    // Offset the frame so that line zero is part of the first field, ok boss
    // FIXME: Whatever version of llvmpipe I'm using is doubling the lines at the
    // bottom of the frame; probably check into that
    v2i luma_size = { 720, 480 };
    v2i chroma_size = { 720 / 2, 480 / 2 };

    box2i source_window = frame->full_window,
        result_window = { { 0, 0 }, { luma_size.x - 1, luma_size.y - 1 } };

    v2i input_offset;
    v2i_subtract( &input_offset, &result_window.min, &source_window.min );

    // RGB->Rec. 709 YPbPr matrix in Poynton, p. 315:
    /*const float color_matrix[3][3] = {
        {  0.2126f,    0.7152f,    0.0722f   },
        { -0.114572f, -0.385428f,  0.5f      },
        {  0.5f,      -0.454153f, -0.045847f }
    };*/

    // RGB->Rec. 601 YPbPr matrix in Poynton, p. 304
    const float color_matrix[3][3] = {
        {  0.299f,     0.587f,     0.114f    },
        { -0.168736f, -0.331264f,  0.5f      },
        {  0.5f,      -0.418688f, -0.081312f }
    };

    const int strides[3] = { luma_size.x, chroma_size.x, chroma_size.x };
    const int line_counts[3] = { luma_size.y, chroma_size.y, chroma_size.y };

    coded_image *planar = coded_image_alloc0( strides, line_counts, 3 );

    // Define the output textures
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, luma_tex );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE8, luma_size.x, luma_size.y, 0,
        GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, cb_tex );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE8, chroma_size.x, chroma_size.y, 0,
        GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, cr_tex );
    glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE8, chroma_size.x, chroma_size.y, 0,
        GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL );

    // Set up the input textures
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, frame->texture );
    glEnable( GL_TEXTURE_RECTANGLE_ARB );

    // Set up vertex buffer
    glBindBuffer( GL_ARRAY_BUFFER, shader->vertex_buffer );

    //three o eight, oh me oh myo, I love jumbalayo, you pretty thingo, o me oh myo
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, shader->framebuffer );

    // Define the luma texture first
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB, luma_tex, 0 );

    glUseProgram( shader->luma.program );
    glUniform1i( shader->luma.tex_uniform, 0 );
    glUniformMatrix3fv( shader->luma.rgb2yuv_uniform, 1, false, &color_matrix[0][0] );
    glUniform2iv( shader->luma.tex_offset_uniform, 1, &input_offset.x );
    glUniform2iv( shader->luma.frame_size_uniform, 1, &luma_size.x );
    glUniform2i( shader->luma.frame_offset_uniform, 0, 0 );

    glEnableVertexAttribArray( shader->luma.position_attrib );
    glVertexAttribPointer( shader->luma.position_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0 );

    glViewport( 0, 0, luma_size.x, luma_size.y );
    glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

    glDisableVertexAttribArray( shader->luma.position_attrib );

    // Now paint to chromas
    glUseProgram( shader->chroma.program );
    glUniform1i( shader->chroma.tex_uniform, 0 );
    glUniformMatrix3fv( shader->chroma.rgb2yuv_uniform, 1, false, &color_matrix[0][0] );
    glUniform2iv( shader->chroma.tex_offset_uniform, 1, &input_offset.x );
    glUniform1f( shader->chroma.result0_uniform, 0.0f );
    glUniform2iv( shader->chroma.frame_size_uniform, 1, &chroma_size.x );
    glUniform2i( shader->chroma.frame_offset_uniform, 0, 0 );

    glEnableVertexAttribArray( shader->chroma.position_attrib );
    glVertexAttribPointer( shader->chroma.position_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0 );

    glViewport( 0, 0, chroma_size.x, chroma_size.y );

    if( max_draw_buffers >= 2 && max_color_attachments >= 2 ) {
        // Bind and use both chromas at the same time
        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, cb_tex, 0 );
        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT,
            GL_TEXTURE_RECTANGLE_ARB, cr_tex, 0 );

        GLenum buffers[] = { GL_COLOR_ATTACHMENT0_EXT, GL_COLOR_ATTACHMENT1_EXT };
        glDrawBuffers( 2, buffers );

        glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT,
            GL_TEXTURE_RECTANGLE_ARB, 0, 0 );

        glDrawBuffers( 1, buffers );
    }
    else {
        // The slow way; draw twice
        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, cb_tex, 0 );
        glUniform1f( shader->chroma.result0_uniform, 0.0f );
        glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

        // Select CR in the shader and draw it now
        glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, cr_tex, 0 );
        glUniform1f( shader->chroma.result0_uniform, 1.0f );
        glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
    }

    glDisableVertexAttribArray( shader->chroma.position_attrib );

    // Clean up
    glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB, 0, 0 );
    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, 0 );

    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, luma_tex );
    glGetTexImage( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, planar->data[0] );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, cb_tex );
    glGetTexImage( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, planar->data[1] );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, cr_tex );
    glGetTexImage( GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, planar->data[2] );
    glBindTexture( GL_TEXTURE_RECTANGLE_ARB, 0 );
    glDisable( GL_TEXTURE_RECTANGLE_ARB );

    glDeleteTextures( 3, textures );
    glUseProgram( 0 );

    return planar;
}

