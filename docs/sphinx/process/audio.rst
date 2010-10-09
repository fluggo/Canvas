.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.media.process

Audio support
=============

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



