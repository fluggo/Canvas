*****************************
Feature proposal: Media hints
*****************************

Hints travel with audio and video frames in the filter graph. Hints can be, but
aren't required to be, used to reduce the work the filter system has to do to
render video and audio.

This document describes two such hints: opaque and quality.

"Opaque" hint
=============

"Opaque" is a boolean hint that travels downstream with video frames. It can be
set on a frame if and only if each of the pixels in the frame (current_window in
soft frames, full_window in OpenGL frames) has an alpha of 1.0. This is a very
important hint when compositing two frames: if the top frame has the opaque flag,
the compositor may skip even requesting the frame for the bottom frame, saving a
lot of work building the bottom frame.

Not all sources know enough about their content to know if a frame is opaque.
Sources are welcome to go the extra mile and test each pixel, but most filters
will want to set the opaque flag if:

* the upstream frame is opaque and the filter doesn't introduce transparency, or
* the filter only produces opaque frames.

The planned auto-resize filter, for example, can fulfill the second condition
when its "Fill black" flag is set: it will composite the possibly-transparent
frame onto a solid black color, rendering the frame opaque. In fact, since the
solid color source always renders opaque frames when its alpha is 1.0, the
compositor would probably figure this fact out on its own.

"Quality" hint
==============

"Quality" is an integer hint that travels upstream with audio and video frames.
It's a value that ranges from zero to ten, and it requests a particular
level of quality from filters. Filters are welcome to ignore the hint, in which
case they will effectively operate at quality 10, but are required to pass the
hint to their sources.

Each of the numbers in the quality setting represent a certain level of service:

* 10 is high quality; the user will probably render the final output with this
  setting. All filters work according to their settings.
* 9 is good quality; the user will probably view their project in the timeline
  with this setting. All filters are enabled, and will produce output close to
  that of setting 10, but trading off the most expensive operations so that
  the user might get close to playing the result back in real-time.
* 8 represents the best trade-off between speed and quality. A normal user should
  expect real-time playback at this quality unless tricky effects are involved,
  and some finesse effects like unsharp mask might disable themselves.
  A user in the initial editing phase might set their timeline to this quality.
* 7 through 2 represent decreasing levels of service.
* Zero is minimum quality, where the project will render, but almost all
  filters are disabled.

Here are some examples of what might happen at each quality level. These are
only suggestions; consider the guide above when deciding what your filter should
do at each level.

* 10 (or hint disabled): All filters enabled. Frame resamplers run using at
  least bicubic or Lanczos interpolation (or whatever setting the user picks).
  Every filter uses its full settings.
* 9: All filters enabled.
  Frame resamplers might drop to antialiased bilinear.
* 8: Frame resamplers run with antialiased bilinear (triangle filter). Unsharp
  mask disabled, expensive blurs use a cheaper algorithm.
  Chromakey effects 


