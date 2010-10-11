.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.media.process

Video support
=============

The media library deals with floating-point video data, which *video sources*
produce as *video frames*. (See :ref:`framework` for an overview.)
When you request a frame, you specify a rectangular region of the frame (the *window*)
you're interested in. The source returns a video frame with as much of the
data as the source defines.

You don't normally have to deal with video frames directly.
You would usually chain several video sources together
and let them do the work of passing video data back and forth. If you need to
examine or display the data, though, video sources provide two methods
which will allow you to retrieve the data as a :class:`RgbaFrameF16` or
:class:`RgbaFrameF32` object. (Both classes are also video sources. They produce
the same captured frame for all frame indexes.)

.. class:: VideoSource

    :class:`VideoSource` is the base class for audio sources.

    :class:`VideoSource` has two public methods for retrieving video frames:

    .. method:: get_frame_f16(frame_index, data_window)
        get_frame_f32(frame_index, data_window)

        Produce the part of frame *frame_index* that appears inside *data_window*,
        a :class:`~fluggo.media.basetypes.box2i`. :meth:`get_frame_f16` produces
        a :class:`RgbaFrameF16` object and :meth:`get_frame_f32` produces a
        :class:`RgbaFrameF32` object. Hardware acceleration is not used.

.. class:: RgbaFrameF32

    :class:`RgbaFrameF32` contains a frame of video in 32-bit floating point channels.
    While this is useful for video filters that need to do heavy processing on the
    image, it's not very useful as a container type since it uses a lot of memory.
    It's also not all that useful as a video source, since the frame data it contains
    may be converted to 16-bit float data at any time. For almost all uses, you
    should use :class:`RgbaFrameF16` instead.

    :class:`RgbaFrameF32` has the following public attributes:

    .. attribute:: full_window

        Read-only. A :class:`~fluggo.media.basetypes.box2i` value with the full data window allocated for
        the frame.

    .. attribute:: current_window

        Read-only. A :class:`~fluggo.media.basetypes.box2i` value with the data window for which the frame
        is defined. Data outside this window but inside :attr:`full_window`
        is undefined and probably junk. (It's usually safe to consider everything
        outside of the :attr:`current_window` to be transparent black, i.e.,
        ``rgba(0, 0, 0, 0)``.)

        If :attr:`current_window` is empty, then there is no valid data in the frame.

    It also has a few methods for retrieving pixel data:

    .. method:: pixel(x, y)

        Get the value of the pixel at (*x*, *y*) (see :ref:`framework` for a
        description of the coordinate system) as an :class:`~fluggo.media.basetypes.rgba`.
        If (*x*, *y*) is outside the :attr:`current_window` for this frame, ``None`` is returned.

        The index for (*x*, *y*) is given by::

            (y - frame.full_window.min.y) * frame.full_window.width + x - frame.full_window.min.x

    .. describe:: frame[index]

        Get raw data from the frame at *index*.

.. class:: RgbaFrameF16

    :class:`RgbaFrameF16` provides the same functionality as :class:`RgbaFrameF32`,
    but uses half the memory since it stores a frame of video in 16-bit floating
    point channels. It also has an additional method:

    .. method:: to_argb_string()

        This convenience method produces the :attr:`~RgbaFrameF32.current_window`
        as a string (bytes object in Python 3) using 8-bit premultiplied packed ARGB.
        The image is transformed from linear to 8-bit using a simple gamma ramp.
        The result is suitable for use with the QImage class in Qt.


