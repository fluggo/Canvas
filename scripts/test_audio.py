import logging
handler = logging.StreamHandler()
handler.setLevel(logging.NOTSET)
handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

root_logger = logging.getLogger()
root_logger.setLevel(logging.NOTSET)
root_logger.addHandler(handler)

from fluggo.media import process, libav, x264, matroska, faac
import sys
import datetime
import struct
import fractions

process.enable_glib_logging(True)

audio_packet_source = libav.AVDemuxer(sys.argv[1], 1)
audio_decoder = libav.AVAudioDecoder(audio_packet_source, 'pcm_s16le', 2)

if True:
    encoder = faac.AACAudioEncoder(audio_decoder, 0, 1000000, 48000, 2)
    decoder2 = libav.AVAudioDecoder(encoder, 'aac', 2)

    frame_orig = audio_decoder.get_frame(0, 10000, 2)
    frame_dec = decoder2.get_frame(0, 10000, 2)

    for i in range(0, 10001):
        l1 = frame_orig.sample(i, 0)
        r1 = frame_orig.sample(i, 1)
        l2 = frame_dec.sample(i, 0)
        r2 = frame_dec.sample(i, 1)

        print('{4:05} L{0:+0.4f} R{1:+0.4f} vs L{2:+0.4f} R{3:+0.4f}'.format(l1, r1, l2, r2, i))

elif False:
    packet = audio_packet_source.get_next_packet()

    while packet:
        print('PTS {0} DTS {1} Duration {2}'.format(packet.pts, packet.dts, packet.duration))
        packet = audio_packet_source.get_next_packet()

elif True:
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

