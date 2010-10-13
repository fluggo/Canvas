.. highlight: python
.. _framework:

*********
Framework
*********

The core of the Fluggo Media Library is the processing framework, which lets
users process audio and video by building filter chains. It can be
found in the Python :py:mod:`fluggo.media.process` module.

The processing framework is designed for production-level video and audio work,
as opposed to general media frameworks like GStreamer. It does this by making
a few simplifying assumptions:

* **Random access.** Video, audio, and other key sources are expected to be able
  to produce data from any point in a stream at any time.

* **Fixed-rate.** Video formats in general aren't required to have a specific
  frame rate. While it doesn't always associate timing with video frames, where
  it does, it assumes fixed-rate content only.

* **Single-format.** Video is always 4:4:4 floating-point RGBA, and audio is always
  32-bit floating point. This greatly simplifies coding and removes the need for
  a lot of the negotiation between filters required in other frameworks.

* **Pull-only.** Sources don't prepare or produce data ahead of time, and the
  responsibility of allocating memory for frame data is specified as part of
  each function call. This simplifies fetching data from a source to a single
  function call.

* **Metadata-free.** The framework doesn't take any responsibility for
  correcting frame sizes, frame rates, aspect ratios, color systems, or
  interlacing between different video sources. It's up to the calling
  application to manage this data. This assumption of don't-do-anything-unless-told-to
  saves a lot of time chasing failed automatic processes.

* **Separate streams.** Even if video and audio come from the same file, they
  are never handled together. It's assumed that the user will want to move
  things out of sync, apply different filters, or only use some of the streams
  from any given file.

Sources
=======

Data in the framework moves through *sources*. A *source* provides a single stream,
usually random-access, of a single type of data. A source may produce its own data,
retrieve it from a file, or pull and modify data from any number of other sources.
Sources can also be used multiple times in a filter graph.

There are six kinds of sources: video, audio, presentation clock, frame function,
codec packet, and coded image.

Audio sources
-------------

One of the simplest types of sources is the audio source, which produce blocks of
floating-point audio samples as audio frames. Audio sources are random-access, meaning
they can produce any part of their stream at any time. They can also contain any
number of audio channels. In Python, audio sources are represented by the
:class:`~.AudioSource` class.

Audio data isn't complicated. Audio is indexed by samples (not time), and each
sample is a floating-point number. Samples are nominally in the range [-1.0, 1.0],
with silence at 0.0, but can extend outside this range during processing. Samples
outside this range may be clipped during playback or while writing to a file.

We can play back a simple stereo audio source with :class:`~.AlsaPlayer`::

    from fluggo.media.alsa import AlsaPlayer

    player = AlsaPlayer(rate=48000, channels=2, source=my_source)
    player.play()

.. digraph:: foo2

    rankdir="LR";
    node [shape=box]; src1 [label="my_source (AudioSource)"]; alsa [label="AlsaPlayer"];
    src1->alsa;

Note that we have to tell :class:`~.AlsaPlayer` the correct sample rate and number
of channels; it doesn't know or care about the sample rate or channel count of
``my_source``. :class:`~.AlsaPlayer` only asks ``my_source`` for data and plays it back
at the given rate.

Note also that giving the wrong *rate* isn't a crime: it will just speed up or slow
down playback. The same goes for *channels*; setting ``channels=1`` will just play
the first channel of ``my_source``. If you ask for too many channels, ``my_source``
(and any other :class:`~.AudioSource`) will just fill the extra channels with
silence. In this way, the framework is forgiving, but will only do exactly what
you ask.

Lastly, :class:`~.AlsaPlayer` will happily play the sound at half speed (``player.play(Fraction(1, 2))``),
double speed (``player.play(2)``), and backwards (``player.play(-1)``). The random-access
nature of audio sources makes this possible.

Almost all of the work of the framework is done with sources, which are objects
that can produce some kind of data on request, and usually at random.

There are six kinds of sources in the framework. These four are common:

* **Video sources** produce single frames of video. Sources can read from a file
  (or multiple files), generate data, or modify the data of another video
  source. Sources produce only 4:4:4 RGBA floating-point video.

* **Audio sources** produce audio frames, which are blocks of audio samples.

* **Presentation clocks** produce a time in nanoseconds, which is used to
  synchronize the presentation of video, audio, and other elements. Presentation
  clocks are, in general, not associated with the system clock and may run
  backwards or at different speeds.

* **Frame functions** produce filter parameters which can vary over time and can
  be subsampled.

These two source types are only found when handling specific formats:

* **Codec packet sources** produce a stream of data packets, usually with
  timestamp and keyframe information. The stream is forward-only, but is optionally
  seekable. Codec packet sources can be paired with a matching decoder to
  retrieve the data, or a muxer to write the data to a file.

* **Coded image sources** are similar to video sources, but produce images in a
  device- or codec-dependent video format. For example, the :py:class:`fluggo.media.process.DVSubsamplingFilter`
  produces 720x480 planar 4:1:1 YC\ :sub:`b`\ C\ :sub:`r` subsampled video with a Rec.
  709 matrix and transfer function. The source can be passed to a matching encoder,
  which will produce codec packets, or a matching reconstruction filter, which
  will produce ordinary video frames.

Video
=====

Video sources
-------------

Video is available from video sources, which may read video data from a file,
generate the data on request, or process data from another video source.

.. digraph:: foo

    rankdir="LR";
    node [shape=box]; src1 [label="Video source"]; clock [label="Presentation clock"]; widget1 [label="Qt VideoWidget"];
    { rank=same; clock; widget1; }

    src1 -> widget1; clock->widget1 [constraint=false];

.. digraph:: foo2

    rankdir="LR";
    node [shape=box]; src1 [label="Video source 1"]; src2 [label="Video source 2"];
    filter [label="Filter"];
    clock [label="Presentation clock"]; widget1 [label="Qt VideoWidget 1"]; widget2 [label="Qt VideoWidget 2"];
    { rank=sink; clock; widget1; widget2; }
    { rank=source; src1; src2; }

    src1 -> widget1; src2->filter->widget2; clock->widget1 [constraint=false]; widget2->clock [constraint=false, dir=back];

Frames
------

Data windows
^^^^^^^^^^^^

Interlacing
"""""""""""

Standard data windows
"""""""""""""""""""""

Coded images
------------

Audio
=====

Frames
------

Codecs and muxers
=================

