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

PULLDOWN_NONE = 'None'
PULLDOWN_23 = '2:3'
PULLDOWN_2332 = '2:3:3:2'

class ContainerAttribute:
    # A string identifying the muxer used to mux/demux this stream.
    # For decode:
    #   In the "detected" dictionary, this should be a generic muxer type that
    #   can be used to find one of a number of muxers that can decode the stream.
    #   In the 'override' dictionary, if specified, this should be a specific muxer.
    # For encode:
    #   A specific muxer used to encode. The dictionary should contain any parameters
    #   the user has set for the muxer, each beginning with "muxer:".
    MUXER = 'muxer'

    # Set in the "detected" dictionary for a stream, this is a container-specific identifier that
    # can be used to find the stream again
    STREAM_ID = 'stream_id'

class KnownMuxers:
    AVI = 'video/x-msvideo'
    DV = 'video/DV'

class KnownVideoCodecs:
    DV = 'video/DV'

class VideoAttribute:
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

class StreamFormat(yaml.YAMLObject):
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

    def __init__(self, type_):
        self.type = type_
        self.detected = {}
        self.override = {}
        self.length = None

    def get(self, property, default=None):
        return self.override.get(property, self.detected.get(property, default))

    @property
    def id(self):
        return self.get(ContainerAttribute.STREAM_ID)

    @property
    def pixel_aspect_ratio(self):
        return (
            self.override.get(VideoAttribute.SAMPLE_ASPECT_RATIO) or
            self.detected.get(VideoAttribute.SAMPLE_ASPECT_RATIO) or '')

    @property
    def pulldown_type(self):
        return (
            self.override.get(VideoAttribute.PULLDOWN_TYPE) or
            self.detected.get(VideoAttribute.PULLDOWN_TYPE) or '')

    @property
    def pulldown_phase(self):
        return (
            self.override.get(VideoAttribute.PULLDOWN_PHASE) or
            self.detected.get(VideoAttribute.PULLDOWN_PHASE) or 0)

    @property
    def thumbnail_box(self):
        return (
            self.override.get(VideoAttribute.THUMBNAIL_WINDOW) or
            self.override.get(VideoAttribute.MAX_DATA_WINDOW) or
            self.detected.get(VideoAttribute.THUMBNAIL_WINDOW) or
            self.detected.get(VideoAttribute.MAX_DATA_WINDOW))

class MediaContainer(yaml.YAMLObject):
    '''
    Describes the properties of a container.

    path - Full path to the container file. In the future, we'll have to
        come back and accomodate relative paths and multi-file containers.
    streams - List of StreamFormats.
    detected - Detected attributes of the container.
    override - Overridden attributes of the container.
    '''
    yaml_tag = u'!MediaContainer'

    def __init__(self):
        self.path = None
        self.detected = {}
        self.override = {}
        self.streams = []

    def get(self, property, default=None):
        return self.override.get(property, self.detected.get(property, default))

    @property
    def muxer(self):
        return self.get(ContainerAttribute.MUXER)

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

