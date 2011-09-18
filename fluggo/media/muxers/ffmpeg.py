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

from fluggo.media import process, ffmpeg
from fluggo.media.formats import *
from fluggo.editor import model

_CODEC_FORMAT_MAP = {}

for (name, const) in ffmpeg.__dict__.iteritems():
    if name.startswith('CONST_ID_'):
        _CODEC_FORMAT_MAP[const] = name[9:]

_FF_CONTAINER_FORMAT_TO_STD = {
    'avi': KnownContainerFormat.AVI,
    'dv': KnownContainerFormat.DV,
}

_FF_VIDEO_FORMAT_TO_STD = {
    'dvvideo': KnownVideoFormat.DV,
}

class FFMuxPlugin(object):
    @classmethod
    def handles_container(cls, container):
        return container.muxer in cls.supported_muxers

    @classmethod
    def get_stream(cls, container, index):
        # BJC: Naive version, this will need a change
        for stream in container.streams:
            if stream.index != index:
                continue

            demux = ffmpeg.FFDemuxer(container.path, index)

            if stream.type == 'video':
                # TODO: Right now, anticipate only DV video
                decode = ffmpeg.FFVideoDecoder(demux, 'dvvideo')
                source = process.DVReconstructionFilter(decode)

                if stream.pulldown_type == '2:3':
                    source = process.Pulldown23RemovalFilter(source, stream.pulldown_phase)

                return model.VideoStream(source, stream)
            elif stream.type == 'audio':
                return model.AudioStream(ffmpeg.FFAudioDecoder(demux, 'pcm_s16le', 2), stream)

            raise RuntimeError('Could not identify stream type.')

        raise RuntimeError('Stream ID {0} not found in {1}.'.format(index, container.path))

    @classmethod
    def detect_container(cls, path):
        # Should return None or throw exception if unable to handle
        data = ffmpeg.FFContainer(path)

        result = ContainerFormat()
        result.detected[ContainerProperty.FORMAT] = _FF_CONTAINER_FORMAT_TO_STD.get(data.format_name, 'ffmpeg/' + data.format_name)
        result.detected[ContainerProperty.MUXER] = 'ffmpeg'
        result.path = path

        for (i, stream) in enumerate(data.streams):
            stream_type = stream.type
            encoded = StreamFormat(stream_type)
            encoded.detected[ContainerProperty.STREAM_ID] = stream.id
            encoded.detected[ContainerProperty.STREAM_INDEX] = i

            if stream_type == 'video':
                encoded.detected[VideoProperty.FRAME_RATE] = stream.real_frame_rate

                if stream.sample_aspect_ratio:
                    encoded.detected[VideoProperty.SAMPLE_ASPECT_RATIO] = stream.sample_aspect_ratio

                rect = box2i(0, -1 if stream.codec in ('dvvideo') else 0,
                    stream.frame_size[0] - 1, stream.frame_size[1] - (2 if stream.codec in ('dvvideo') else 1))

                encoded.detected[VideoProperty.MAX_DATA_WINDOW] = rect
                encoded.detected[VideoProperty.CODEC] = _FF_VIDEO_FORMAT_TO_STD.get(_CODEC_FORMAT_MAP.get(stream.codec_id, stream.codec).lower(), 'ffmpeg/' + stream.codec)

                encoded.length = stream.frame_count

                if not encoded.length:
                    # We need to give our best guess
                    if stream.duration:
                        encoded.length = int(round(fractions.Fraction(stream.duration) * stream.time_base * stream.real_frame_rate))
                    elif data.duration:
                        encoded.length = int(round(fractions.Fraction(data.duration, 1000000) * stream.real_frame_rate))
                else:
                    encoded.length = int(encoded.length)

                # Some things, like interlacing, require
                # peeking into the stream to guess them correctly
                result.streams.append(encoded)
    #        elif stream_type == 'audio':
    #            encoded = Container.EncodedAudio(result,
    #                stream.index,
    #                formats.AudioFormat(stream.sample_rate, formats.guess_channel_assignment(stream.channels)),
    #                formats.EncodedAudioFormat('ffmpeg/' + stream.codec))
    #
    #            if not encoded.length:
    #                if stream.duration:
    #                    encoded.length = int(round(fractions.Fraction(stream.duration) * stream.time_base * stream.sample_rate))
    #                elif data.duration:
    #                    encoded.length = int(round(fractions.Fraction(data.duration, 1000000) * stream.sample_rate))
    #
    #            result.audio_streams.append(encoded)
            #else:
            #    result._streams.append(None)

        return result

