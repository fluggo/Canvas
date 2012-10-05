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

if not process.hardware_mode_available:
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

if False:
    encoder = x264.X264VideoEncoder(mpeg2_subsample, 0, 1000, preset='veryfast')

    with open('test.264', mode='wb') as myfile:
        #myfile.write(encoder.get_header())
        packet = encoder.get_next_packet()

        while packet:
            print('Writing packet pts {0} dts {1}'.format(packet.pts, packet.dts))
            myfile.write(packet.data)
            packet = encoder.get_next_packet()
elif False:
    packet = audio_packet_source.get_next_packet()

    while packet:
        print('PTS {0} DTS {1} Duration {2}'.format(packet.pts, packet.dts, packet.duration))
        packet = audio_packet_source.get_next_packet()

elif False:
    matroska.write_audio_pcm_float('test_pcm.mkv', audio_decoder, 0, 1000000,
        sample_rate=48000, channels=2)

elif True:
    frame_rate = fractions.Fraction(30000, 1001)
    sample_rate = 48000
    writing_app = "Brian's test MKV writer"
    sar = fractions.Fraction(40, 33)

    min_frame, max_frame = 0, 110161

    #min_sample = round(min_frame * sample_rate / float(frame_rate))
    #max_sample = round(max_frame * sample_rate / float(frame_rate))
    min_sample, max_sample = 0, 177163458
    print('min_sample: {0}, max_sample: {1}'.format(min_sample, max_sample))

    params = x264.X264EncoderParams(preset='ultrafast', width=720, height=480,
        frame_rate=frame_rate, constant_ratefactor=23.0, sample_aspect_ratio=sar,
        annex_b=False, repeat_headers=False)
    video_encoder = x264.X264VideoEncoder(mpeg2_subsample, min_frame, max_frame, params)
    audio_encoder = faac.AACAudioEncoder(audio_decoder, min_sample, max_sample, 48000, 2)

    with open('test.mkv', mode='wb') as myfile:
        writer = matroska.MatroskaWriter(myfile)

        # Matroska test writing; much of this is based on the x264 Matroska muxer
        ns = 1000000000
        timescale = math.floor(ns/sample_rate)

        writer.write_start(
            writing_app=writing_app,
            duration=0.0,
            timecode_scale=timescale)

        # You want a rhyme or reason for this, ask the x264 devs
        video_header = bytearray()
        sps = video_encoder.sps[4:]
        pps = video_encoder.pps[4:]

        video_header.append(1)
        video_header.extend(sps[1:4])
        video_header.extend(b'\xFF\xE1')     # One SPS
        video_header.extend(len(sps).to_bytes(2, byteorder='big'))
        video_header.extend(sps)
        video_header.append(1)               # One PPS
        video_header.extend(len(pps).to_bytes(2, byteorder='big'))
        video_header.extend(pps)

        video_track = matroska.Track(
            number=1,
            uid=1,
            type_=matroska.TrackType.VIDEO,
            codec_id='V_MPEG4/ISO/AVC',
            codec_private=video_header,
            lacing=False,
            default_duration_ns=round(float(ns) / float(frame_rate)),
            video=matroska.TrackVideo(720, 480,
                interlaced=True,
                display_width=round(720 * float(sar)),
                display_height=480,
                display_unit=matroska.DisplayUnit.PIXELS))

        audio_track = matroska.Track(
            number=2,
            uid=2,
            type_=matroska.TrackType.AUDIO,
            codec_id='A_AAC/MPEG4/MAIN',
            lacing=False,
            # Matroska codec specs LIED, the header is required
            codec_private=audio_encoder.get_header(),
            audio=matroska.TrackAudio(sample_rate, channels=2))

        writer.write_tracks([video_track, audio_track])

        writer.add_tag(matroska.Tag(
            [matroska.Target('TAPE', 50)],
            [matroska.SimpleTag('DESCRIPTION', 'Dad videotaping the van out in the snow.')]))

        # Time to actually code stuff!
        first_frame = True
        samples_per_block = 1024
        duration_timecode = 0

        try:
            # Parameters to write_simple_block: track, timecode, data, keyframe, discardable
            Packet = collections.namedtuple('Packet', 'track timecode decode_timecode data keyframe discardable duration_timecode')

            video_packet, audio_packet = None, None

            while True:
                if not video_packet:
                    packet = video_encoder.get_next_packet()

                    if packet:
                        data = packet.data

                        # Stick the SEI in the first frame
                        if first_frame:
                            data = video_encoder.sei + data
                            first_frame = False

                        video_packet = Packet(1,
                            matroska.timecode(packet.pts, frame_rate, timescale),
                            matroska.timecode(packet.dts, frame_rate, timescale),
                            data,
                            packet.keyframe,
                            packet.discardable,
                            matroska.timecode(packet.pts + 1, frame_rate, timescale))

                if not audio_packet:
                    packet = audio_encoder.get_next_packet()

                    if packet:
                        audio_packet = Packet(2,
                            matroska.timecode(packet.pts, sample_rate, timescale),
                            matroska.timecode(packet.dts, sample_rate, timescale),
                            packet.data,
                            packet.keyframe,
                            packet.discardable,
                            matroska.timecode(packet.pts + 1, sample_rate, timescale))

                next_packet = None

                if audio_packet:
                    if video_packet:
                        if audio_packet.decode_timecode < video_packet.decode_timecode:
                            next_packet = audio_packet
                            audio_packet = None
                        else:
                            next_packet = video_packet
                            video_packet = None
                    else:
                        next_packet = audio_packet
                        audio_packet = None
                elif video_packet:
                    next_packet = video_packet
                    video_packet = None
                else:
                    # We're done
                    break

                print('Writing track {0} packet {2} {1}'.format(next_packet.track, next_packet.timecode, next_packet.decode_timecode))

                # Write the block
                writer.write_simple_block(next_packet.track, next_packet.timecode,
                    next_packet.data, keyframe=next_packet.keyframe, discardable=next_packet.discardable)
                duration_timecode = next_packet.duration_timecode
        finally:
            print('Duration: ' + str(float(duration_timecode * timescale)/float(ns)))
            writer.write_end(duration=float(duration_timecode))

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

elif True:
    encoder = x264.X264VideoEncoder(mpeg2_subsample, 0, 1000, preset='veryfast')

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

else:
    dv_frame = dv_decoder.get_frame(970)
    mpeg2_frame = mpeg2_subsample.get_frame(970)

    with open('plane_luma_dv', mode='wb') as myfile:
        myfile.write(dv_frame[0].data)

    with open('plane_luma_mpeg2', mode='wb') as myfile:
        myfile.write(mpeg2_frame[0].data)

    with open('plane_cb_dv', mode='wb') as myfile:
        myfile.write(dv_frame[1].data)

    with open('plane_cb_mpeg2', mode='wb') as myfile:
        myfile.write(mpeg2_frame[1].data)

    with open('plane_cr_dv', mode='wb') as myfile:
        myfile.write(dv_frame[2].data)

    with open('plane_cr_mpeg2', mode='wb') as myfile:
        myfile.write(mpeg2_frame[2].data)

# Run "convert -size 720x480 -depth 8 r:plane0 plane0.png" on the resulting
# image to get something you can look at, since PIL doesn't work in Python 3

