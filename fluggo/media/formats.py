# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2010 Brian J. Crowell <brian@fluggo.com>
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

import fractions, yaml
from fluggo.media.basetypes import *
from fluggo.media import process

PULLDOWN_NONE = 'None'
PULLDOWN_23 = '2:3'
PULLDOWN_2332 = '2:3:3:2'

class ContainerProperty:
    FORMAT = 'format'
    STREAM_ID = 'stream_id'
    STREAM_INDEX = 'stream_index'
    MUXER = 'muxer'

class KnownContainerFormat:
    AVI = 'avi'
    DV = 'dv'

class KnownVideoFormat:
    DV = 'dvvideo'

class AudioProperty:
    SAMPLE_RATE = 'sample_rate'
    CHANNELS = 'channels'
    CODEC = 'codec'

class VideoProperty:
    # Video frame rate (Fraction)
    FRAME_RATE = 'frame_rate'

    # Interlaced (bool)
    INTERLACED = 'interlaced'

    # Sample aspect ratio, x/y (Fraction)
    SAMPLE_ASPECT_RATIO = 'sample_aspect_ratio'

    # Pulldown type ('None', '2:3', '2:3:3:2', possibly more)
    PULLDOWN_TYPE = 'pulldown_type'
    PULLDOWN_PHASE = 'pulldown_phase'

    # Color primaries: Either a 3-tuple of v2fs with xy coordinates of RGB chromaticities,
    # or one of the names in the KnownColorPrimaries
    COLOR_PRIMARIES = 'color_primaries'

    # White point: Either a v2f with the xy coordinates of the white point,
    # or one of the names in the KnownIlluminants
    WHITE_POINT = 'white_point'

    # Max data window: A box2i describing the maximum size of frames.
    MAX_DATA_WINDOW = 'max_data_window'

    # Window to use when generating thumbnails; if not set, this will default to the MAX_DATA_WINDOW
    THUMBNAIL_WINDOW = 'thumbnail_window'

    # A string identifying the codec used to encode/decode this stream.
    # For decode:
    #   In the "detected" dictionary, this should be a generic codec type that
    #   can be used to find one of a number of codecs that can decode the stream.
    #   In the 'override' dictionary, if specified, this should be a specific codec.
    # For encode:
    #   A specific codec used to encode. The dictionary should contain any parameters
    #   the user has set for the codec, each beginning with "codec:".
    CODEC = 'codec'

class KnownColorPrimaries:
    '''
    Known RGB primary sets, aliases, and their colors in xy-space.

    Each set is a three-tuple with the respective xy-coordinates for R, G, and B.
    '''
    AdobeRGB = (v2f(0.6400, 0.3300), v2f(0.2100, 0.7100), v2f(0.1500, 0.0600))
    AppleRGB = (v2f(0.6250, 0.3400), v2f(0.2800, 0.5950), v2f(0.1550, 0.0700))
    sRGB = (v2f(0.6400, 0.3300), v2f(0.3000, 0.6000), v2f(0.1500, 0.0600))
    Rec709 = sRGB

class KnownIlluminants:
    '''
    Known illuminants and their colors in xy-space. Where specified, these describe
    the coordinates for the two-degree CIE standard observer.

    Source: http://en.wikipedia.org/w/index.php?title=Standard_illuminant&oldid=364143326#White_points_of_standard_illuminants
    '''
    D50 = v2f(0.34567, 0.35850)
    D65 = v2f(0.31271, 0.32902)

class StreamFormat(object):
    '''
    Describes the format of a stream.

    type - Type of the stream, such as "video" or "audio".
    detected - This dictionary contains attributes that were detected
        in the stream. The idea is to be able to re-detect the stream
        and compare to find changes.
    override - This dictionary contains attributes explicitly set on
        the stream. These may be attributes that weren't detected or
        for whatever reason were detected incorrectly.
    length - Length of the stream in frames or samples.
    '''
    yaml_tag = u'!StreamFormat'

    def __init__(self, type=None, detected=None, override=None, length=None):
        self.type = type
        self.detected = detected if detected is not None else {}
        self.override = override if override is not None else {}
        self.length = length

    def get(self, property, default=None):
        return self.override.get(property, self.detected.get(property, default))

    @property
    def adjusted_length(self):
        length = self.length

        if self.pulldown_type == PULLDOWN_23:
            source = process.Pulldown23RemovalFilter(None, self.pulldown_phase);
            length = source.get_new_length(length)

        return length

    @property
    def index(self):
        return self.get(ContainerProperty.STREAM_INDEX)

    @property
    def pixel_aspect_ratio(self):
        return (
            self.override.get(VideoProperty.SAMPLE_ASPECT_RATIO) or
            self.detected.get(VideoProperty.SAMPLE_ASPECT_RATIO) or fractions.Fraction(1, 1))

    @property
    def pulldown_type(self):
        return (
            self.override.get(VideoProperty.PULLDOWN_TYPE) or
            self.detected.get(VideoProperty.PULLDOWN_TYPE) or '')

    @property
    def pulldown_phase(self):
        return (
            self.override.get(VideoProperty.PULLDOWN_PHASE) or
            self.detected.get(VideoProperty.PULLDOWN_PHASE) or 0)

    @property
    def max_data_window(self):
        return (
            self.override.get(VideoProperty.MAX_DATA_WINDOW) or
            self.detected.get(VideoProperty.MAX_DATA_WINDOW))

    @property
    def thumbnail_box(self):
        return (
            self.override.get(VideoProperty.THUMBNAIL_WINDOW) or
            self.override.get(VideoProperty.MAX_DATA_WINDOW) or
            self.detected.get(VideoProperty.THUMBNAIL_WINDOW) or
            self.detected.get(VideoProperty.MAX_DATA_WINDOW))

    @property
    def frame_rate(self):
        return self.get(VideoProperty.FRAME_RATE, None)

    @property
    def sample_rate(self):
        return self.get(AudioProperty.SAMPLE_RATE, None)

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {'type': data.type}

        for name in ['detected', 'override', 'length']:
            if getattr(self, name):
                result[name] = getattr(self, name)

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class ContainerFormat(yaml.YAMLObject):
    '''
    Describes the properties of a container.

    path - Full path to the container file. In the future, we'll have to
        come back and accomodate relative paths and multi-file containers.
    streams - List of StreamFormats.
    detected - Detected attributes of the container.
    override - Overridden attributes of the container.
    '''
    yaml_tag = u'!ContainerFormat'

    def __init__(self):
        self.path = None
        self.detected = {}
        self.override = {}
        self.streams = []

    def get(self, property, default=None):
        return self.override.get(property, self.detected.get(property, default))

    @property
    def muxer(self):
        return self.get(ContainerProperty.MUXER)

    @property
    def format(self):
        return self.get(ContainerProperty.FORMAT)

_channel_assignment_guesses = {
    # Mono
    1: ['S'],
    # Stereo
    2: ['FL', 'FR'],
    # 3-stereo
    3: ['FL', 'FC', 'FR'],
    # Quad
    4: ['FL', 'FR', 'RL', 'RR'],
    # 5.0
    5: ['FL', 'FC', 'FR', 'SL', 'SR'],
    # 5.1
    6: ['FL', 'FC', 'FR', 'SL', 'SR', 'LF']
}

def _guess_channel_assignment(channels):
    if channels in channel_assignment_guesses:
        return channel_assignment_guesses[channels]

    return ['S' for x in xrange(channels)]

def _yamlreg(cls):
    yaml.add_representer(cls, cls.to_yaml)
    yaml.add_constructor(cls.yaml_tag, cls.from_yaml)

_yamlreg(StreamFormat)


