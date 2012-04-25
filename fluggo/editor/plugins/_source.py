# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2012 Brian J. Crowell <brian@fluggo.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

from ._base import *
from fluggo.media.basetypes import *
from fluggo.media import process
from fluggo import signal, logging
import collections, threading, traceback

_log = logging.getLogger(__name__)

PULLDOWN_NONE = u'None'
PULLDOWN_23 = u'2:3'
PULLDOWN_2332 = u'2:3:3:2'

class SourceOfflineError(Exception):
    '''Error raised when an operation on a source requires the source to be online.'''
    def __init__(self):
        Exception.__init__(self, u'Source is offline.')

class KnownIlluminants:
    '''
    Known illuminants and their colors in xy-space. Where specified, these describe
    the coordinates for the two-degree CIE standard observer.

    Source: http://en.wikipedia.org/w/index.php?title=Standard_illuminant&oldid=364143326#White_points_of_standard_illuminants
    '''
    D50 = v2f(0.34567, 0.35850)
    D65 = v2f(0.31271, 0.32902)

class SourcePlugin(Plugin):
    '''Base class for a plugin that handles certain source types.

    It's this plugin's responsibility to notify the user of any source errors
    to the user through a NotificationManager.'''

    def create_source(self, name, definition):
        '''Return a source from the given definition.

        The *definition* will be a definition returned by Source.definition().'''
        raise NotImplementedError

    def create_source_from_file(self, name, path):
        '''Return a new source if one can be created from the given file. If the plugin
        can't read the file (or doesn't support files), it can raise an error or return None.'''
        return None

    # TODO:
    #   Ways to represent/create ad-hocable sources
    #       Thumbnail

#    def get_source_templates(self):
#        '''Return a list of sources that can be used as templates.
#
#        This is usually used for ad-hoc sources that can create their own
#        content, such as a solid color source, and would represent base versions
#        that the user could drag into a composition and alter to their needs.'''
#        # TODO: How to know the list is updated? How to separate into categories?
#        #   * Perhaps this isn't a function of the plugin, but part of some kind
#        #     of installation procedure. The templates could be .yaml files in
#        #     some program directory.
#        return []

class _CachedStream(object):
    __slots__ = ('stream', 'placeholder')

    def __init__(self):
        self.stream = stream
        self.placeholder = process.VideoPassThroughFilter(None)

    def set_stream(self, stream):
        self.stream = stream
        self.placeholder.set_source(stream)

class Source(AlertPublisher):
    '''An object which produces one or more streams.

    A source is welcome to produce its streams from scratch (in which case,
    it's likely to have just one stream) or render the contents of a file
    or several files. In the latter case, it's the job of the source to
    faithfully render the source material according to its original format.

    For example, DV is 4:1:1 subsampled Y'CbCr in Rec. 601 (or commonly Rec. 709)
    encoding at 720x480 with a 10:11 pixel aspect ratio, interlaced lower-field
    first at 30000/1001 frames per second, with two 16-bit linear PCM channels
    at 48 kHz (for the usual USA variant).

    Canvas is only prepared to deal with some of those details, namely:
      * Active area (Rec. 601 is 704x480)
      * Pixel aspect ratio (10:11)
      * White point and chromaticities (Rec. 601 or Rec. 709)
      * Interlacing and frame rate
      * Number of audio channels
      * Audio sample rate

    It's the source's job to deal with the other details as the original format demands.
    In this case, Canvas:

      * Converts each frame to floating-point, with the studio luma 16-235 range remapped to 0.0-1.0
        and the chroma remapped to -0.5 to 0.5 (allowing for headroom on each)
      * Bilinearly resamples the chroma to 4:4:4 (bearing in mind that DV chroma subsamples are co-sited on the left)
      * Inverts the Rec. 601 or Rec. 709 transfer function to yield linear luminance values
        in the range 0.0-1.0 (leaving headroom values above 1.0)
      * Converts the RGBA to XYZA
      * Renders each frame at (-8, -1) in the coordinate system, with an active area of (0, -1) to (703, 478),
        to account for the eight blanking pixels on the left and right side of the frame (which are still present, just
        not considered "active") and shifted up one pixel to account for the fact that DV
        starts one line earlier than MPEG-2, and is therefore lower field first; Canvas is
        strictly upper (or "even," going by the vertical coordinates) field first.
      * Present the stream format with original aspect ratio, white point and frame rate
      * Audio is presented as a single stereo stream at 48 kHz, with the original 16-bit
        data mapped onto -1.0 to 1.0

    Other choices could have been made for decoding DV, but these choices preserve all
    of the detail of the original DV, and perhaps more than other decoders, which
    often do nearest-neighbor chroma reconstruction and site the subsamples too far to the right,
    and leave headroom and gamma confusing issues. This also makes DV conform with
    Canvas's expectations for video and audio streams.

    By finding the right source to decode a file, Canvas aims to faithfully represent
    everything it's given.

    While it is the responsibility of sources to decode the data, a file source is
    perfectly welcome to just extract the streams and hand the data off to a codec
    from the plugin system. The codec is then responsible for faithfully decoding the data.

    The offline attribute stores whether the source is offline. This defaults to True;
    you can wait until bring_online() is called to load and populate the source.
    '''

    def __init__(self, name):
        AlertPublisher.__init__(self)
        self._name = name
        self.offline_changed = signal.Signal()
        self._offline = True

    @property
    def offline(self):
        return self._offline

    @offline.setter
    def offline(self, value):
        value = bool(value)

        if value == self._offline:
            return

        old_value = self._offline
        self._offline = value
        self.offline_changed(self)

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, value):
        self._name = unicode(value)

    def bring_online(self):
        '''Populate the source's streams and metadata, and set offline to False.

        If the source fails to come online, it should leave offline True and
        set an alert for the user to retry the action.

        If a source needs to rediscover streams-- say, if a source file changes--
        it needs to set offline to True first to notify downstream users.

        Not all sources support being offline. For those sources, the base
        implementations should do.'''

        self.offline = False

    def take_offline(self):
        self.offline = True

    @property
    def plugin(self):
        '''Return a reference to the plugin that created this source.'''
        return None

    def get_definition(self):
        '''Return an object that the source plugin can use to recreate this
        source in the future. The returned object is considered opaque (the
        caller promises not to alter it), but it should be representable in
        YAML.'''
        raise NotImplementedError

    @property
    def file_path(self):
        '''Return the path to the file this source represents.

        If the source is not file-based, return None. Otherwise, this needs to be
        a valid path that the source plugin can use to find the source again.
        This is important if the user has moved the source files to a new
        directory and needs to remap them.'''
        return None

    def get_streams(self):
        '''Return a list of streams in this source.

        The streams returned should be in some sensible order based on the source.
        The default implementation of get_default_streams() returns the first video
        and the first audio streams it finds.'''
        # TODO: What should this do if the source is offline? On the one hand,
        # it would be nice to know what to expect from an offline source. On the
        # other, you can't actually do anything with those streams, and they
        # might not even exist when the source does come online.
        raise NotImplementedError

    def get_default_streams(self):
        '''Return the streams the user is most likely to want to work with.

        Override this if the source provides a stream that the user probably doesn't
        want by default, but could choose to add on later, such as a commentary audio track.
        The default streams are used to determine what to lay out when a source gets
        dragged from the source list into a composition.

        The base implementation returns the first video and the first audio stream, in that order.'''

        # For now, first video stream and first audio stream
        streams = self.get_streams()

        video_streams = [stream for stream in streams if stream.stream_type == u'video']
        audio_streams = [stream for stream in streams if stream.stream_type == u'audio']

        return video_streams[0:1] + audio_streams[0:1]

    def get_stream(self, name):
        '''Return the stream with the given name. The returned stream will be updated
        when the source changes.

        This is a VideoStream or AudioStream (or some similar stream). Codec decoding
        takes place in the source.

        If the stream is unknown, or the source is offline, raises KeyError.
        Otherwise, the source needs to create and return a stream, even if the
        stream is blank, and set an alert for the user to resolve any issues.
        The stream should be updated if the user resolves the issue.
        '''
        raise NotImplementedError

    def get_source_metadata(self):
        '''Return the set of user metadata defined in the source itself, if any.

        This is used to establish a baseline for metadata. It might be called only
        once and then never called again if the caller takes over handling metadata
        for this source.'''
        return None

    def get_stream_metadata(self, name):
        '''Return the base set of user metadata for the stream, if any.

        This is used to establish a baseline for metadata. It might be called only
        once and then never called again if the caller takes over handling metadata
        for this source.'''
        return None

    def get_thumbnail(self, size):
        '''Return an RgbaFrameF16 representative of this source that fits in the given
        *size* (a v2i).'''
        raise NotImplementedError

    # TODO: Property sheets, tool boxes, menu items, 3D
    # What about sources that can produce to any specification?
    #   * Probably have the specification set by the user before transform
    # What about overrides?
    #   a) Could be on the transform input side <-- NO, too many
    #   b) Could be created and maintained by the calling app
    #   c) Could be maintained by the source itself <-- I think so
    # What about *codec* overrides?
    #   a) Could be maintained by the source itself, where the source plugin
    #      can ask the plugin architecture for codec plugins <-- DING DING
    # Reinterpretation of formats
    # TODO: Proxies, which are probably handled by host

_VideoFormat = collections.namedtuple('_VideoFormat', 'interlaced pulldown_type pulldown_phase full_frame active_area pixel_aspect_ratio white_point frame_rate')

class VideoFormat(_VideoFormat):
    '''Provides Canvas-relevant video format information.'''

    # TODO: Need to provide track information; see Matroska specs for some ideas

    # interlaced = bool
    # active_area = box2i
    # pixel_aspect_ratio = fractions.Fraction
    # white_point = (float, float, float) or wellknownwhitepoint (a string)
    # frame_rate = fractions.Fraction

    __slots__ = ()
    format_type = u'video'

    def __new__(cls, interlaced=False, pulldown_type=PULLDOWN_NONE, pulldown_phase=0,
        full_frame=box2i(0, 0, 99, 99), active_area=None,
        pixel_aspect_ratio=fractions.Fraction(1, 1), white_point=u'D65',
        frame_rate=fractions.Fraction(1, 1)):

        return _VideoFormat.__new__(cls, interlaced, pulldown_type, pulldown_phase,
            full_frame, active_area or full_frame, pixel_aspect_ratio, white_point, frame_rate)

    @property
    def white_point_value(self):
        if self.white_point.__class__ is v2f:
            return self.white_point

        return getattr(KnownIlluminants, self.white_point)

    @property
    def thumbnail_box(self):
        '''Return the box from which thumbnails should be taken.
        Defaults to active_area.'''
        return self.active_area

def _VideoFormat_represent(dumper, data):
    mapp = {}

    if data.interlaced != False:
        mapp[u'interlaced'] = data.interlaced

    if data.pulldown_type != PULLDOWN_NONE:
        mapp[u'pulldown_type'] = data.pulldown_type

        if data.pulldown_phase != 0:
            mapp[u'pulldown_phase'] = data.pulldown_phase

    mapp[u'full_frame'] = data.full_frame

    if data.active_area != data.full_frame:
        mapp[u'active_area'] = data.active_area

    if data.pixel_aspect_ratio != fractions.Fraction(1, 1):
        mapp[u'pixel_aspect_ratio'] = data.pixel_aspect_ratio

    mapp[u'white_point'] = data.white_point
    mapp[u'frame_rate'] = data.frame_rate

    return dumper.represent_mapping(u'!VideoFormat', mapp)

def _VideoFormat_construct(loader, node):
    return VideoFormat(**loader.construct_mapping(node))

yaml.add_representer(VideoFormat, _VideoFormat_represent)
yaml.add_constructor(u'!VideoFormat', _VideoFormat_construct)


_AudioFormat = collections.namedtuple('_AudioFormat', 'sample_rate channel_assignment')

class AudioFormat(_AudioFormat):
    # sample_rate = fractions.Fraction
    # channel_assignment = (one of ('FrontRight', 'FrontLeft', 'Center', 'Solo',
    #       'CenterLeft', 'CenterRight', 'BackLeft', 'BackRight', 'LFE'))

    # TODO: There's a standard channel assignment according to SMPTE;
    # implement that here (Matroska has the list)

    # TODO: I used to have "loudness" listed here; I found that ReplayGain was
    # probably a suitable loudness measuring standard, but the ability to apply
    # it at different levels (track, album) suggests to me that this class
    # is probably the wrong place to put it. I may backtrack on that later.

    __slots__ = ()
    format_type = u'audio'

    def __new__(cls, sample_rate=fractions.Fraction(1, 1), channel_assignment=None):
        return _AudioFormat.__new__(cls, sample_rate, channel_assignment or [])

def _AudioFormat_represent(dumper, data):
    mapp = {u'sample_rate': data.sample_rate, u'channel_assignment': data.channel_assignment}
    return dumper.represent_mapping(u'!AudioFormat', mapp)

def _AudioFormat_construct(loader, node):
    return AudioFormat(**loader.construct_mapping(node))

yaml.add_representer(AudioFormat, _AudioFormat_represent)
yaml.add_constructor(u'!AudioFormat', _AudioFormat_construct)


class VideoStream(process.VideoPassThroughFilter, AlertPublisher):
    '''Abstract base class for a video stream.

    This object can be used directly as a source filter;
    the resulting stream will be updated as the stream object
    is updated. For a stream that won't change on you, call
    obj.get_static_stream().

    VideoStream has three signals:

        format_changed(stream) is raised after the format of the stream has
        changed.

        frames_updated(stream, start_frame, end_frame) is raised when the
        contents of some or all of the frames have changed. start_frame
        is the first frame that has changed (or None if all frames up to
        end_frame have changed); end_frame is the last frame (or also None).
        Both start_frame and end_frame can be None to indicate the entire
        stream has changed.

        range_changed(stream) is raised when the defined_range of the stream
        has changed.

    '''
    # What I don't like about this class is that it presumes creating
    # the source filter right off the bat. What if I want to just look
    # at the stream properties? How might I decide to manage the number
    # of files open at the same time?
    #
    # Perhaps the answer is that VideoStream and AudioStream represent
    # a real stream anywhere in the Canvas system, and that Sources need
    # to have a way to *produce* these that is independent of describing them?
    #
    # I'll probably come back and define other ways of getting at the
    # data. For now, it'll live in this class.

    stream_type = u'video'

    def __init__(self, base_filter=None, format=None, range=(None, None)):
        self._format = format or VideoFormat()
        self._defined_range = range
        self.format_changed = signal.Signal()
        self.frames_updated = signal.Signal()
        self.range_changed = signal.Signal()

        AlertPublisher.__init__(self)
        process.VideoPassThroughFilter.__init__(self, base_filter)

    @property
    def format(self):
        return self._format

    def set_format(self, format):
        if self._format == format:
            return

        self._format = format
        self.format_changed(self)

    @property
    def defined_range(self):
        return self._defined_range

    def set_defined_range(self, defined_range):
        if self._defined_range == defined_range:
            return

        self._defined_range = defined_range
        self.range_changed(self)

    def set_base_filter(self, base_filter, new_range=None):
        '''Call this in your implementation to replace the base filter.
        The frames_updated method will be called for the defined_range.
        If new_range is specified, the range will be replaced before changing
        the base filter, and frames_updated will be called for the union of the two.'''
        old_range = self._defined_range

        if new_range:
            self.set_defined_range(new_range)
        else:
            new_range = old_range

        self.set_source(base_filter)

        start_frame = None if (old_range[0] is None or new_range[0] is None) else min(old_range[0], new_range[0])
        end_frame = None if (old_range[1] is None or new_range[1] is None) else min(old_range[1], new_range[1])

        self.frames_updated(self, start_frame, end_frame)

    def get_static_stream(self):
        '''Return a raw source filter that represents the stream as it exists now.
        Further changes to the stream will not be reflected in the result.'''
        raise NotImplementedError

class AudioStream(process.AudioPassThroughFilter, AlertPublisher):
    '''Abstract base class for an audio stream.

    This object can be used directly as a source filter;
    the resulting stream will be updated as the stream object
    is updated. For a stream that won't change on you, call
    obj.get_static_stream().

    AudioStream has three signals:

        format_changed(stream) is raised after the format of the stream has
        changed.

        samples_updated(stream, start_sample, end_sample) is raised when the
        contents of some or all of the stream have changed. start_sample
        is the first sample that has changed (or None if all samples up to
        end_sample have changed); end_sample is the last sample (or also None).
        Both start_sample and end_sample can be None to indicate the entire
        stream has changed.

        range_changed(stream) is raised when the defined_range of the stream
        has changed.

    '''
    # TODO: Expect this class to provide the primary UI for altering the
    # properties of streams at their source.

    stream_type = u'audio'

    def __init__(self, base_filter=None, format=None, range=(None, None)):
        self._format = format or VideoFormat()
        self._defined_range = range
        self.format_changed = signal.Signal()
        self.frames_updated = signal.Signal()
        self.range_changed = signal.Signal()

        AlertPublisher.__init__(self)
        process.AudioPassThroughFilter.__init__(self, base_filter)

    @property
    def format(self):
        return self._format

    def set_format(self, format):
        if self._format == format:
            return

        self._format = format
        self.format_changed(self)

    @property
    def defined_range(self):
        return self._defined_range

    def set_defined_range(self, defined_range):
        if self._defined_range == defined_range:
            return

        self._defined_range = defined_range
        self.range_changed(self)

    def set_base_filter(self, base_filter, new_range=None):
        '''Call this in your implementation to replace the base filter.
        The frames_updated method will be called for the defined_range.
        If new_range is specified, the range will be replaced before changing
        the base filter, and frames_updated will be called for the union of the two.'''
        old_range = self._defined_range

        if new_range:
            self.set_defined_range(new_range)
        else:
            new_range = old_range

        self.set_source(base_filter)

        start_frame = None if (old_range[0] is None or new_range[0] is None) else min(old_range[0], new_range[0])
        end_frame = None if (old_range[1] is None or new_range[1] is None) else min(old_range[1], new_range[1])

        self.frames_updated(self, start_frame, end_frame)

    def get_static_stream(self):
        '''Return a raw source filter that represents the stream as it exists now.
        Further changes to the stream will not be reflected in the result.'''
        raise NotImplementedError

