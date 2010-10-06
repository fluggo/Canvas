.. highlight:: c

*****
Video
*****

Video frames
============

There are three types of video frames. All store the same information, just in
slightly different ways.

The basic type is :c:type:`rgba_frame_f16`, which stores pixel data as half-float
:c:type:`rgba_f16` values. Half-float, which stores three decimal digits of precision,
is the most detail that the Fluggo Media library will preserve, and should be
sufficient for all but the most demanding image applications.

There are two other image types, provided for convenience: :c:type:`rgba_frame_f32`,
which uses the more easily-computed 32-bit float type, and :c:type:`rgba_frame_gl`,
which stores the image in a half-float OpenGL texture.

The frame-fetching functions :c:func:`video_get_frame_f16`, :c:func:`video_get_frame_f32`,
and :c:func:`video_get_frame_gl` check what types of frames a source supports and
converts between the different frame types as needed.

.. c:type:: rgba_frame_f32

    The :c:type:`rgba_frame_f32` structure is used to store a frame of video as
    32-bit floating-point pixels::

        typedef struct {
            rgba_f32 *data;
            box2i full_window;
            box2i current_window;
        } rgba_frame_f32;

    .. c:member:: rgba_f32 *data

        Buffer where the pixel data is stored. It is at least big enough to store
        all of the pixels in the :c:member:`full_window`. The data is stored row by row,
        each row the width of the window, with no space between the rows. A pixel's
        address can be calculated with :c:func:`video_get_pixel_f32`.

    The rectangles covered by the frame are defined by the :c:member:`full_window`
    and the :c:member:`current_window`. The current window defines which pixels
    in the full window are valid. Pixels outside the current window should be
    considered zero.

    .. c:member:: box2i full_window

        The box covered by the pixels in :c:member:`data`. The upper-left pixel
        in the window is the first pixel in :c:member:`data`.

    .. c:member:: box2i current_window

        The box of pixels inside :c:member:`full_window` that is valid. If either
        dimension of this window is empty (max < min), the entire frame is invalid,
        and the :c:member:`current_window` doesn't have any relation to the
        :c:member:`full_window`.

.. c:type:: rgba_frame_f16

    The :c:type:`rgba_frame_f16` structure is used to store a frame of video as
    16-bit floating-point pixels::

        typedef struct {
            rgba_f16 *data;
            box2i full_window;
            box2i current_window;
        } rgba_frame_f16;

    This structure is exactly analogous to :c:type:`rgba_frame_f32`.

.. c:function:: static inline rgba_f16 *video_get_pixel_f16(rgba_frame_f16 *frame, int x, int y)

    Get a pointer to the pixel at *x* and *y* in frame *frame*.

    This function doesn't check the parameters. If you want to dereference the
    returned pointer, *x* and *y* should be inside the :c:member:`full_window`.

.. c:function:: static inline rgba_f32 *video_get_pixel_f32(rgba_frame_f32 *frame, int x, int y)

    Same as :c:func:`video_get_pixel_f16`, but for 32-bit frames.

Compositing
-----------

.. c:function:: void video_copy_frame_f16(rgba_frame_f16 *out, rgba_frame_f16 *in)

.. c:function:: void video_copy_frame_alpha_f32(rgba_frame_f32 *out, rgba_frame_f32 *in, float alpha)

.. c:function:: void video_mix_cross_f32_pull(rgba_frame_f32 *out, video_source *a, int frame_a, video_source *b, int frame_b, float mix_b)

.. c:function:: void video_mix_cross_f32(rgba_frame_f32 *out, rgba_frame_f32 *a, rgba_frame_f32 *b, float mix_b)

.. c:function:: void video_mix_over_f32(rgba_frame_f32 *out, rgba_frame_f32 *b, float mix_b)

Transforms
----------

.. c:function:: void video_scale_bilinear_f32(rgba_frame_f32 *target, v2f target_point, rgba_frame_f32 *source, v2f source_point, v2f factors)

.. c:function:: void video_scale_bilinear_f32_pull(rgba_frame_f32 *target, v2f target_point, video_source *source, int frame, box2i *source_rect, v2f source_point, v2f factors)

Transfer functions
------------------

.. c:function:: const uint8_t *video_get_gamma45_ramp()

    Return a generic gamma ramp for converting linear half values to 8-bit values suitable
    for display on most monitors::

        uint8_t *ramp = video_get_gamma45_ramp();
        half input;

        uint8_t output = ramp[input];

    The ramp function is currently:

    .. math::

        Y'(L) = \begin{cases}
            0& L < 0\\
            255L^{0.45}& 0 \leq L \leq 1\\
            255& L > 1
          \end{cases}

    ...but it may be changed to an sRGB transfer function in the future.

.. c:function:: void video_transfer_linear_to_sRGB(const half *in, half *out, size_t count)

    Map *count* :c:type:`half` values from *in* to *out*, converting from
    linear to encoded using the sRGB transfer function:

    .. math::

        Y'(L) = \begin{cases}
            12.92L& L \leq 0.0031308\\
            1.055L^{\frac{1}{2.4}}-0.055& L > 0.0031308
          \end{cases}

    The sRGB function is appropriate for encoding for display on computer screens
    in bright environments. Source values should nominally be in the range [0.0, 1.0],
    but can be outside this range.

Rec. 709
""""""""

.. c:function:: void video_transfer_linear_to_rec709(const half *in, half *out, size_t count)

    Map *count* :c:type:`half` values from *in* to *out*, converting from
    linear to encoded using the Rec. 709 transfer function with scene intent:

    .. math::

        Y'(L) = \begin{cases}
            4.5L& L < 0.018\\
            1.099L^{0.45} - 0.099& L \geq 0.018
          \end{cases}

    The Rec. 709 function is appropriate for encoding for display on televisions
    or monitors in dim environments. Source values should nominally be in the range
    [0.0, 1.0], but can be outside this range.

.. c:function:: void video_transfer_rec709_to_linear_scene(const half *in, half *out, size_t count)

    Map *count* :c:type:`half` values from *in* to *out*, converting from
    encoded to linear values using the inverse Rec. 709 transfer function with scene intent:

    .. math::

        L(Y') = \begin{cases}
            \frac{Y'}{4.5}& Y' < 4.5(0.018)\\
            \frac{Y'+0.099}{1.099}^\frac{1}{0.45}& Y' \geq 4.5(0.018)
          \end{cases}

    Use this function to recover the original luminance values from a Rec. 709-encoded scene.
    Source values should nominally be in the range [0.0, 1.0], but can be outside this range.

.. c:function:: void video_transfer_rec709_to_linear_display(const half *in, half *out, size_t count)

    Map *count* :c:type:`half` values from *in* to *out*, converting from
    encoded to linear using the Rec. 709 transfer function with display intent.

    In effect, this is just the normal CRT display transfer function:

    .. math::

        L(Y') = Y'^{2.5}

    Use this function to obtain the expected luminance values when a Rec. 709-encoded scene is displayed.
    Source values should nominally be in the range [0.0, 1.0], but can be outside this range.

Video sources
=============

A video source is a set of functions and an object pointer that can produce an indexed frame
at random.

A video source is a virtual function table combined with an object pointer::

    typedef void (*video_get_frame_func)( void *self, int frame_index, rgba_frame_f16 *frame );
    typedef void (*video_get_frame_32_func)( void *self, int frame_index, rgba_frame_f32 *frame );
    typedef void (*video_get_frame_gl_func)( void *self, int frame_index, rgba_frame_gl *frame );

    typedef struct {
        int flags;            // Reserved, should be zero
        video_get_frame_func get_frame;
        video_get_frame_32_func get_frame_32;
        video_get_frame_gl_func get_frame_gl;
    } video_frame_source_funcs;

    typedef struct {
        void *obj;
        video_frame_source_funcs *funcs;
    } video_source;

Fetching frames
---------------

.. c:function:: void video_get_frame_f16(video_source *source, int frame_index, rgba_frame_f16 *frame)

    Fetch frame *frame_index* into :c:type:`rgba_frame_f16` *frame* from video source *source*.

    Before you call, set :c:member:`full_window` to the window you want to retrieve
    from the source, and allocate memory for the :c:member:`data` member.
    :c:func:`video_get_frame_f16` will set :c:member:`current_window` before it returns.

.. c:function:: void video_get_frame_f32(video_source *source, int frame_index, rgba_frame_f32 *frame)

    Same as :c:func:`video_get_frame_f16`, but for 32-bit frames.

.. c:function:: void video_get_frame_gl(video_source *source, int frame_index, rgba_frame_gl *frame)

    Fetch frame *frame_index* into :c:type:`rgba_frame_gl` *frame* from video source *source*.

    Before you call, set :c:member:`full_window` to the window you want to retrieve
    from the source. :c:func:`video_get_frame_gl` allocate a texture and set
    :c:member:`texture` and :c:member:`current_window` before it returns.

