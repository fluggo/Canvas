.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.editor.model

:class:`AssetStreamRef` --- Reference for sources
==================================================

This class holds a source name and stream index. 

.. class:: AssetStreamRef([source_name=None, stream_index=None])

	Serializable.
	
	Read-only attributes:

	.. attribute:: source_name

		The name of the source being referenced.

	.. attribute:: stream_index

		The index of the stream in the source.

:class:`Source` --- Base class for sources
==========================================

In Canvas, a *source* is an object that can produce streams. Every source has a
name (given to it by the :class:`SourceList`) and a set of keywords the user can
use to find it.

.. class:: Source([, keywords=[]])

    Creates a :class:`Source` with the given *keywords*; intended to be called in
    a subclass.

    Read-only attributes:

    .. attribute:: source_list

        The :class:`SourceList` that owns this source, if any.

    .. attribute:: name

        The name of the source. This only has a value if the source belongs to a
        :class:`SourceList`.

    .. attribute:: keywords

        The keywords associated with the source as a set.

    .. attribute:: stream_formats

        A list of stream formats for the source. The base class returns
        an empty list.

        You can call :meth:`get_stream` using the :attr:`fluggo.media.formats.StreamFormat.index`
        from one of these stream formats to get the decoded stream itself.

    Signals:

    .. method:: updated()

        Raised when the :attr:`stream_formats` or the streams
        themselves change. Throw away all information you've gathered about the
        streams and start over.

    .. method:: keywords_updated()

        Raised when the keywords have changed.

    Other methods:

    .. method:: fixup()

        Called by :class:`SourceList` after the the :class:`Source` is loaded from
        disk and its :attr:`source_list` is set. A derived class should override
        it to perform any checks it needs, such as looking up another source or
        using the muxers (defined in :class:`SourceList`) to check on the status
        of a file.

        Subclasses should expect that this can be called more than once.

    .. method:: visit(visitfunc)

        Call *visitfunc* once for each generated source or source reference this
        source depends on, passing the reference as a parameter. *visitfunc* will
        return a replacement source, which might be the same as the one passed in
        (or which could be ``None`` --- if, for example, the source being referenced
        was deleted).

        For example, a timeline might call *visitfunc* once for each clip in the
        timeline. *visitfunc* could be a no-op, returning the same reference it
        gets passed, or it might return a new reference if the name of the source
        has changed. Be ready to deal with any change returned.

    .. method:: get_stream(stream_index)

        Get a stream for the given *stream_index*. A video stream will be of type
        :class:`VideoStream` and audio of type :class:`AudioStream`; other types
        will probably have other base classes.

        The base class raises a :class:`NotImplementedError`.

    .. method:: get_default_stream_formats()

        Return a list of :class:`fluggo.media.formats.StreamFormat` objects
        describing the default streams for this source. The base class returns
        the first video and first audio stream, in that order.

:class:`FileSource` --- Source for disk media files
===================================================

.. class:: FileSource(container[, keywords=[]])

    Creates a :class:`Source`-based object for the given *container*
    (of type :class:`fluggo.media.formats.ContainerFormat`, best retrieved
    from the :class:`SourceList`).

:class:`SourceList` --- Dictionary of all sources in the project by name
========================================================================

.. class:: SourceList(muxers[, sources=None])

    Creates a source list with the given list of *muxers* (in order of preference,
    muxers earliest in the list are tried first) and optional dictionary of
    *sources*. Call :meth:`fixup` after loading the :class:`SourceList`.

    The source list is the top of the document structure for Canvas. Every clip,
    file, take, timeline, and space is a source, and goes into this list. The list
    assigns a unique name to each one.

    :class:`SourceList` has a dictionary-like interface, where the keys are names
    and the values are instances of :class:`Source`.

    .. attribute:: muxers

        A list of muxers to use for detecting and opening files, in order of preference.
        Read-only.

        Note for future: I don't plan to stop at just "muxer." I want codecs to
        also be part of the plugin architecture, rather than muxers deciding which
        codecs to use. This is a simplification for now.

    .. method:: detect_container(path)

        Opens the file at *path* and tries to determine, and return, its
        :class:`fluggo.media.formats.ContainerFormat`.

    .. method:: fixup()

        Sets up sources after being loaded from disk.

Streams --- :class:`VideoStream` and :class:`AudioStream`
=========================================================

.. class:: VideoStream(base_stream[, format=None])

    Creates a transparent source filter for the video source filter *base_stream*, which
    should be a :class:`fluggo.media.process.VideoSource` or equivalent. The
    *format* should be the format of the stream; if ``None``, it's taken from
    the *format* attribute on *base_stream*.

    In Canvas, all media library video sources are encapsulated as :class:`VideoStream`
    objects, which carry format and length information with them and have a signal
    for when their contents change. Otherwise, they act as ordinary video sources
    in the :mod:`fluggo.media.process` module and can be used in all the same
    ways.

    *Note to self:* I used to think this would be useful, I'm not sure if it is
    anymore. The length is the most important thing here. The format-- well, the
    format should be determined by the space or timeline this stream appears in.
    The format can really only be three things: the format of the original source,
    the format of the intermediate, or the format of the output.

    .. attribute:: format

        The :class:`fluggo.media.formats.StreamFormat` of the video.

    .. attribute:: length

        The length of the video, in frames. Right now this is adjusted for any
        automatic transformations done on the video; while that use case is
        important, I don't know if this is the right place for it.

    .. attribute:: frames_updated

        *Signal.* Raised when the contents (but not the format) of the source
        have changed. The signal gives two parameters *start* and *end*, which
        are the first and last frames affected by the change.

.. class:: AudioStream(base_stream[, format=None])

    Like :class:`VideoStream`, but as a media library audio source.

