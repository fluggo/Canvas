# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009 Brian J. Crowell <brian@fluggo.com>
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

import fractions
import yaml

(PD_NONE, PD_23, PD_2332) = range(3)
_pd_names = {PD_NONE: 'none', PD_23: '2:3', PD_2332: '2:3:3:2'}

class VideoFormat(yaml.YAMLObject):
    '''
    Properties we'll look at here:

    rate (Fraction) = video frame rate
    interlaced (bool)
    sample_aspect_ratio
    pulldownType = PD_NONE, PD_23, PD_2332
    pulldownPhase
    colorPrimaries
    whitePoint
    frameSize

    channels = RGB, YUV, Luma, Depth, Alpha

    '''
    yaml_tag = u'!videoFormat'

    def __init__(self, frame_rate, frame_rect, sample_aspect_ratio):
        self.frame_rate = frame_rate
        self.frame_rect = frame_rect
        self.sample_aspect_ratio = sample_aspect_ratio
        self.interlaced = None
        self.pulldown_type = None
        self.pulldown_phase = None
        self.color_primaries = None
        self.white_point = None

    @classmethod
    def to_yaml(cls, dumper, data):
        mapping = {'frame_rate': str(data.frame_rate), 'frame_rect': list(data.frame_rect),
            'sample_aspect_ratio': str(data.sample_aspect_ratio)}

        if data.interlaced:
            mapping['interlaced'] = data.interlaced

        return dumper.represent_mapping(cls.yaml_tag, mapping)

    @classmethod
    def from_yaml(cls, loader, node):
        mapping = loader.construct_mapping(node, deep=True)
        result = cls(fractions.Fraction(mapping['frame_rate']), tuple(mapping['frame_size']),
                fractions.Fraction(mapping['sample_aspect_ratio']))

        result.interlaced = mapping.get('interlaced', None)

        return result

class EncodedVideoFormat(yaml.YAMLObject):
    '''
    codec = 'ffmpeg/mpeg'
    subsampling, siting, studio levels, input lut/lut3d, etc.

    transferFunction = ???
    yuvToRgbMatrix

    '''
    yaml_tag = u'!encodedVideoFormat'

    def __init__(self, codec):
        self.codec = codec

    @classmethod
    def to_yaml(cls, dumper, data):
        mapping = {'codec': data.codec}
        return dumper.represent_mapping(cls.yaml_tag, mapping)

    @classmethod
    def from_yaml(cls, loader, node):
        mapping = loader.construct_mapping(node)
        return cls(mapping['codec'])

class AudioFormat(yaml.YAMLObject):
    '''
    rate (fraction)
    channelAssignment = [channel, ...] where channel in:
        'FL'    Front Left
        'FR'    Front Right
        'FC'    Center
        'CL'    Front left of center
        'CR'    Front right of center
        'SL'    Side left
        'SR'    Side right
        'RL'    Rear left
        'RR'    Rear right
        'LF'    Low frequency
        'S'        Solo (no steering)
        None    Ignored channel

    '''
    yaml_tag = u'!audioFormat'

    def __init__(self, sample_rate, channel_assignment):
        self.sample_rate = sample_rate
        self.channel_assignment = channel_assignment

    @classmethod
    def to_yaml(cls, dumper, data):
        mapping = {'sample_rate': str(data.sample_rate), 'channel_assignment': data.channel_assignment}
        return dumper.represent_mapping(cls.yaml_tag, mapping)

    @classmethod
    def from_yaml(cls, loader, node):
        mapping = loader.construct_mapping(node)
        return cls(fractions.Fraction(mapping['sample_rate']), mapping['channel_assignment'])

class EncodedAudioFormat(yaml.YAMLObject):
    '''
    codec = 'ffmpeg/pcm_s16le'
    bitRate, etc.

    '''
    yaml_tag = u'!encodedAudioFormat'

    def __init__(self, codec):
        self.codec = codec

    @classmethod
    def to_yaml(cls, dumper, data):
        mapping = {'codec': data.codec}
        return dumper.represent_mapping(cls.yaml_tag, mapping)

    @classmethod
    def from_yaml(cls, loader, node):
        mapping = loader.construct_mapping(node)
        return cls(mapping['codec'])

channel_assignment_guesses = {
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

def guess_channel_assignment(channels):
    if channels in channel_assignment_guesses:
        return channel_assignment_guesses[channels]

    return ['S' for x in xrange(channels)]

