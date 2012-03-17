*******************************
Feature proposal: Canvas editor
*******************************

Right now, this is a general mind dump for what I'm trying to accomplish at the
editor level with Canvas. Once I've got it written down, hopefully someone can
arrange it into coherent ideas.

Quick Glossary
==============

media repository
   A media repository stores and provides access to source footage, intermediates,
   and final renderings and all associated metadata. It's expected this will be
   highly subject to plugins, and media could be spread out over multiple
   machines.

space
   A space is what other nonlinear editors might call a "timeline," or
   what Final Cut calls a "sequence" and After Effects calls a "composition."
   It's a collection of clips and other events arranged over time (represented
   on the X axis) and space (on the Y and Z axes). It represents the composition
   of video, audio, and other elements into a cohesive whole.

stream
   A stream is a single set of events. A stream only represents one kind of data
   (such as audio or video), and they often come bundled together in media files.
   A stream could also be "virtual," as when a series of individual image files
   makes up a video stream. Audio and video streams are always fixed-rate in
   Canvas and may carry a wealth of metadata.

   Streams are not guaranteed to be defined for all moments in time. In fact,
   streams may not even be finite. It's expected that metadata will help the editor
   determine which points in time are valid, but the only real way to know is to
   request data from a point in time and see if there is any.

   (A stream in Canvas is the same thing as a source in the media toolkit, except
   that Canvas tracks metadata such as color space, interlacing, frame rate, etc.)

source
   A source is a collection of synchronized streams. Not all of the streams might
   be defined for the same periods of time. Media files, timelines, takes, and spaces
   can all act as sources, and can all be used as clips.

   (This terminology is different from the media library, which uses "

sequence
   A sequence is like a track in other editors, except that it's free-floating
   and can be placed anywhere in the space just like clips. Sequences hold a series
   of clips and support transitions between them. They can be expanded to show
   an A/B roll.

narrative
   A narrative is a special sequence that runs the length of the space. Each space
   has one narrative, and it defines how long the space is and how it's timed.
   Narratives give a handy default for ripples: Moving clips in the narrative

anchor
   Clips aren't grouped in Canvas so much as they're anchored. Clips optionally have a link
   to a point in another clip that determines its timing. Moving the anchor clip
   moves the anchored clip; sliding the anchor footage moves the anchored clip
   as well. Moving the anchored clip redefines its timing relationship to the
   anchor clip.

   There can be big hierarchies of anchors, but the goal is to have almost all of
   them be implicit. Synchronized clips (see take) are automatically moved as a
   group, and clips outside the narrative are implicitly anchored to the most
   recent clip in the narrative. You can define your own anchor to override the
   defaults.

take
   A take defines the synchronization between different streams. A take
   may combine streams from a variety of different media files, all of which
   represent the same point in time. For example, a complicated take might
   synchronize footage from multiple camera angles plus different audio recordings,
   and an editor could use the information to find related source material.

   Media files with more than one stream are implicitly also takes, where the
   synchronization is defined by the file.

format
   A format describes the properties of a stream necessary to playing or converting
   it to other formats correctly. For video formats, this includes:

   * Color space, primaries, white point, and transfer function
   * Frame rate and interlacing or pulldown
   * Pixel aspect ratio
   * Optional active area rectangle (the video's resolution or "frame")

   For audio formats, it includes:

   * Sample rate
   * Channel configuration
   * Optional normal sound level (dialnorm or loudness measure to help mix or play
     back at predictable levels)

Streams
=======

To Canvas, a stream is the same thing as a source in the media library, but with
more metadata.

To recap, a source in the media library:

* has an undefined sample rate, which we assume is fixed 
* has no fixed boundaries (could be "infinitely" long, or might have an "infinite"
  area)
* video has an undefined color space (but is assumed to be tristimulus)
* audio has an undefined number of channels

Canvas assigns all of these properties to a stream, usually based on the source
file or a guess, which the user can override.

Canvas knows about a video stream's:

* fixed sample rate
* interlacing/pulldown mode
* gamma encoding
* color primaries and white point
* "thumbnail" range, the rectangle that best describes the active area of the video;
  this is only a hint
* ranges where video is defined (mostly a hint; if the stream is marked finite,
  Canvas won't look for video outside of defined ranges, but that doesn't mean
  the video is defined for the whole range)

It knows an audio stream's:

* fixed sample rate
* channel count and channel assignment
* ranges where audio is defined (same as for video)

Sources
=======

A source in Canvas is a collection of one or more synchronized streams. Streams don't have to
have the same sample rates, but this is the usual case.


Takes
=====

If you shoot a wedding, you usually end up with several different recordings of the
same event. You might have the following streams available for the ceremony:

* Camera A video (running whole time)
* Camera A audio (not a high-quality mic, but captured ambience)
* Camera B video (running part of the time)
* Camera B audio (junk)
* Bride's microphone
* Groom's microphone
* Officiator's microphone
* Singer's microphone

...and perhaps more, depending on how detailed you got. These would all be split
across multiple files and start and stop at different times-- especially camera
B, which, as listed above, was only running part of the time, in short bursts.

In a typical NLE, you might lay all of these streams across the bottom tracks of
your timeline, bring them in sync, and then mute those tracks and splice footage
onto upper tracks that represent the result of your editing.

This sounds simple enough, and almost is for a wedding, until you have to remove
a slice of time or overlap other parts. You have to edit for time on the top track
while carefully maintaining sync on the bottom tracks. When dealing with scripted
footage and multiple takes, this technique is useless.

I want to solve this in Canvas with the concept of a "take," which should mean the
same thing as a take in movies: multiple synchronized streams that represent the
same point in time. Take 52B in a movie might be two video files and five audio
files (or even lots of small slices of different video and audio files), but we
should be able to represent it with just one object in Canvas that combines all
of the source footage.

(Many of these same ideas would also apply to any other source footage, except that
the synchronization and streams have already been defined for you.)

So think about the bottom tracks in our wedding example. You might label the tracks:

* Camera A video
* Camera B video
* Camera A audio
* Bride's microphone
* Groom's microphone
* etc.

On the track for camera A, you'd put a single clip, since it was running the
whole time. For camera B, you'd put multiple clips and synchronize them with
camera A (or, if you were smart enough to jam-sync the cameras, you could get
Canvas to do this for you). You'd do the same for all of the other tracks. Call
these tracks our "take," and pretend that it's a media file just like any other,
except that it has two video tracks and several audio tracks, and some of the
footage is only available at certain points in time.

Now that we've defined this "take," drag it from your source window into your
space. The editor would place one video clip and one audio clip from your take
(or you might define several tracks as "default" in your take if you wanted them
all to appear). As you edit, it keeps all of the pieces of the take synced.

As you're editing, you want to see what the other camera had available. You bring
up a multicam viewer that shows, as you scrub the timeline, all of the angles from
the take in the narrative. You decide you want to use the other camera, so you
select part of the clip, right click, and choose "Camera B" from the list. The
footage you selected is replaced with footage from the exact same time on camera B.
(And also handling cases where not all of the time you selected was available
from B.)

You could also ask for the additional footage to be added to the space instead of
replacing what's there. This would be handy for editing in rough, then adding more
material as you add it to the source take.

So, for takes:

* Takes act as source media
* Streams in a take can combine clips from multiple sources, including other
  spaces, but can't use transitions

And for all sources in general:

* All sources have a concept of their "default streams," which are the streams
  that would appear if you dragged the file into the space. You can set what these
  are for takes (and spaces, and maybe also for media files).
* You can ask the editor to expand out other streams from the same source as a
  clip or replace an existing stream with a different one.


Conforming
==========

Conforming is the process of converting one video or audio format into another.
It applies in two areas: conforming a source to a composition's format when it is
added, and conforming a composition's output format to a desired render format.
(It is expected that filters applied within a composition don't alter format at
all, so Canvas won't support format changes outside this process.)

The goal of conforming is to make this process as automatic as possible and to
allow overrides when a manual conversion process is needed. The decisions made
during conformance are saved at the time and are reused (and can be altered) later
when the filter graph is constructed.

(Future note: The user should also be able to apply filters at the source to
transform streams in any way they like before the conformance even runs.)

It should be noted that the base media library doesn't explicitly care about 
formats at all. It's canvas's job to know what these properties are for every
stream and add in the appropriate filters to perform the conversion.

Frame rate conformance
----------------------

There are two properties for time format in a video stream: frame rate and
interlacing. The frame rate is a ratio and can take on just about any fractional
value. The interlacing property describes whether the frame is divided into fields
and how they're used. It has these possible values:

* Progressive (the video has no fields; each frame describes a single point in time)
* Interlaced (the video has two fields, which on an interlaced monitor would play
  one after the other; these fields are present in the frame in a weave pattern
  with the first field on even lines and the second field on odd lines)
* Interlaced with 2:3 pulldown (interlaced, but the video contains progressive content that
  natively is 4/5ths the frame rate or 2/5ths the field rate and has been converted
  to interlaced content through 2:3 pulldown; the cadence of the pulldown will also
  have to be specified)
* Interlaced with 2:3:3:2 pulldown (like above, but with 2:3:3:2 pulldown)

(Compositions, by the way, can only take on the progressive or interlaced modes.)

Typical frame rate/interlacing combinations include:

* Film - 24/1, progressive
* NTSC - 30000/1001, interlaced
* Telecine film - 30000/1001, interlaced with 2:3 pulldown
* Progressive telecine - 24000/1001, progressive

It should be easy for the user to specify one of these modes when the source's
format has been guessed wrong (or was specified wrong in the source).

On interlaced content, the user may also want to describe whether the top field
or bottom field come first (in many formats, this can be guessed automatically).
While Canvas needs to be concerned about this, it doesn't impact conformance. It
may just require Canvas to shift the video up or down one pixel before considering
interlacing.

The following conversions exist for changing one frame rate to another:

* None: The video is inserted as-is with no conversion. If the source frame rate
  is higher than the target, it plays slower, and vice-versa.
* Bob deinterlace - Separates the fields of a frame into two progressive frames.
  This converts interlaced video to progressive video running at twice the frame
  rate of the source.
* Deinterlace - Like "bob deinterlace," but discards half the frames. This produces
  progressive frames at the same frame rate as the source.
* Remove 2:3 pulldown - Reconstructs progressive frames from 2:3 pulldown material.
  This converts interlaced 2:3 video to progressive video running at 4/5ths the
  frame rate of the source.
* Add 2:3 pulldown - Spreads progressive source frames across two fields, then
  three in a pattern. Converts progressive video to interlaced 2:3 video running
  at 5/4ths the frame rate of the source.
* Remove/add 2:3:3:2 pulldown - Same as adding or removing 2:3, but with 2:3:3:2.
* Bob interlace - Combines two frames as the fields of a single interlaced frame.
  This converts progressive video into interlaced video running at half the frame
  rate of the original.
* Weave interlace - Softens vertical details in progressive frames to prevent
  misinterpreting them as movement in interlaced video. The frame rate is preserved.

It's up to conformance to look at the ratios of frame rates and the different
interlacing modes to guess which conversion to apply. The order should be:

* All special cases should be considered first
* If a source is being added to a composition, any interlaced mode to progressive
  (where bob deinterlace is preferred), or weave interlace
* If rendering, ask
* "None" as a last default

Active area and pixel aspect ratio
----------------------------------

The important thing to remember here is that the active area is optional on sources,
but not on compositions. So if a source has no active area defined, only the pixel
aspect ratio needs to be conformed, and that's done as a scale around
the origin (unless we decide an origin is needed, in which case we transform around
that).

With an active area, we have several ways to conform:

* None: No scaling or translating is done. The user is free to specify one manually
  some other way.
* Align: No scaling, but the center of the source active area is translated to the
  center of the target.
* Letterbox: The active area of the source is scaled and translated to fit completely
  inside the active area of the target.
* Fill: The active area of the source is scaled and translated so that it fills
  the target active area and the centers of the active areas are aligned.
* Stretch: Pixel aspect ratios are ignored as the source is scaled to match the
  target active area.

Which is default should probably be specifiable at the install, project, and/or
composition levels. As far as I'm concerned, "Letterbox" should be the default.

Color
-----

Color conforming, as I understand it, should work as a natural consequence of
specifying colors in the chain in the XYZ linear color space. We just need the
transformation to XYZ at the source and a transformation to the target color
space at the end. I may be wrong about this.





