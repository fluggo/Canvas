# -*- coding: utf-8 -*-
#
# Matroska support in direct Python.
#
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

import datetime
import io
import struct
import math

from fluggo import logging
_log = logging.getLogger(__name__)

sizes = tuple(1 << (7 * i) for i in range(1, 9))

class Error(Exception):
    pass

def encode_size(value):
    for i, size in enumerate(sizes, start=1):
        if value >= size:
            continue

        return (size | value).to_bytes(i, byteorder='big')

    raise ValueError('Size too big to encode for EBML.')

def encode_int(value):
    value = int(value)

    if value == 0:
        return b'\0'

    result = value.to_bytes((value.bit_length() // 8) + 1, byteorder='big', signed=True)

    if result[0] == 0:
        return result[1:]
    else:
        return result

def make_void(size):
    if size == 0:
        return b''

    if size == 1:
        raise NotImplementedError('Can\'t void size of 1.')

    if (size - 2) < 128:
        return b'\xEC' + encode_size(size - 2) + (b'\x00' * (size - 2))

    raise NotImplementedError('Void not implemented for sizes larger than 129.')

class ebml:
    def __init__(self, element_id, contents, size=None):
        self.element_id = element_id
        self.contents = contents
        self.size = size
        self.written_size = None
        self.written_pos = None

    def add_bool(self, element_id, value, default=None):
        if value is None or value == default:
            return

        element = ebml(element_id, 1 if value else 0)
        self.contents.append(element)
        return element

    def add_float(self, element_id, value, default=None):
        if value is None or value == default:
            return

        element = ebml(element_id, float(value), size=4)
        self.contents.append(element)
        return element

    def add_double(self, element_id, value, default=None):
        if value is None or value == default:
            return

        element = ebml(element_id, float(value), size=8)
        self.contents.append(element)
        return element

    def add_int(self, element_id, value, default=None):
        if value is None or value == default:
            return

        element = ebml(element_id, int(value))
        self.contents.append(element)
        return element

    def add_date(self, element_id, value):
        if value is None:
            return

        element = ebml(element_id, value)
        self.contents.append(element)
        return element

    def add_string(self, element_id, value, default=None):
        if value is None or value == default:
            return

        if isinstance(value, str):
            value = value.encode('ascii')

        element = ebml(element_id, bytes(value))
        self.contents.append(element)
        return element

    def add_utf8(self, element_id, value, default=None):
        if value is None or value == default:
            return

        element = ebml(element_id, str(value))
        self.contents.append(element)
        return element

    def add_binary(self, element_id, value):
        if value is None:
            return

        element = ebml(element_id, bytes(value))
        self.contents.append(element)
        return element

    def update(self):
        pass

    def write(self, fd):
        '''Write or rewrite the element and its contents, if any.'''
        self.update()
        data = self.body(fd.tell())

        if self.written_size:
            if len(data) > self.written_size:
                raise RuntimeError('Could not rewrite element. The new version is too long. {0} > {1}'.format(len(data), self.written_size))

            fd.seek(self.written_pos, io.SEEK_SET)
            fd.write(data)
            fd.write(make_void(self.written_size - len(data)))
            fd.seek(0, io.SEEK_END)
        else:
            self.written_size = len(data)
            self.written_pos = fd.tell()
            fd.write(data)

    def write_close(self, fd):
        '''For open elements (contents=None), calculates the new size of the element
        based on the current position, and writes that size to the file.'''
        if self.contents is not None:
            raise Error("Can't close this element, it has contents.")

        if self.written_pos is None:
            raise Error("Can't close this element, it hasn't been written yet.")

        self.size = fd.tell() - self.written_size
        self.write(fd)

    def clear(self):
        self.contents = []

    def body(self, pos):
        result = bytearray()
        result.extend(encode_int(self.element_id))

        if self.contents is None:
            # We don't have any contents to write, but if they bothered to give
            # us a size, we'll use it
            if self.size:
                result.extend(encode_size(self.size))
            else:
                # Indeterminate size, but room to write one
                result.extend(b'\xFF' + make_void(7))
        elif isinstance(self.contents, datetime.datetime):
            # Python datetime sucks, but here we go...
            millenium = datetime.datetime(2001, 1, 1)
            diff = self.contents - millenium
            data = ((diff.days * 24 * 60 * 60 + diff.seconds) * 1000000 + diff.microseconds) * 1000

            result.extend(encode_size(8))
            result.extend(data.to_bytes(8, byteorder='big', signed=True))
        elif isinstance(self.contents, float):
            if self.size == 8:
                result.extend(encode_size(8))
                result.extend(struct.pack('>d', self.contents))
            else:
                result.extend(encode_size(4))
                result.extend(struct.pack('>f', self.contents))
        elif isinstance(self.contents, bool):
            data = b'\x01' if self.contents else b'\x00'
            result.extend(encode_size(1) + data)
        elif isinstance(self.contents, int):
            data = encode_int(self.contents)
            result.extend(encode_size(len(data)))
            result.extend(data)
        elif isinstance(self.contents, bytes) or isinstance(self.contents, bytearray):
            data = self.contents
            result.extend(encode_size(len(data)))
            result.extend(data)
        elif isinstance(self.contents, str):
            data = self.contents.encode()

            if self.size:
                if self.size < len(data):
                    raise ValueError('UTF-8 string too long for the given size.')
                else:
                    result.extend(encode_size(self.size))
                    result.extend(data)
                    result.extend(b'\x00' * (self.size - len(data)))
            else:
                result.extend(encode_size(len(data)))
                result.extend(data)
        else:
            # It had better be a list of elements
            data = bytearray()

            for element in self.contents:
                body = element.body(0)
                element.written_size = len(body)
                data.extend(body)

            result.extend(encode_size(len(data)))
            current_pos = pos + len(result)

            for element in self.contents:
                element.written_pos = current_pos
                current_pos += element.written_size

            result.extend(data)

            # Pad out to size if specified
            if self.size and self.size > len(data):
                result.extend(make_void(self.size - len(data)))

        return result


class EBMLIDs:
    Element = 0x1A45DFA3
    EBMLVersion = 0x4286
    EBMLReadVersion = 0x42F7
    EBMLMaxIDLength = 0x42F2
    EBMLMaxSizeLength = 0x42F3
    DocType = 0x4282
    DocTypeVersion = 0x4287
    DocTypeReadVersion = 0x4285

class SegmentIDs:
    Element = 0x18538067
    SeekHead = 0x114D9B74
    Seek = 0x4DBB
    SeekID = 0x53AB
    SeekPosition = 0x53AC
    class Info:
        Element = 0x1549A966
        SegmentUID = 0x73A4
        SegmentFilename = 0x7384
        PrevUID = 0x3CB923
        PrevFilename = 0x3C83AB
        NextUID = 0x3EB923
        NextFilename = 0x3E83BB
        SegmentFamily = 0x4444
        class ChapterTranslate:
            Element = 0x6924
            EditionUID = 0x69FC
            Codec = 0x69BF
            ID = 0x69A5
        TimecodeScale = 0x2AD7B1
        Duration = 0x4489
        DateUTC = 0x4461
        Title = 0x7BA9
        MuxingApp = 0x4D80
        WritingApp = 0x5741
    class Tracks:
        Element = 0x1654AE6B
        class TrackEntry:
            Element = 0xAE
            Number = 0xD7
            UID = 0x73C5
            Type = 0x83
            FlagEnabled = 0xB9
            FlagDefault = 0x88
            FlagForced = 0x55AA
            FlagLacing = 0x9C
            MinCache = 0x6DE7
            MaxCache = 0x6DF8
            DefaultDuration = 0x23E383
            TrackTimecodeScale = 0x23314F
            MaxBlockAdditionID = 0x55EE
            Name = 0x536E
            Language = 0x22B59C
            CodecID = 0x86
            CodecPrivate = 0x63A2
            CodecName = 0x258688
            class Video:
                Element = 0xE0
                FlagInterlaced = 0x9A
                StereoMode = 0x53B8
                PixelWidth = 0xB0
                PixelHeight = 0xBA
                PixelCropBottom = 0x54AA
                PixelCropTop = 0x54BB
                PixelCropLeft = 0x54CC
                PixelCropRight = 0x54DD
                DisplayWidth = 0x54B0
                DisplayHeight = 0x54BA
                DisplayUnit = 0x54B2
                AspectRatioType = 0x54B3
                ColourSpace = 0x2EB524
    class Cluster:
        Element = 0x1F43B675
        Timecode = 0xE7
        SimpleBlock = 0xA3

class TrackType:
    VIDEO = 1
    AUDIO = 2
    COMPLEX = 3
    LOGO = 0x10
    SUBTITLE = 0x11
    BUTTONS = 0x12
    CONTROL = 0x20

class DisplayUnit:
    PIXELS = 0
    CENTIMETERS = 1
    INCHES = 2
    DISPLAY_ASPECT_RATIO = 3

class TrackVideo(ebml):
    Element = 0xE0
    FlagInterlaced = 0x9A
    StereoMode = 0x53B8
    PixelWidth = 0xB0
    PixelHeight = 0xBA
    PixelCropBottom = 0x54AA
    PixelCropTop = 0x54BB
    PixelCropLeft = 0x54CC
    PixelCropRight = 0x54DD
    DisplayWidth = 0x54B0
    DisplayHeight = 0x54BA
    DisplayUnit = 0x54B2
    AspectRatioType = 0x54B3
    ColourSpace = 0x2EB524

    def __init__(self, pixel_width, pixel_height, pixel_crop=None, interlaced=False,
            display_width=None, display_height=None, display_unit=None):
        ebml.__init__(self, self.Element, [])
        self.pixel_width = pixel_width
        self.pixel_height = pixel_height
        self.pixel_crop = pixel_crop
        self.interlaced = interlaced
        self.display_width = display_width
        self.display_height = display_height
        self.display_unit = display_unit

        self.add_bool(self.FlagInterlaced, self.interlaced, default=False)
        self.add_int(self.PixelWidth, self.pixel_width)
        self.add_int(self.PixelHeight, self.pixel_height)

        if self.pixel_crop:
            self.add_int(self.PixelCropBottom, self.pixel_crop.bottom)
            self.add_int(self.PixelCropTop, self.pixel_crop.top)
            self.add_int(self.PixelCropLeft, self.pixel_crop.left)
            self.add_int(self.PixelCropRight, self.pixel_crop.right)

        self.add_int(self.DisplayWidth, self.display_width)
        self.add_int(self.DisplayHeight, self.display_height)
        self.add_int(self.DisplayUnit, self.display_unit)

class TrackAudio(ebml):
    Element = 0xE1
    SamplingFrequency = 0xB5
    OutputSamplingFrequency = 0x78B5
    Channels = 0x9F
    BitDepth = 0x6264

    def __init__(self, sample_rate=8000.0, output_sample_rate=None, channels=1, bit_depth=None):
        ebml.__init__(self, self.Element, [])
        self.sample_rate = sample_rate
        self.output_sample_rate = output_sample_rate
        self.channels = channels
        self.bit_depth = bit_depth

        self.add_float(self.SamplingFrequency, self.sample_rate, default=8000.0)
        self.add_float(self.OutputSamplingFrequency, self.output_sample_rate)
        self.add_int(self.Channels, self.channels, default=1)
        self.add_int(self.BitDepth, self.bit_depth)

class Track(ebml):
    Element = 0xAE
    Number = 0xD7
    UID = 0x73C5
    Type = 0x83
    FlagEnabled = 0xB9
    FlagDefault = 0x88
    FlagForced = 0x55AA
    FlagLacing = 0x9C
    MinCache = 0x6DE7
    MaxCache = 0x6DF8
    DefaultDuration = 0x23E383
    TrackTimecodeScale = 0x23314F
    MaxBlockAdditionID = 0x55EE
    Name = 0x536E
    Language = 0x22B59C
    CodecID = 0x86
    CodecPrivate = 0x63A2
    CodecName = 0x258688

    def __init__(self, number, uid, type_, codec_id, enabled=True, default=True,
            forced=False, lacing=True, min_cache=0, max_cache=None, default_duration_ns=None,
            name=None, language=None, codec_private=None, codec_name=None,
            video=None, audio=None):
        ebml.__init__(self, self.Element, [])
        self.number = number
        self.uid = uid
        self.type_ = type_
        self.enabled = enabled
        self.default = default
        self.forced = forced
        self.lacing = lacing
        self.min_cache = min_cache
        self.max_cache = max_cache
        self.default_duration_ns = default_duration_ns
        self.name = name
        self.language = language
        self.codec_id = codec_id
        self.codec_private = codec_private
        self.codec_name = codec_name
        self.video = video
        self.audio = audio

        self.add_int(self.Number, self.number)
        self.add_int(self.UID, self.uid)
        self.add_int(self.Type, self.type_)
        self.add_bool(self.FlagEnabled, self.enabled, default=True)
        self.add_bool(self.FlagDefault, self.default, default=True)
        self.add_bool(self.FlagForced, self.forced, default=False)
        self.add_bool(self.FlagLacing, self.lacing, default=True)
        self.add_int(self.MinCache, self.min_cache, default=0)
        self.add_int(self.MaxCache, self.max_cache)
        self.add_int(self.DefaultDuration, self.default_duration_ns)
        self.add_utf8(self.Name, self.name)
        self.add_string(self.Language, self.language)
        self.add_string(self.CodecID, self.codec_id)
        self.add_binary(self.CodecPrivate, self.codec_private)
        self.add_utf8(self.CodecName, self.codec_name)

        if self.video:
            self.contents.append(self.video)

        if self.audio:
            self.contents.append(self.audio)

class TrackList(ebml):
    Element = 0x1654AE6B

    def __init__(self, tracks):
        ebml.__init__(self, self.Element, tracks)

class Segment(ebml):
    Element = 0x18538067

    def __init__(self):
        ebml.__init__(self, self.Element, None)

    def body(self, pos):
        # This one is special, because seek items refer to offsets from the
        # beginning of the segment, which is after the size. So we write a big
        # size placeholder, such that the segment always begins in the same place.
        result = bytearray()
        result.extend(encode_int(self.Element))
        result.extend(encode_int(0x0100000000000000 | (self.size or 0)))

        return result

class SeekHead(ebml):
    Element = 0x114D9B74

    def __init__(self, entries, max_count=None):
        ebml.__init__(self, self.Element, entries,
            size=(max_count * Seek.MAX_SIZE) if max_count else None)

class Seek(ebml):
    Element = 0x4DBB
    SeekID = 0x53AB
    SeekPosition = 0x53AC

    # Max size of this element is Element + Size, then SeekID + Size + ElementID,
    # then SeekPosition + Size + up-to-8-byte-position
    MAX_SIZE = 2 + 1 + (2 + 1 + 4) + (2 + 1 + 8)

    def __init__(self, element_id, position):
        ebml.__init__(self, self.Element, [])
        self.add_int(self.SeekID, element_id)
        self.add_int(self.SeekPosition, position)

    @classmethod
    def from_element(cls, segment, element):
        pos = element.written_pos - (segment.written_pos + segment.written_size)
        return cls(element.element_id, pos)

class SegmentInfo(ebml):
    Element = 0x1549A966
    SegmentUID = 0x73A4
    SegmentFilename = 0x7384
    PrevUID = 0x3CB923
    PrevFilename = 0x3C83AB
    NextUID = 0x3EB923
    NextFilename = 0x3E83BB
    SegmentFamily = 0x4444
    class ChapterTranslate:
        Element = 0x6924
        EditionUID = 0x69FC
        Codec = 0x69BF
        ID = 0x69A5
    TimecodeScale = 0x2AD7B1
    Duration = 0x4489
    DateUTC = 0x4461
    Title = 0x7BA9
    MuxingApp = 0x4D80
    WritingApp = 0x5741

    def __init__(self, writing_app, muxing_app='Fluggo MatroskaWriter', duration=None, date_utc=None, title=None,
            timecode_scale=1000000, uid=None, filename=None,
            prev_uid=None, prev_filename=None,
            next_uid=None, next_filename=None, segment_family_uid=None):
        ebml.__init__(self, self.Element, [])
        self.muxing_app = muxing_app
        self.writing_app = writing_app
        self.duration = duration
        self.date_utc = date_utc
        self.title = title
        self.timecode_scale = timecode_scale
        self.uid = uid
        self.filename = filename
        self.prev_uid = prev_uid
        self.prev_filename = prev_filename
        self.next_uid = next_uid
        self.next_filename = next_filename
        self.segment_family_uid = segment_family_uid

        self.duration_element = self.add_float(self.Duration, self.duration)
        self.add_utf8(self.Title, self.title)
        self.add_utf8(self.MuxingApp, self.muxing_app)
        self.add_utf8(self.WritingApp, self.writing_app)
        self.add_date(self.DateUTC, self.date_utc)
        self.add_int(self.TimecodeScale, self.timecode_scale)
        self.add_binary(self.SegmentUID, self.uid)
        self.add_utf8(self.SegmentFilename, self.filename)
        self.add_binary(self.PrevUID, self.prev_uid)
        self.add_utf8(self.PrevFilename, self.prev_filename)
        self.add_binary(self.NextUID, self.next_uid)
        self.add_utf8(self.NextFilename, self.next_filename)
        self.add_binary(self.SegmentFamily, self.segment_family_uid)

class Cluster(ebml):
    Element = 0x1F43B675
    Timecode = 0xE7

    def __init__(self, timecode):
        ebml.__init__(self, self.Element, [])
        self.add_int(self.Timecode, timecode)

class SimpleBlock(ebml):
    Element = 0xA3

    def __init__(self, track, relative_pts, data, keyframe=True, invisible=False, discardable=False):
        contents = bytearray()
        contents.extend(encode_size(track))
        contents.extend(relative_pts.to_bytes(2, byteorder='big', signed=True))
        contents.append(
            (0x80 if keyframe else 0) |
            (0x08 if invisible else 0) |
            (0x01 if discardable else 0))
        contents.extend(data)

        ebml.__init__(self, self.Element, contents)

class MatroskaWriter:
    def __init__(self, fd):
        self.fd = fd
        self.segment = None
        self.segment_info = None
        self.top_seek_head = None

        self.max_cluster_size = 5*1024*1024
        self.cluster = None
        self.cluster_size = 0
        self.cluster_time = 0

    def write_start(self, *args, **kw):
        # Write the EBML header
        header = ebml(EBMLIDs.Element, [
            ebml(EBMLIDs.EBMLVersion, 1),
            ebml(EBMLIDs.EBMLReadVersion, 1),
            ebml(EBMLIDs.EBMLMaxIDLength, 4),
            ebml(EBMLIDs.EBMLMaxSizeLength, 8),
            ebml(EBMLIDs.DocType, 'matroska'),
            ebml(EBMLIDs.DocTypeVersion, 2),
            ebml(EBMLIDs.DocTypeReadVersion, 2)])

        header.write(self.fd)
        print('Wrote header...')

        # Start the segment
        self.segment = Segment()
        self.segment.write(self.fd)
        print('Wrote segment start...')

        # Follow what libav does: write a seek head at the beginning of the file,
        # which points to the segment info and tracks, and a second seek head at
        # the end, which points to all of the clusters.
        self.top_seek_head = SeekHead([], max_count=3)
        self.top_seek_head.write(self.fd)
        print('Wrote top seek head...')

        self.bottom_seek_head = SeekHead([])

        self.segment_info = SegmentInfo(*args, **kw)
        self.segment_info.write(self.fd)
        print('Wrote segment info...')

        self.top_seek_head.contents.append(Seek.from_element(self.segment, self.segment_info))

    def write_tracks(self, tracks):
        tracks = TrackList(tracks)
        tracks.write(self.fd)
        print('Wrote tracks...')
        self.top_seek_head.contents.append(Seek.from_element(self.segment, tracks))

    def write_top_level(self, element):
        element.write(self.fd)
        self.bottom_seek_head.contents.append(Seek.from_element(self.segment, element))

    def write_simple_block(self, track, pts, data, keyframe=True, invisible=False, discardable=False):
        # TODO: Support cue points for video keyframes

        # Limit cluster to particular PTS / size
        if self.cluster and (abs(pts - self.cluster_time) > 32767 or self.cluster_size > self.max_cluster_size):
            self.write_top_level(self.cluster)
            self.cluster = None
            self.cluster_size = 0

        if not self.cluster:
            self.cluster_time = pts
            self.cluster = Cluster(pts)

        self.cluster.contents.append(SimpleBlock(track, (pts - self.cluster_time),
            data, keyframe=keyframe, invisible=invisible, discardable=discardable))
        self.cluster_size += len(data)

    def write_end(self, duration=None):
        # Write the final cluster
        if self.cluster:
            self.write_top_level(self.cluster)
            self.cluster = None

        # Pop the bottom seek head at the end
        self.bottom_seek_head.write(self.fd)

        # Put a pointer to it at the beginning
        self.top_seek_head.contents.append(Seek.from_element(self.segment, self.bottom_seek_head))
        self.top_seek_head.write(self.fd)

        # Rewrite the duration if that's been asked for
        if duration:
            print('Rewriting duration to {0}'.format(duration))
            self.segment_info.duration_element.contents = duration
            print(self.segment_info.duration_element.written_pos)
            print(self.segment_info.duration_element.written_size)
            self.segment_info.duration_element.write(self.fd)

        # Close the segment
        self.segment.write_close(self.fd)

def write_audio_pcm_float(filename, source, min_sample, max_sample, sample_rate, channels,
        writing_app='Fluggo media test writer'):
    '''Writes a raw PCM audio Matroska containing floating point data from the
    given source. It's a convenience method for testing your audio sources.'''

    with open(filename, mode='wb') as myfile:
        writer = MatroskaWriter(myfile)

        # Matroska test writing; much of this is based on the x264 Matroska muxer
        ns = 1000000000
        timescale = math.floor(ns/sample_rate)

        writer.write_start(
            writing_app=writing_app,
            duration=0.0,
            timecode_scale=timescale)

        audio_track = Track(
            number=1,
            uid=1,
            type_=TrackType.AUDIO,
            codec_id='A_PCM/FLOAT/IEEE',
            lacing=False,
            audio=TrackAudio(sample_rate, channels=channels, bit_depth=32))

        writer.write_tracks([audio_track])

        # Time to actually code stuff!
        cluster = None
        cluster_time = 0
        cluster_size = 0
        first_frame = True
        frames_written = 0
        last_pts = 0
        samples_per_block = 1024
        last_sample = min_sample

        sample_struct = struct.Struct('<' + ('f' * channels))

        try:
            while last_sample <= max_sample:
                # Get 1024 samples at a time
                frame = source.get_frame(last_sample,
                    min(last_sample + samples_per_block - 1, max_sample), channels)

                _log.debug('Writing packet {0} - {1}', frame.full_min_sample, frame.full_max_sample)
                raw_timecode = round(float(frame.current_min_sample * ns) / float(sample_rate))
                abs_timecode = int(round(raw_timecode / timescale))

                # Pack into audio packet
                packet = bytearray(sample_struct.size * (frame.full_max_sample - frame.full_min_sample + 1))

                for i in range(frame.full_min_sample, frame.full_max_sample + 1):
                    sample_struct.pack_into(packet,
                        (i - frame.full_min_sample) * sample_struct.size,
                        *(frame.sample(i, ch) for ch in range(channels)))

                # Write the block
                _log.debug('raw_timecode: {0}, abs_timecode {1}', raw_timecode, abs_timecode)
                writer.write_simple_block(1, abs_timecode, packet, keyframe=True)
                frames_written += 1

                raw_timecode = round(float((frame.current_max_sample + 1) * ns) / float(sample_rate))
                abs_timecode = int(round(raw_timecode / timescale))

                last_pts = abs_timecode
                last_sample += samples_per_block
        finally:
            _log.debug('Duration: ' + str(float(last_pts * timescale)/float(ns)))
            writer.write_end(duration=float(last_pts))

