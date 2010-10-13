.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.media.process

Audio support
=============

:class:`AudioSource` and :class:`AudioFrame`
--------------------------------------------

The media library deals with floating-point audio data, which *audio sources*
produce as *audio frames*. (See :ref:`framework` for an overview.)
When you request a frame, you specify the starting point
and the length of the frame (which samples you want) and the number of channels of
audio you're interested in. The source returns an audio frame with as much of the
data as the source defines.

You don't normally have to deal with audio frames directly.
You would usually chain several audio sources together
and let them do the work of passing audio data back and forth. If you need to
examine or display the data, though, audio sources provide a method :meth:`~AudioSource.get_frame`
which will allow you to retrieve the data as an :class:`AudioFrame` object. (For
what it's worth, an :class:`AudioFrame` is also an audio source for the data it
contains, but it's probably a bad idea to use it that way.)

.. class:: AudioSource

    :class:`AudioSource` is the base class for audio sources.
    :class:`AudioSource` has one public method:

    .. method:: get_frame(min_sample, max_sample, channels)

        Return an :class:`AudioFrame` with *channels* channels of audio, from
        *min_sample* to *max_sample*.

.. class:: AudioFrame

    :class:`AudioFrame` has the following public attributes:

    .. attribute:: full_min_sample
        full_max_sample

        Read-only. The range of samples the frame was allocated for.

    .. attribute:: current_min_sample
        current_max_sample

        Read-only. The range of samples for which the frame is defined. Data outside this
        range but inside :attr:`full_min_sample` and :attr:`full_max_sample`
        is undefined and probably junk.

        As a special case, :attr:`current_max_sample` is less than :attr:`current_min_sample`
        if there is no valid data in the frame.

    .. attribute:: channels

        Read-only. The number of channels the frame contains.

    It also has a few methods for retrieving sample data:

    .. method:: sample(sample, channel)

        Get the value of sample *sample* on *channel*. *channel* should be between
        0 and :attr:`channels`--- if it isn't, an :class:`IndexError` is raised.
        If *sample* is outside the valid samples for this frame, ``None`` is returned.

        The index for *sample* and *channel* is given by
        ``(sample - frame.full_min_sample) * frame.channels + channel``.

    .. describe:: frame[index]

        Get raw data from the frame at *index*.

:class:`AudioPassThroughFilter` --- Pass through another source
---------------------------------------------------------------

One of the simplest audio filters is the pass-through filter, which simply passes
a frame request to another source. This filter can be used as a base class, which
makes it useful for making your own source-like class.

.. class:: AudioPassThroughFilter(source)

    Return a filter that passes requests to *source*.

    :class:`AudioPassThroughFilter` has the following methods:

    .. method:: source()

        Return the current source.

    .. method:: set_source(source)

        Set the source to *source*.

:class:`AudioWorkspace` --- Compose an arrangement of audio clips
-----------------------------------------------------------------

An :class:`AudioWorkspace` is a convenient way of composing a set of audio clips
across time. Add your clips to the workspace as items, setting when they begin
how long they last, and where to pull them from their original source. The workspace
will add overlapping clips together when audio data is requested.

.. class:: AudioWorkspace

    An audio source that combines audio clips.

    :class:`AudioWorkspace` has the following public attributes:

    .. method:: add(source[, offset=0, x=0, width=0, tag=None])

        Add a new item to the workspace using source *source* starting *offset*
        samples in. The item is placed at *x* and runs for *width* samples. An
        optional *tag* object lets you associate user data with the item. The new
        item is returned (see :class:`AudioWorkspaceItem`).

    .. method:: remove(item)

        Remove *item* from the workspace.

    .. describe:: len(workspace)

        Get the number of items in the workspace.

    .. describe:: workspace[i]

        Get the item at index *i*. Note that the items in the workspace are in
        no particular order, and the order may change when items are added, moved,
        or removed.

.. class:: AudioWorkspaceItem

    An :class:`AudioWorkspaceItem` has these public attributes:

    .. attribute:: x

        Read-only. Sample in the workspace where the item starts.

    .. attribute:: width

        Read-only. Length of the item in samples.

    .. attribute:: tag

        Optional user data for the item, can be any Python object. Defaults to ``None``.

    .. attribute:: source

        Audio source for the item.

    .. attribute:: offset

        Sample in :attr:`source` where the item starts.

    It also has this method:

    .. method:: update(**kw)

        Update one or more of the item's properties---any of *x*, *width*, *source*, *offset*,
        or *tag*.



