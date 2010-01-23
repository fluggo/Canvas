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
import formats, process

class Source(object):
    pass

class VideoSource(Source):
    '''
    Abstract video source.
    '''
    def __init__(self, format):
        self.format = format
        self._passthru = process.VideoPassThroughFilter(None)

    def unload(self):
        self._passthru.set_source(None)

    def create_underlying_source(self):
        raise NotImplementedError

    def load(self):
        if not self._passthru.source():
            self._passthru.set_source(self.create_underlying_source())

    @property
    def _video_frame_source_funcs(self):
        '''Returns the block of functions for retreiving frames from this source.'''
        self.load()
        return self._passthru._video_frame_source_funcs

class AudioSource(Source):
    '''
    Abstract audio source.
    '''
    def __init__(self, format):
        self.format = format
        self._passthru = process.AudioPassThroughFilter(None)

    def unload(self):
        self._passthru.set_source(None)

    def create_underlying_source(self):
        raise NotImplementedError

    def load(self):
        if not self._passthru.source():
            self._passthru.set_source(self.create_underlying_source())

    @property
    def _audio_frame_source_funcs(self):
        '''Returns the block of functions for retrieving frames from this source.'''
        self.load()
        return self._passthru._audio_frame_source_funcs

class SourceRef(object):
    def get_source(self):
        raise NotImplementedError

class ContainerMeta(object):
    '''
    startTimecode - n
    regions -
        length - in frames
        type =
            bad - not part of any take
            margin - slate/prep
            good - useful
    dividers - divisions between useful parts of the source
    '''
    pass

class Container(yaml.YAMLObject):
    class EncodedAudio(AudioSource, yaml.YAMLObject):
        '''
        length = n samples

        '''
        yaml_tag = u'!encodedAudio'

        def __init__(self, container, stream_index, format, encoded_format):
            AudioSource.__init__(self, format)
            self.container = container
            self.stream_index = stream_index
            self.encoded_format = encoded_format
            self.length = None

        @classmethod
        def to_yaml(cls, dumper, data):
            return dumper.represent_mapping(u'!encodedAudio',
                {'stream_index': data.stream_index, 'format': data.format, 'encoded_format': data.encoded_format,
                'length': data.length})

        @classmethod
        def from_yaml(cld, loader, node):
            mapping = loader.construct_mapping(node)
            result = cls(None, mapping['stream_index'], mapping['format'], mapping['encoded_format'])
            result.length = mapping['length']
            return result

    class EncodedVideo(VideoSource, yaml.YAMLObject):
        '''
        container = ...
        streamNumber = ...
        encodedFormat = ...
        length = n frames

        '''
        yaml_tag = u'!encodedVideo'

        def __init__(self, container, stream_index, format, encoded_format):
            VideoSource.__init__(self, format)
            self.container = container
            self.stream_index = stream_index
            self.encoded_format = encoded_format
            self.length = None

        @classmethod
        def to_yaml(cls, dumper, data):
            return dumper.represent_mapping(u'!encodedVideo',
                {'stream_index': data.stream_index, 'format': data.format, 'encoded_format': data.encoded_format,
                'length': data.length})

        @classmethod
        def from_yaml(cld, loader, node):
            mapping = loader.construct_mapping(node)
            result = cls(None, mapping['stream_index'], mapping['format'], mapping['encoded_format'])
            result.length = mapping['length']
            return result

    '''
    filePath = '/path/to/file.mpeg'
    muxer = 'ffmpeg/avi'
    videoStreams = [EncodedVideoStream, ...]
    audioStreams = [EncodedAudioStream, ...]

    '''
    yaml_tag = u'!container'

    def __init__(self, file_path):
        self.file_path = file_path
        self.muxer = None
        self.video_streams = []
        self.audio_streams = []

    def get_stream(self, id):
        raise NotImplementedError

    @classmethod
    def discover(cls, path):
        data = process.FFContainer(path)
        result = cls(path)

        result.muxer = 'ffmpeg/' + data.format_name

        for stream in data.streams:
            stream_type = stream.type

            if stream_type == 'video':
                sar = stream.sample_aspect_ratio

                if not sar:
                    sar = fractions.Fraction(1, 1)

                rect = (0, 0, stream.frame_size[0] - 1, stream.frame_size[1] - 1)

                encoded = Container.EncodedVideo(result,
                    stream.index,
                    formats.VideoFormat(stream.real_frame_rate, rect, sar),
                    formats.EncodedVideoFormat('ffmpeg/' + stream.codec))
                encoded.length = stream.frame_count

                if not encoded.length:
                    # We need to give our best guess
                    if stream.duration:
                        encoded.length = int(round(fractions.Fraction(stream.duration) * stream.time_base * stream.real_frame_rate))
                else:
                    encoded.length = int(encoded.length)

                # Some things, like interlacing, require
                # peeking into the stream to guess them correctly
                result.video_streams.append(encoded)
            elif stream_type == 'audio':
                encoded = Container.EncodedAudio(result,
                    stream.index,
                    formats.AudioFormat(stream.sample_rate, formats.guess_channel_assignment(stream.channels)),
                    formats.EncodedAudioFormat('ffmpeg/' + stream.codec))

                if not encoded.length:
                    encoded.length = int(round(fractions.Fraction(stream.duration) * stream.time_base * stream.sample_rate))

                result.audio_streams.append(encoded)
            #else:
            #    result._streams.append(None)

        return result

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(u'!container',
            {'file_path': data.file_path, 'muxer': data.muxer, 'video_streams': data.video_streams,
                'audio_streams': data.audio_streams })

    @classmethod
    def from_yaml(cls, loader, node):
        mapping = loader.construct_mapping(node)
        data = cls(mapping.pop('file_path'))
        data.muxer = mapping['muxer']
        data.video_streams = mapping['video_streams']
        data.audio_streams = mapping['audio_streams']
        return data

