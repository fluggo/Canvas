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

:class:`EmptyVideoSource` --- Produce blank video
-------------------------------------------------

.. class:: EmptyVideoSource

    The simplest source is the :class:`EmptyVideoSource`, which takes no parameters
    and only produces empty frames.

:class:`SolidColorVideoSource` --- Produce solid colors
-------------------------------------------------------

.. class:: SolidColorVideoSource(color[, window])

    Return a video source that produces frames filled with *color*.
    If *window* is specified, the color is restrained to that area of the frame,
    otherwise, the color covers the entire frame.

:class:`Pulldown23RemovalFilter` --- Remove 2:3 pulldown from interlaced video
------------------------------------------------------------------------------

When working with 24 fps content, it's best to remove any pulldown (interlacing)
present so that you can work with progressive frames. This filter does just that.

Remember that there is nothing about a source or frame that indicates whether it
is interlaced or not. :class:`Pulldown23RemovalFilter` assumes its source is
interlaced. Remember also that the order of fields in interlaced content is determined
by its placement in the frame; see :ref:`framework` for details.

.. class:: Pulldown23RemovalFilter(source, cadence_offset)

    Return a video source that removes 2:3 pulldown from interlaced source *source*,
    starting at *cadence_offset*, which is one of:

    ==========================  === === === === ===
    ``cadence_offset``          0   1   2   3   4
    ==========================  === === === === ===
    First field/second field    AA  BB  BC  CD  DD
    Whole/split                 W   W   S   S   W
    ==========================  === === === === ===

    The filter will produce four frames for every five in the source. If the source
    starts or ends on a split, the incomplete field will be discarded.

    :class:`Pulldown23RemovalFilter` offers a method that helps calculate what
    the new length of a sequence will be:

    .. method:: get_new_length(old_length)

        Return the number of complete frames the filter will produce if the source
        has *old_length* frames and you start reading at frame zero.

:class:`VideoSequence` --- Combine video clips in sequence
----------------------------------------------------------

:class:`VideoSequence` is a way of arranging sections of video one after another.
For a more powerful tool, see :class:`VideoWorkspace`, which can also composite
clips that appear at the same time.

.. class:: VideoSequence

    A :class:`VideoSequence` acts as a list of clips, where the clips are represented
    as 3-tuples: (*source*, *offset*, *length*). The *source* is a video source for
    that clip, *offset* is the frame in *source* where the clip will start, and
    *length* is the number of frames to produce from that source.

    :class:`VideoSequence` has these public methods:

    .. describe:: (source, offset, length) = seq[index]

        Get the clip at *index*.

    .. describe:: seq[index] = (source, offset, length)

        Set the clip at *index*.

    .. method:: append(clip)

        Add *clip* to the end of the sequence, where *clip* is a tuple.

    .. method:: insert(index, clip)

        Insert *clip* at *index*.

    .. method:: get_start_frame(index)

        Return the frame index at which the clip at *index* will start.

:class:`VideoWorkspace` --- Arrange and composite multiple clips
----------------------------------------------------------------

A :class:`VideoWorkspace` is a way of composing a set of video clips
across time. Add your clips to the workspace as items, setting when they begin
how long they last, and where to pull them from their original source. The workspace
will composite overlapping clips when producing the resulting video.

.. class:: VideoWorkspace

    A source that combines video clips.

    :class:`VideoWorkspace` has the following public attributes:

    .. method:: add(source[, offset=0, x=0, width=0, z=0, tag=None])

        Add a new item to the workspace using source *source* starting *offset*
        frames in. The item starts at frame *x* and runs for *width* frames.
        If it overlaps with other video clips, *z* will be used to determine the
        compositing order. An
        optional *tag* object lets you associate user data with the item. The new
        item is returned (see :class:`VideoWorkspaceItem`).

    .. method:: remove(item)

        Remove *item* from the workspace.

    .. describe:: len(workspace)

        Get the number of items in the workspace.

    .. describe:: workspace[i]

        Get the item at index *i*. Note that the items in the workspace are in
        no particular order, and the order may change when items are added, moved,
        or removed.

.. class:: VideoWorkspaceItem

    A :class:`VideoWorkspaceItem` has these public attributes:

    .. attribute:: x

        Read-only. Sample in the workspace where the item starts.

    .. attribute:: width

        Read-only. Length of the item in frames.

    .. attribute:: z

        Relative order of the clip when compositing. Higher Z-order items composite
        on top of lower Z-order items.

    .. attribute:: tag

        Optional user data for the item, can be any Python object. Defaults to ``None``.

    .. attribute:: source

        Video source for the item.

    .. attribute:: offset

        Frame in :attr:`source` where the item starts.

    It also has this method:

    .. method:: update(**kw)

        Update one or more of the item's properties---any of ``x``, ``width``,
        ``z``, ``source``, ``offset``, or ``tag``.



