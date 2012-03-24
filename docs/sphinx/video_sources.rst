************************
Using video sources in C
************************

The functions and structures needed for working with pure C video functions are found in ``framework.h``. Use ``pyframework.h`` for Python C code.

.. contents::

Video frames in C
=================

.. highlight:: c

All video frames carry three pieces of information:

* *Frame data* -- Floating-point RGBA data describing the image.
* *Full data window* -- The rectangle covered by the frame data. The coordinate space for frames is effectively infinite-- windows describe where and how big each frame is.
* *Current data window* -- The rectangle (in the same coordinate space) containing valid data.

There are three kinds of video frames, which store the same data in different
ways: as 16-bit float data (half floats), as 32-bit float data, and as an OpenGL
texture.

Fluggo Media considers all three to be equivalent, and will freely convert
between all three. This means that the most precision you can be *guaranteed* to
get is half precision, which is about three or four decimal places.

The 16-bit frame structure in C is::

    typedef struct {
        rgba_f16 *data;
        box2i full_window;
        box2i current_window;
    } rgba_frame_f16;

The 32-bit frame has the same structure, but uses :c:type:`rgba_f32` instead.
The frame data covered by the full data window but not the current data window
might contain junk data.

The OpenGL structure is different::

    typedef struct {
        GLuint texture;
        box2i full_window;
        box2i current_window;
    } rgba_frame_gl;

Here, *texture* is a reference to an OpenGL texture the same size as the
*full_window*. :c:type:`rgba_frame_gl` frames always have valid data for the
whole texture, but the area outside the current data window is guaranteed to be
transparent.

Functions for working with frames
---------------------------------

The :c:type:`box2i` functions are all useful for working with data windows.

The math for finding a pixel inside a frame is messy, so the functions
:c:func:`video_get_pixel_f16` and :c:func:`video_get_pixel_f32` will return a pointer to a pixel::

    static inline rgba_f16 *video_get_pixel_f16( rgba_frame_f16 *frame, int x, int y );
    static inline rgba_f32 *video_get_pixel_f32( rgba_frame_f32 *frame, int x, int y );

Only mess with pixels inside the *full_window*.

A common technique for processing each pixel in a frame goes like this::

    rgba_frame_f32 frame;

    for( int y = frame.current_window.min.y; y <= frame.current_window.max.y; y++ ) {
        // Get a pointer to the pixel at (0, y), which might be outside the data window
        rgba_f32 *row = video_get_pixel_f32( out, 0, y );

        for( int x = frame.current_window.min.x; x <= frame.current_window.max.x; x++ ) {
            // Process the pixel at row[x]
        }
    }

Video sources
=============

Video sources in C are defined by two structures, :c:type:`video_source` and
:c:type:`VideoFrameSourceFuncs`.

:c:type:`video_source` holds an arbitrary pointer *obj* and a pointer to a
:c:type:`VideoFrameSourceFuncs` structure::

    typedef struct {
        void *obj;
        VideoFrameSourceFuncs *funcs;
    } video_source;

:c:type:`VideoFrameSourceFuncs` contains pointers to functions which take *obj*
as their first parameter::

    typedef struct {
        int flags;            // Reserved, should be zero
        video_getFrameFunc getFrame;
        video_getFrame32Func getFrame32;
        video_getFrameGLFunc getFrameGL;
    } VideoFrameSourceFuncs;

Not all of the functions will be defined for every video source. In fact, a
video source is only required to provide *getFrame* or *getFrame32*.

You don't normally call these functions directly. Fluggo Media provides three
functions which will retrieve the kind of frame you're after::

    void video_get_frame_f16( video_source *source, int frame_index, rgba_frame_f16 *frame );
    void video_get_frame_f32( video_source *source, int frame_index, rgba_frame_f32 *frame );
    void video_get_frame_gl( video_source *source, int frame_index, rgba_frame_gl *frame );

If the source doesn't provide the kind of frame directly, these functions will
retrieve the frame with a different function and convert it to your type.

:c:func:`video_get_frame_f16` and :c:func:`video_get_frame_f32` require you to
allocate space for the frame you want. You need to set *data* and *full_window*
before calling these functions. The function will set *current_window* before it
returns.

With :c:func:`video_get_frame_gl`, the upstream source allocates a half-float
RGBA rectangle texture to match the given *full_window*. All of the texture is
valid, but parts of the image outside *current_window* are transparent (alpha is
zero). Calls to :c:func:`video_get_frame_gl` require a valid current OpenGL
context for the calling thread.

:c:type:`video_source` pointers are used in the ``cprocess`` (pure C99) part of
the library. Video operations often have two versions of each function that
operates on video: one that accepts frames, and another that accepts
:c:type:`video_source` pointers, especially if there are conditions that might
make an upstream call unnecessary.

Video sources and Python
------------------------

:c:type:`video_source` structures are rare in the pure C world. They are usually
used to refer to source functions found on Python objects. This use is very
handy, as it makes it very easy to accept video sources declared in other modules.

pyframework.h defines an additional structure and function for working with
Python video sources::

    typedef struct {
        video_source source;
        PyObject *csource;
    } VideoSourceHolder;

    bool py_video_take_source( PyObject *source, VideoSourceHolder *holder );

To work with a video source provided to you as a PyObject pointer, declare a
:c:type:`VideoSourceHolder` and pass it and the object to :c:func:`py_video_take_source`.
:c:func:`py_video_take_source` will increase the reference count on the object
and return true if it successfully produces a :c:type:`video_source` in the
*source* member. If it can't interpret the object as a video source, it will
return false.

Once you have the source, you can use the ``video_get_frame_*`` functions on it,
or pass it to other functions that accept :c:type:`video_source` pointers. When
you are done, release the reference by calling ``py_video_take_source( NULL, &holder )``.

Using video sources in Python
=============================

There are four kinds of sources in the Fluggo Media library: video, audio,
presentation clocks, and frame functions.

Video sources represent any collection of images-- even still images-- as an
indexed set of frames, almost always starting at zero. Sources are
random-access, and don't rely on any negotiation: once the source exists, you
can request any frame from it at any time.

The basic filters and video sources are found in the
:py:mod:`fluggo.media.process` module. Most should derive from
:py:class:`fluggo.media.process.VideoSource`, but this isn't a requirement.
Video sources are designed to work well with Python, but for the most part, you
don't work with the video itself in Python. You will typically set up a tree of
sources and filters in Python and let the library do the rest of the work for
you.

Requesting frames
-----------------

.. highlight: python

There are several ways to use video sources. For sources that derive from
:py:class:`VideoSource`, you can call :py:meth:`VideoSource.get_frame_f16(frame, data_window)`,
where *frame* is the index of the frame you want, and *data_window* is a
:py:class:`box2i` defining what part of the frame you're after::

    from fluggo.media import process

    source = SolidColorVideoSource(rgba(1.0, 0.0, 0.0, 0.5), box2i(50, 50, 100, 100))
    frame = source.get_frame_f16(0, box2i(0, 0, 150, 150))

The result is an :py:class:`RgbaFrameF16`. You can use the methods and attributes
to inspect the frame::

    print frame.current_window, frame.pixel(55, 55)

Or you can even use the frame itself as a source::

    # Produce an identical copy of frame
    frame2 = frame.get_frame_f16(0, frame.current_window)

Chaining and filters
--------------------

The above method can be used to produce stills or thumbnails, but the usual way
to use sources will be to *chain* sources together and then use the last source
in the chain on a playback control or some other output.

For example::

    from fluggo.media import process, libav

    videro = libav.AVVideoSource('softboiled01;03;21;24.avi')
    pulldown = process.Pulldown23RemovalFilter(videro, 0)
    mix = VideoMixFilter(src_a=pulldown, src_b=SolidColorVideoSource(rgba(1.0, 0.0, 0.0, 0.5), box2i(50, 50, 100, 100)), mix_b=LinearFrameFunc(a=1/300.0, b=0))

This example uses four sources. :py:class:`AVVideoSource` is a source that reads
a video file using Libav. We use it as input to :py:class:`Pulldown23RemovalFilter`,
which will remove 2:3 pulldown in the file. That output then becomes input to a
:py:class:`VideoMixFilter`, along with a :py:class:`SolidColorVideoSource`. The
mix filter will mix between the two. The :py:class:`LinearFrameFunc` is a
different kind of source called a *frame function*-- it supplies a mixing factor
that changes over several frames.

We could then use the mix output in a call to :py:func:`process.writeVideo` or
on the GTK or Qt4 playback controls.

Video sources can be used as many times as you like. A source doesn't care and
has no way of knowing who is downstream of it.

Coordinates
-----------

Video sources work on an infinite XY plane. A given source will typically only
produce valid data for some region.

