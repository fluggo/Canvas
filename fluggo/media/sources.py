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

class Source(object):
    pass

class VideoSource(Source):
    '''
    Abstract video source.
    '''
    def __init__(self, format):
        self.format = format
        self._passthru = fluggo.media.VideoPassThroughFilter(None)

    def unload(self):
        self._passthru.setSource(None)

    def createUnderlyingSource(self):
        raise NotImplementedError()

    def load(self):
        if not self._passthru.source():
            self._passthru.setSource(self.createUnderlyingSource())

    @property
    def _videoFrameSourceFuncs(self):
        '''Returns the block of functions for retreiving frames from this source.'''
        self.load()
        return self._passthru._videoFrameSourceFuncs

class AudioSource(Source):
    '''
    Abstract audio source.
    '''
    def __init__(self, format):
        self.format = format
        self._passthru = fluggo.media.AudioPassThroughFilter(None)

    def unload(self):
        self._passthru.setSource(None)

    def createUnderlyingSource(self):
        raise NotImplementedError()

    def load(self):
        if not self._passthru.source():
            self._passthru.setSource(self.createUnderlyingSource())

    @property
    def _audioFrameSourceFuncs(self):
        '''Returns the block of functions for retrieving frames from this source.'''
        self.load()
        return self._passthru._audioFrameSourceFuncs

class MediaContainer(object):
    class EncodedAudioStream(AudioSource):
        '''
        length = n samples
        '''
        def __init__(self, container, streamIndex, format, encodedFormat):
            AudioSource.__init__(self, format)
            self.container = container
            self.streamIndex = streamIndex
            self.encodedFormat = encodedFormat
            self.length = None

    class EncodedVideoStream(VideoSource):
        '''
        container = ...
        streamNumber = ...
        encodedFormat = ...
        length = n frames
        '''
        def __init__(self, container, streamIndex, format, encodedFormat):
            VideoSource.__init__(self, format)
            self.container = container
            self.streamIndex = streamIndex
            self.encodedFormat = encodedFormat
            self.length = None

    '''
    filePath = '/path/to/file.mpeg'
    muxer = 'ffmpeg/avi'
    videoStreams = [EncodedVideoStream, ...]
    audioStreams = [EncodedAudioStream, ...]
    '''
    def __init__(self, filePath):
        self.filePath = filePath
        self.muxer = None
        self._streams = []

    def getStream(self, id):
        raise NotImplementedError()

def discoverContainer(path):
    data = fluggo.media.FFContainer(path)
    result = MediaContainer(path)

    result.muxer = 'ffmpeg/' + data.formatName

    for stream in data.streams:
        streamType = stream.type

        if stream.type == 'video':
            encoded = MediaContainer.EncodedVideoStream(result, stream.index,
                VideoFormat(stream.realFrameRate, stream.frameSize),
                EncodedVideoFormat('ffmpeg/' + stream.codec))
            encoded.length = stream.frameCount

            if not encoded.length:
                # We need to give our best guess
                encoded.length = round(Fraction(stream.duration) * stream.timeBase * stream.realFrameRate)

            # Some things, like interlacing, require
            # peeking into the stream to guess them correctly
            result._streams.append(encoded)
        elif stream.type == 'audio':
            encoded = MediaContainer.EncodedAudioStream(result, stream.index,
                AudioFormat(stream.sampleRate, guessChannelAssignment(stream.channels)),
                EncodedAudioFormat('ffmpeg/' + stream.codec))
            result._streams.append(encoded)
        else:
            result._streams.append(None)

    return result
