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

from fluggo.media import process, sources
from fluggo.media.formats import *

_FF_MUXER_TO_STD = {
    'avi': KnownMuxers.AVI,
    'dv': KnownMuxers.DV,
}

_FF_VIDEO_CODEC_TO_STD = {
    'dvvideo': KnownVideoCodecs.DV,
}

class FFMuxPlugin(object):
    supported_muxers = frozenset((KnownMuxers.AVI, KnownMuxers.DV, 'video/x-ffmpeg-avi', 'video/x-ffmpeg-dv'))

    @classmethod
    def get_stream(cls, container, id):
        # BJC: Naive version, this will need a change
        for stream in container.streams:
            if stream.id != id:
                continue

            if stream.type == 'video':
                return sources.VideoSource(process.FFVideoSource(container.path), stream)

        raise RuntimeError('Stream ID {0} not found in {1}.'.format(id, container.path))

    @classmethod
    def detect_container(cls, path):
        data = process.FFContainer(path)

        result = MediaContainer()
        result.detected[ContainerAttribute.MUXER] = _FF_MUXER_TO_STD.get(data.format_name, 'video/x-ffmpeg-' + data.format_name)
        result.path = path

        for stream in data.streams:
            stream_type = stream.type
            encoded = StreamFormat(stream_type)
            encoded.detected[ContainerAttribute.STREAM_ID] = stream.id

            if stream_type == 'video':
                encoded.detected[VideoAttribute.FRAME_RATE] = stream.real_frame_rate

                if stream.sample_aspect_ratio:
                    encoded.detected[VideoAttribute.SAMPLE_ASPECT_RATIO] = stream.sample_aspect_ratio

                rect = box2i(0, -1 if stream.codec in ('dvvideo') else 0,
                    stream.frame_size[0] - 1, stream.frame_size[1] - (2 if stream.codec in ('dvvideo') else 1))

                encoded.detected[VideoAttribute.MAX_DATA_WINDOW] = rect
                encoded.detected[VideoAttribute.CODEC] = _FF_VIDEO_CODEC_TO_STD.get(stream.codec, 'video/x-ffmpeg-' + stream.codec)

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

