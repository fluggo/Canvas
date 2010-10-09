.. highlight: python
.. _framework:

*********
Framework
*********

The core of the Fluggo Media Library is the processing framework, which lets
users process audio and video by building filter chains. It can be
found in the Python :py:mod:`fluggo.media.process` module.

About
=====

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

General
=======

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

