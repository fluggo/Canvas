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

class VideoFormat(object):
    '''
    Properties we'll look at here:

    rate (Fraction) = video frame rate
    interlaced (bool)
    pulldownType = PD_NONE, PD_23, PD_2332
    pulldownPhase
    colorPrimaries
    whitePoint
    frameSize

    channels = RGB, YUV, Luma, Depth, Alpha
    '''
    def __init__(self, frameRate, frameSize):
        self.frameRate = frameRate
        self.frameSize = frameSize
        self.interlaced = None
        self.pulldownType = None
        self.pulldownPhase = None
        self.colorPrimaries = None
        self.whitePoint = None

class EncodedVideoFormat(object):
    '''
    codec = 'ffmpeg/mpeg'
    subsampling, siting, studio levels, input lut/lut3d, etc.

    transferFunction = ???
    yuvToRgbMatrix
    '''
    def __init__(self, codec):
        self.codec = codec

class AudioFormat(object):
    '''
    rate (int)
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
    def __init__(self, sampleRate, channelAssignment):
        self.sampleRate = sampleRate
        self.channelAssignment = channelAssignment

class EncodedAudioFormat(object):
    '''
    codec = 'ffmpeg/pcm_s16le'
    bitRate, etc.
    '''
    def __init__(self, codec):
        self.codec = codec

channelAssignmentGuesses = {
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

def guessChannelAssignment(channels):
    if channels in channelAssignmentGuesses:
        return channelAssignmentGuesses[channels]

    return ['S' for x in xrange(channels)]
