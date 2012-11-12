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

encoder = faac.AACAudioEncoder(audio_decoder, 0, 1000000, 48000, 2)

with open('test_audio.mkv', mode='wb') as myfile:
    writer = matroska.MatroskaWriter(myfile)

    # Matroska test writing; much of this is based on the x264 Matroska muxer
    ns = 1000000000
    timescale = 10000
    sample_rate = 48000

    writer.write_start(
        writing_app='Brian\'s test muxer',
        duration=0.0,
        timecode_scale=timescale)

    header = encoder.get_header()
    print('Len(header): ' + str(len(header)))

    audio_track = matroska.Track(
        number=1,
        uid=1,
        type_=matroska.TrackType.AUDIO,
        codec_id='A_AAC/MPEG4/MAIN',
        lacing=False,
        # Matroska codec specs LIED, the header is required
        codec_private=header,
        #default_duration_ns=int(float(ns) / float(sample_rate)),
        audio=matroska.TrackAudio(sample_rate, channels=2))

    writer.write_tracks([audio_track])

    # Time to actually code stuff!
    cluster = None
    cluster_time = 0
    cluster_size = 0
    first_frame = True
    frames_written = 0
    last_pts = 0

    try:
        packet = encoder.get_next_packet()

        while packet:
            print('Writing packet pts {0} dts {1}'.format(packet.pts, packet.dts))
            raw_timecode = round(float(packet.pts * ns) / float(sample_rate))
            abs_timecode = int(round(raw_timecode / timescale))

            # Write the block
            print('raw_timecode: {0}, abs_timecode {1}'.format(raw_timecode, abs_timecode))
            writer.write_simple_block(1, abs_timecode, packet.data, keyframe=packet.keyframe)
            frames_written += 1
            last_pts = abs_timecode

            packet = encoder.get_next_packet()
    finally:
        # TODO: This is severely incorrect, but it gets us to a result
        print('Duration: ' + str(float(last_pts)/float(ns)))
        writer.write_end(duration=float(last_pts))

