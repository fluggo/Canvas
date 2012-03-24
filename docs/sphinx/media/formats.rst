.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.media.formats

****************************************************************
:mod:`fluggo.media.formats` --- Stream formats and properties
****************************************************************

:class:`StreamFormat` --- Stream format description
===================================================

.. class:: StreamFormat(type)

    A :class:`StreamFormat` can describe one of the streams in a
    :class:`ContainerFormat`, but can also stand on its own to describe the format
    of streams in a composition. *type* is the type of stream, ``'video'`` or
    ``'audio'``.

    A stream format has two basic attributes common to all stream types:

    .. attribute:: type

        The type of the stream, ``'video'`` or ``'audio'``.

    .. attribute:: length

        The length of the stream in frames or samples, or None if the stream
        has no length (continuous, like a solid color source) or the length is
        unknown.

    Most of a format's properties are divided into two dictionary attributes:

    .. attribute:: detected

        This dictionary contains the properties that the codec determined or
        guessed when it opened the file. For custom formats, this might be empty.

    .. attribute:: override

        The properties the user has set on the stream, which take precedence over
        the properties in :attr:`detected` when both are specified.

    This division is arbitrary. The important thing is that when you read the
    format directly, you check :attr:`override` first, then `detected`. The
    :meth:`get` method does this for you:

    .. method:: get(property[, default=None])

        Get property *property* with default *default* if it's not found in
        detected or override properties.

    Property keys can be arbitrary strings, but the known properties are found
    in the :class:`ContainerProperty`, :class:`VideoProperty`, and
    :class:`AudioProperty` classes.

    :class:`StreamFormat` has a few helper attributes to get certain properties
    for you:

    .. attribute:: index

        The :attr:`ContainerProperty.STREAM_INDEX`.

.. class:: ContainerFormat

    A :class:`ContainerFormat` is a lot like a :class:`StreamFormat`, with "detected"
    and "override" property dictionaries. It also has a :attr:`streams` attribute:

    .. attribute:: streams

        A list of :class:`StreamFormat` objects for each (identifiable) stream in
        the container. The list isn't necessarily in any order; users should check
        the :attr:`StreamFormat.index` on the stream.

.. class:: ContainerProperty

    Defines properties that relate to containers.

    .. attribute:: STREAM_INDEX

        Appears on streams and defines the zero-based index of the stream in the
        container. Value is ``'stream_index'``.

    .. attribute:: FORMAT

        A string identifying the format of the container, such as "avi" or "dv".
        These should come from :class:`KnownContainerFormat` when possible.
        If that class doesn't define a value for your container, prefix your name
        with 'x-'. The value of :attr:`FORMAT` is ``'format'``.

    .. attribute:: MUXER

        Identifies a specific muxer for encoding or decoding this :attr:`FORMAT`.
        In Canvas, this is the name of the plugin followed by a forward slash and
        the plugin-specific name for the muxer, such as ``'libav/dv'``.

        For decode:

        * In the "detected" dictionary, the muxer that identified and decoded the
          container.
        * In the "override" dictionary, if specified, the muxer that should be used
          to decode this container.

        For encode:

        * A specific muxer used to encode. The dictionary should also contain any parameters
          the user has set for the muxer.

.. class:: KnownContainerFormat

    Well-known container types that can show up in the :attr:`ContainerProperty.FORMAT`
    property.

    .. attribute:: AVI

        Value ``'avi'``. The Microsoft AVI format.

    .. attribute:: DV

        Value ``'dv'``. A raw DV stream.

.. class:: AudioProperty

    .. attribute:: FORMAT

        A string identifying the format of the stream, such as "dv" or "mpeg2".
        These should come from :class:`KnownAudioFormat` when possible.
        If that class doesn't define a value for your stream, prefix your name
        with 'x-'. The value of :attr:`FORMAT` is ``'format'``.

    .. attribute:: CODEC

        Identifies a specific codec for encoding or decoding this :attr:`FORMAT`.
        In Canvas, this is the name of the plugin followed by a forward slash and
        the plugin-specific name for the codec, such as ``'libav/dv'``.

        For decode:

        * In the "detected" dictionary, a default codec that should be able to
          read the stream.
        * In the "override" dictionary, if specified, the codec that should be used
          to decode this stream.

        For encode:

        * A specific codec used to encode. The dictionary should also contain any parameters
          the user has set for the codec.

    .. attribute:: SAMPLE_RATE

        An integer specifying the sample rate in Hz.

.. class:: VideoProperty

    .. attribute:: FORMAT

        The same as for :attr:`AudioProperty.FORMAT`.

    .. attribute:: CODEC

        The same as for :attr:`AudioProperty.CODEC`.

    .. attribute:: FRAME_RATE

        A :class:`fractions.Fraction` specifying the number of frames per second.

    .. attribute:: SAMPLE_ASPECT_RATIO

        The ratio of the width of a pixel to its height as a :class:`fractions.Fraction`.
