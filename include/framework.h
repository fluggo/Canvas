/*
    This file is part of the Fluggo Media Library for high-quality
    video and audio processing.

    Copyright 2009 Brian J. Crowell <brian@fluggo.com>

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

#if !defined(fluggo_framework)
#define fluggo_framework

#include <stdint.h>
#include <stdbool.h>
#include "half.h"

#if defined(WINNT)
#define EXPORT __attribute__((dllexport))
#else
#define EXPORT __attribute__((visibility("default")))
#endif

#include <glib.h>

#include <GL/glew.h>
#include <GL/gl.h>

#define NS_PER_SEC    INT64_C(1000000000)

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    int n;
    unsigned int d;
} rational;

typedef struct {
    int x, y;
} v2i;

typedef struct {
    v2i min, max;
} box2i;

typedef struct {
    float x, y;
} v2f;

typedef struct {
    v2f min, max;
} box2f;

G_GNUC_CONST static inline int min( int a, int b ) {
    return a < b ? a : b;
}

G_GNUC_CONST static inline int max( int a, int b ) {
    return a > b ? a : b;
}

static inline void box2i_set( box2i *box, int minX, int minY, int maxX, int maxY ) {
    box->min.x = minX;
    box->min.y = minY;
    box->max.x = maxX;
    box->max.y = maxY;
}

static inline void box2i_set_empty( box2i *box ) {
    box2i_set( box, 0, 0, -1, -1 );
}

G_GNUC_PURE static inline bool box2i_is_empty( const box2i *box ) {
    return box->max.x < box->min.x || box->max.y < box->min.y;
}

static inline void box2i_intersect( box2i *result, const box2i *first, const box2i *second ) {
    result->min.x = max(first->min.x, second->min.x);
    result->min.y = max(first->min.y, second->min.y);
    result->max.x = min(first->max.x, second->max.x);
    result->max.y = min(first->max.y, second->max.y);
}

static inline void box2i_union( box2i *result, const box2i *first, const box2i *second ) {
    result->min.x = min(first->min.x, second->min.x);
    result->min.y = min(first->min.y, second->min.y);
    result->max.x = max(first->max.x, second->max.x);
    result->max.y = max(first->max.y, second->max.y);
}

static inline void box2i_normalize( box2i *result ) {
    int temp;

    if( result->min.x > result->max.x ) {
        temp = result->min.x - 1;
        result->min.x = result->max.x + 1;
        result->max.x = temp;
    }

    if( result->min.y > result->max.y ) {
        temp = result->min.y - 1;
        result->min.y = result->max.y + 1;
        result->max.y = temp;
    }
}

static inline void box2i_get_size( const box2i *box, v2i *result ) {
    result->x = (box->max.x < box->min.x) ? 0 : (box->max.x - box->min.x + 1);
    result->y = (box->max.y < box->min.y) ? 0 : (box->max.y - box->min.y + 1);
}

G_GNUC_CONST static inline float minf( float a, float b ) {
    return a < b ? a : b;
}

G_GNUC_CONST static inline float maxf( float a, float b ) {
    return a > b ? a : b;
}

G_GNUC_CONST static inline float clampf( float value, float min, float max ) {
    return minf(maxf(value, min), max);
}

int64_t get_frame_time( const rational *frame_rate, int frame );
int get_time_frame( const rational *frame_rate, int64_t time );

/************* Video *******/
typedef struct {
    half r, g, b, a;
} rgba_f16;

typedef struct {
    uint8_t r, g, b, a;
} rgba_u8;

typedef struct {
    float r, g, b, a;
} rgba_f32;

typedef struct {
    rgba_f16 *data;
    box2i full_window;
    box2i current_window;
} rgba_frame_f16;

typedef struct {
    rgba_f32 *data;
    box2i full_window;
    box2i current_window;
} rgba_frame_f32;

typedef struct {
    GLuint texture;
    box2i full_window;
    box2i current_window;
} rgba_frame_gl;

typedef void (*video_get_frame_func)( void *self, int frame_index, rgba_frame_f16 *frame );
typedef void (*video_get_frame_32_func)( void *self, int frame_index, rgba_frame_f32 *frame );
typedef void (*video_get_frame_gl_func)( void *self, int frame_index, rgba_frame_gl *frame );

typedef struct {
    int flags;            // Reserved, should be zero
    video_get_frame_func get_frame;
    video_get_frame_32_func get_frame_32;
    video_get_frame_gl_func get_frame_gl;
} video_frame_source_funcs;

G_GNUC_PURE static inline rgba_f16 *video_get_pixel_f16( rgba_frame_f16 *frame, int x, int y ) {
    return &frame->data[
        (y - frame->full_window.min.y) *
            (frame->full_window.max.x - frame->full_window.min.x + 1) +
        x - frame->full_window.min.x];
}

G_GNUC_PURE static inline rgba_f32 *video_get_pixel_f32( rgba_frame_f32 *frame, int x, int y ) {
    return &frame->data[
        (y - frame->full_window.min.y) *
            (frame->full_window.max.x - frame->full_window.min.x + 1) +
        x - frame->full_window.min.x];
}

typedef struct {
    void *obj;
    video_frame_source_funcs *funcs;
} video_source;

void video_get_frame_f16( video_source *source, int frame_index, rgba_frame_f16 *frame );
void video_get_frame_f32( video_source *source, int frame_index, rgba_frame_f32 *frame );
void video_get_frame_gl( video_source *source, int frame_index, rgba_frame_gl *frame );
const uint8_t *video_get_gamma45_ramp();

void video_copy_frame_f16( rgba_frame_f16 *out, rgba_frame_f16 *in );
void video_copy_frame_alpha_f32( rgba_frame_f32 *out, rgba_frame_f32 *in, float alpha );
void video_mix_cross_f32_pull( rgba_frame_f32 *out, video_source *a, int frame_a, video_source *b, int frame_b, float mix_b );
void video_mix_cross_f32( rgba_frame_f32 *out, rgba_frame_f32 *a, rgba_frame_f32 *b, float mix_b );
void video_mix_over_f32( rgba_frame_f32 *out, rgba_frame_f32 *b, float mix_b );

void video_scale_bilinear_f32( rgba_frame_f32 *target, v2f target_point, rgba_frame_f32 *source, v2f source_point, v2f factors );
void video_scale_bilinear_f32_pull( rgba_frame_f32 *target, v2f target_point, video_source *source, int frame, box2i *source_rect, v2f source_point, v2f factors );

// Transfer functions
void video_transfer_rec709_to_linear_scene( const half *in, half *out, size_t count );
void video_transfer_rec709_to_linear_display( const half *in, half *out, size_t count );
void video_transfer_linear_to_rec709( const half *in, half *out, size_t count );
void video_transfer_linear_to_sRGB( const half *in, half *out, size_t count );

// OpenGL utility routines
void *getCurrentGLContext();

#define gl_checkError()        __gl_checkError(__FILE__, __LINE__)
void __gl_checkError(const char *file, const unsigned long line);

void gl_printShaderErrors( GLhandleARB shader );
void gl_renderToTexture( rgba_frame_gl *frame );
void gl_buildShader( const char *source, GLhandleARB *outShader, GLhandleARB *outProgram );

/************* Audio *******/

/*
    Structure: audio_frame
    A frame of audio, containing one or more audio samples.

    data - Buffer where the sample data is stored. It is at least
        sizeof(float) * channelCount * (fullMaxSample - fullMinSample + 1)
        bytes. Channels are interleaved; a sample can be found at
        data[(sample - fullMinSample) * channelCount + channel].
    channels - The number of channels in the buffer. Note that this is
        constant: An audio source should not set this to the number of channels
        it can produce, but rather produce zero samples for channels it
        does not have.
    full_min_sample - The index of the sample found at frameData[0].
    full_max_sample - The index of the last possible sample in the buffer,
        which is at frameData[(fullMaxSample - fullMinSample) * channelCount].
    current_min_sample - The index of the first valid sample in the buffer.
    current_max_sample - The index of the last valid sample in the buffer.
*/

typedef struct {
    float *data;
    int channels;
    int full_min_sample, full_max_sample;
    int current_min_sample, current_max_sample;
} audio_frame;

typedef void (*audio_getFrameFunc)( void *self, audio_frame *frame );

G_GNUC_PURE static inline float *
audio_get_sample( const audio_frame *frame, int sample, int channel ) {
    return &frame->data[(sample - frame->full_min_sample) * frame->channels + channel];
}

typedef struct {
    int flags;            // Reserved, should be zero
    audio_getFrameFunc getFrame;
} AudioFrameSourceFuncs;

typedef struct {
    void *obj;
    AudioFrameSourceFuncs *funcs;
} audio_source;

void audio_get_frame( const audio_source *source, audio_frame *frame );

extern AudioFrameSourceFuncs audio_frame_as_source_funcs;

#define AUDIO_FRAME_AS_SOURCE(frame)  { .obj = frame, .funcs = &audio_frame_as_source_funcs }

/*
    Function: audio_copy_frame
    Copy a frame into another frame (already allocated) with a given offset.

    out - Destination frame.
    in - Source frame.
    offset - Offset, in samples, of the source frame relative to the destination frame.
        An offset of 500, for example, would copy source sample 500 to destination sample
        0, 501 to 1, and so on.
*/
void audio_copy_frame( audio_frame *out, const audio_frame *in, int offset );

/*
    Function: audio_copy_frame
    Copy a frame into another frame (already allocated) with a given offset and attenuation.

    out - Destination frame.
    in - Source frame.
    factor - Factor to multiply input samples by. Specify 1.0 for a direct copy.
    offset - Offset, in samples, of the source frame relative to the destination frame.
        An offset of 500, for example, would copy source sample 500 to destination sample
        0, 501 to 1, and so on.
*/
void audio_copy_frame_attenuate( audio_frame *out, const audio_frame *in, float factor, int offset );

/*
    Function: audio_attenuate
    Attenuate an existing frame.

    frame - Frame to attenuate
    factor - Factor to multiply input samples by.
*/
void audio_attenuate( audio_frame *frame, float factor );

/*
    Function: audio_mix_add
    Adds two audio frames.

    out - First frame to mix, and the frame to receive the result.
    a - Second frame to mix.
    mix_a - Attenuation on the second frame.
    offset - Offset, in samples, of frame A relative to the destination frame.
*/
void audio_mix_add( audio_frame *out, const audio_frame *a, float mix_a, int offset );

void audio_mix_add_pull( audio_frame *out, const audio_source *a, float mix_a, int offset_a );


/************ Codec packet source ******/

#define PACKET_TS_NONE      INT64_C(0x8000000000000000)

typedef struct {
    void *data;
    int length;
    int64_t pts, dts;
    bool keyframe;

    GFreeFunc free_func;
} codec_packet;

typedef codec_packet *(*codec_getNextPacketFunc)( void *self );
typedef bool (*codec_seekFunc)( void *self, int64_t frame );

/*
    codec.getHeader:
    Get the header data found in the source, if any.

    buffer: Buffer in which to store header data. May be NULL.

    If buffer is NULL, returns the size of the header in bytes, or
    zero if there is no header.
    If buffer is not NULL, returns 1 on success and 0 on failure.
*/
typedef int (*codec_getHeaderFunc)( void *self, void *buffer );

typedef struct {
    int flags;          // Reserved, should be zero
    codec_getNextPacketFunc getNextPacket;
    codec_seekFunc seek;
    codec_getHeaderFunc getHeader;
} codec_packet_source_funcs;

typedef struct {
    void *obj;
    codec_packet_source_funcs *funcs;
} codec_packet_source;

/************ Coded image source ******/

#define CODED_IMAGE_MAX_PLANES 4

typedef struct {
    void *data[CODED_IMAGE_MAX_PLANES];
    int stride[CODED_IMAGE_MAX_PLANES];
    int line_count[CODED_IMAGE_MAX_PLANES];

    GFreeFunc free_func;
} coded_image;

/*
    Function: coded_image_alloc
    Allocates a coded_image, along with the storage for all of its planes.

    Parameters:
    strides - Pointer to an array containing the stride of each plane. Strides can be zero.
    line_counts - Pointer to an array containing the number of lines in each plane. Can also be zero.
    count - Number of planes in strides and line_counts. Should be less than or equal to CODED_IMAGE_MAX_PLANES.

    Returns:
    A pointer to a new coded_image, if successful.
    The planes will be uninitialized.
    Call free_func on the resulting image to free it.
*/
G_GNUC_MALLOC coded_image *coded_image_alloc( const int *strides, const int *line_counts, int count );

/*
    Function: coded_image_alloc
    Allocates a coded_image and its planes and zeroes the memory.

    Parameters:
    strides - Pointer to an array containing the stride of each plane. Strides can be zero.
    line_counts - Pointer to an array containing the number of lines in each plane. Can also be zero.
    count - Number of planes in strides and line_counts. Should be less than or equal to CODED_IMAGE_MAX_PLANES.

    Returns:
    A pointer to a new coded_image, if successful.
    The planes will be initialized with zeroes.
    Call free_func on the resulting image to free it.
*/
G_GNUC_MALLOC coded_image *coded_image_alloc0( const int *strides, const int *line_counts, int count );

typedef coded_image *(*coded_image_getFrameFunc)( void *self, int frame );

typedef struct {
    int flags;
    coded_image_getFrameFunc getFrame;
} coded_image_source_funcs;

typedef struct {
    void *obj;
    coded_image_source_funcs *funcs;
} coded_image_source;

// Video subsampling/reconstruction
void video_reconstruct_dv( coded_image *planar, rgba_frame_f16 *frame );
coded_image *video_subsample_dv( rgba_frame_f16 *frame );


/******** Presentation clocks ****/

#define CLK_LOOP    0x1

typedef struct {
    int64_t playbackMin, playbackMax;
    int64_t loopMin, loopMax;
    int flags;
} ClockRegions;

typedef int64_t (*clock_getPresentationTimeFunc)( void *self );
typedef void (*clock_getSpeedFunc)( void *self, rational *result );
typedef void (*clock_getRegionsFunc)( void *self, ClockRegions *result );
typedef void (*clock_callback_func)( void *data, rational *speed, int64_t time );
typedef void *(*clock_register_callback_func)( void *self, clock_callback_func callback, void *data, GDestroyNotify notify );
typedef void *(*clock_unregister_callback_func)( void *self, void *handle );

typedef struct {
    clock_getPresentationTimeFunc getPresentationTime;
    clock_getSpeedFunc getSpeed;
    clock_getRegionsFunc getRegions;
    clock_register_callback_func register_callback;
    clock_unregister_callback_func unregister_callback;
} PresentationClockFuncs;

typedef struct {
    void *obj;
    PresentationClockFuncs *funcs;
} presentation_clock;

int64_t gettime();


/********** Workspace ***/

typedef struct workspace_t_tag workspace_t;
typedef struct workspace_iter_t_tag workspace_iter_t;
typedef struct workspace_item_t_tag workspace_item_t;

workspace_t *workspace_create();
gint workspace_get_length( workspace_t *workspace );
workspace_item_t *workspace_add_item( workspace_t *self, gpointer source, int64_t x, int64_t width, int64_t offset, int64_t z, gpointer tag );
workspace_item_t *workspace_get_item( workspace_t *self, gint index );
void workspace_remove_item( workspace_item_t *item );
void workspace_as_video_source( workspace_t *workspace, video_source *source );
void workspace_free( workspace_t *workspace );
void workspace_get_item_pos( workspace_item_t *item, int64_t *x, int64_t *width, int64_t *z );
int64_t workspace_get_item_offset( workspace_item_t *item );
void workspace_set_item_offset( workspace_item_t *item, int64_t offset );
gpointer workspace_get_item_source( workspace_item_t *item );
void workspace_set_item_source( workspace_item_t *item, gpointer source );
gpointer workspace_get_item_tag( workspace_item_t *item );
void workspace_set_item_tag( workspace_item_t *item, gpointer tag );
void workspace_update_item( workspace_item_t *item, int64_t *x, int64_t *width, int64_t *z, int64_t *offset, gpointer *source, gpointer *tag );
void workspace_as_video_source( workspace_t *workspace, video_source *source );
void workspace_as_audio_source( workspace_t *workspace, audio_source *source );


/*********** GL widget ***/

typedef struct __tag_widget_gl_context widget_gl_context;

typedef void (*invalidate_func)( void *closure );

widget_gl_context *widget_gl_new();
void widget_gl_free( widget_gl_context *self );

gboolean widget_gl_get_hard_mode_supported( widget_gl_context *self );
gboolean widget_gl_get_hard_mode_enabled( widget_gl_context *self );
void widget_gl_hard_mode_enable( widget_gl_context *self, gboolean enable );
void widget_gl_get_display_window( widget_gl_context *self, box2i *display_window );
void widget_gl_set_display_window( widget_gl_context *self, box2i *display_window );
void widget_gl_set_video_source( widget_gl_context *self, video_source *source );
void widget_gl_set_presentation_clock( widget_gl_context *self, presentation_clock *clock );
float widget_gl_get_pixel_aspect_ratio( widget_gl_context *self );
void widget_gl_set_pixel_aspect_ratio( widget_gl_context *self, float pixel_aspect_ratio );

void widget_gl_draw( widget_gl_context *self, v2i widget_size );
void widget_gl_set_invalidate_func( widget_gl_context *self, invalidate_func func, void *closure );

float widget_gl_get_rendering_intent( widget_gl_context *self );
void widget_gl_set_rendering_intent( widget_gl_context *self, float rendering_intent );


/*********** FIR filters ***/

typedef struct {
    // Coefficients of the filter's taps
    float *coeff;

    // Number of taps in the filter (length of the coeff array)
    int width;

    // Index of the center tap
    int center;
} fir_filter;

/*
    Creates an FIR triangle filter suitable for 1:sub supersampling or sub:1 subsampling.

    Specify an offset of zero to have the filter centered on a sample. A nonzero offset
    will move the center by the specified fraction of taps. (filter->center will point to
    the tap that *would* have been the center)

    If filter->coeff is null, the coefficient array will be allocated for you.
    When you're done with the filter, free its coefficients with filter_free.

    If you would rather specify your own array, fill filter->coeff with the array and
    filter->width with its size. If the array is not big enough, coeff will be unaltered,
    center will be -1, and width will be the required size to hold the filter.
*/
void filter_createTriangle( float sub, float offset, fir_filter *filter );

void filter_createLanczos( float sub, int kernel_size, float offset, fir_filter *filter );

/*
    Frees the coefficients
*/
void filter_free( fir_filter *filter );



#if defined(__cplusplus)
}
#endif

#endif

