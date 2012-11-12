import logging
handler = logging.StreamHandler()
handler.setLevel(logging.INFO)
handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

root_logger = logging.getLogger()
root_logger.setLevel(logging.INFO)
root_logger.addHandler(handler)

from fluggo.media import process, libav, x264, matroska, faac
import sys
import datetime
import struct
import fractions
import collections
import math

process.enable_glib_logging(True)

if not process.check_context_supported():
    print("Sorry, your drivers don't support the minimum")
    print("requirements for this library. Consider using a")
    print("software driver.")
    exit()

packet_source = libav.AVDemuxer(sys.argv[1], 0)
dv_decoder = libav.AVVideoDecoder(packet_source, 'dvvideo')
dv_reconstruct = process.DVReconstructionFilter(dv_decoder)
mpeg2_subsample = process.MPEG2SubsampleFilter(dv_reconstruct)

audio_packet_source = libav.AVDemuxer(sys.argv[1], 1)
audio_decoder = libav.AVAudioDecoder(audio_packet_source, 'pcm_s16le', 2)

params = x264.X264EncoderParams(preset='ultrafast', width=720, height=480,
    frame_rate=fractions.Fraction(30000, 1001), constant_ratefactor=23.0,
    sample_aspect_ratio=fractions.Fraction(10, 11), annex_b=False, repeat_headers=False, interlaced=True)
encoder = x264.X264VideoEncoder(mpeg2_subsample, 0, 1000, params)

with open('test.mkv', mode='wb') as myfile:
    writer = matroska.MatroskaWriter(myfile)

    # Matroska test writing; much of this is based on the x264 Matroska muxer
    ns = 1000000000
    timescale = 1000000
    frame_rate = fractions.Fraction(30000, 1001)
    sar = 40.0 / 33.0

    writer.write_start(
        writing_app='Brian\'s test muxer',
        duration=0.0,
        timecode_scale=timescale)

    # You want a rhyme or reason for this, ask the x264 devs
    private = bytearray()
    sps = encoder.sps[4:]
    pps = encoder.pps[4:]

    private.append(1)
    private.extend(sps[1:4])
    private.extend(b'\xFF\xE1')     # One SPS
    private.extend(len(sps).to_bytes(2, byteorder='big'))
    private.extend(sps)
    private.append(1)               # One PPS
    private.extend(len(pps).to_bytes(2, byteorder='big'))
    private.extend(pps)

    video_track = matroska.Track(
        number=1,
        uid=1,
        type_=matroska.TrackType.VIDEO,
        codec_id='V_MPEG4/ISO/AVC',
        codec_private=private,
        lacing=False,
        default_duration_ns=int(float(ns) / float(frame_rate)),
        video=matroska.TrackVideo(720, 480,
            interlaced=True,
            display_width=int(round(720 * sar)),
            display_height=480,
            display_unit=matroska.DisplayUnit.PIXELS))

    writer.write_tracks([video_track])

    # Time to actually code stuff!
    cluster = None
    cluster_time = 0
    cluster_size = 0
    first_frame = True
    frames_written = 0

    try:
        packet = encoder.get_next_packet()

        while packet:
            print('Writing packet pts {0} dts {1}'.format(packet.pts, packet.dts))
            scaled_pts = (packet.pts * ns * frame_rate.denominator) // (frame_rate.numerator * timescale)

            data = packet.data

            # Stick the SEI in the first frame
            if first_frame:
                data = encoder.sei + packet.data
                first_frame = False

            # Write the block
            # Note that if we know which frames are B-frames, we can set skippable
            writer.write_simple_block(1, scaled_pts, data, keyframe=packet.keyframe)
            frames_written += 1

            packet = encoder.get_next_packet()
    finally:
        writer.write_end(duration=float(ns * frames_written)/(float(frame_rate) * float(timescale)))

