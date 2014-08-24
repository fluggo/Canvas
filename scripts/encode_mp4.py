import logging
handler = logging.StreamHandler()
handler.setLevel(logging.INFO)
handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

root_logger = logging.getLogger()
root_logger.setLevel(logging.INFO)
root_logger.addHandler(handler)

from fluggo.media import process, libav, x264, matroska, faac, libdv
from fluggo.media.basetypes import *
import sys
import datetime
import struct
import fractions
import collections
import math
import argparse
import os.path

parser = argparse.ArgumentParser()
parser.add_argument('in_path')
parser.add_argument('out_path', default=None, nargs='?')
parser.add_argument('--desc', dest='description_file')
parser.add_argument('-f', dest='force', default=False, action='store_true')
parser.add_argument('--crf', type=float, default=23.0)
parser.add_argument('--preset', dest='preset', default='slow')
parser.add_argument('--16x9', dest='wide', default=False, action='store_true')
parser.add_argument('--max-bitrate', dest='max_bitrate', type=int, default=None)
args = parser.parse_args()

if not args.out_path:
    args.out_path = args.in_path.replace('.dv', '-crf{0:g}-{1}.mp4'.format(args.crf, args.preset))

print(args.out_path)

if os.path.isfile(args.out_path) and not args.force:
    print('The output file already exists. Will not overwrite without -f.')
    exit()

description = None

if args.description_file:
    with open(args.description_file, 'r') as f:
        description = f.read()
elif args.in_path.endswith('.dv') and os.path.isfile(args.in_path.replace('.dv', '.label')):
    print('Using {0} for description'.format(args.in_path.replace('.dv', '.label')))

    with open(args.in_path.replace('.dv', '.label'), 'r') as f:
        description = f.read()

if description:
    print('Description:')
    print(description)

process.enable_glib_logging(True)

if not process.check_context_supported():
    print("Sorry, your drivers don't support the minimum")
    print("requirements for this library. Consider using a")
    print("software driver.")
    exit()

container = libav.AVContainer(args.in_path)
frame_count = container.streams[0].duration
sample_count = (container.streams[1].frame_count or container.streams[1].sample_rate * container.streams[1].duration * container.streams[1].time_base.numerator// container.streams[1].time_base.denominator)

print('Frames {0} Samples {1}'.format(frame_count, sample_count))

packet_source = libav.AVDemuxer(args.in_path, 0)
dv_decoder = libav.AVVideoDecoder(packet_source, 'dvvideo')
dv_reconstruct = process.DVReconstructionFilter(dv_decoder)
mpeg2_subsample = process.MPEG2SubsampleFilter(dv_reconstruct)

audio_packet_source = libav.AVDemuxer(args.in_path, 0)
audio_decoder = libdv.DVAudioDecoder(audio_packet_source)

frame_rate = fractions.Fraction(30000, 1001)
sample_rate = 48000
writing_app = "Brian's test MP4 writer"
sar = fractions.Fraction(40, 33) if args.wide else fractions.Fraction(10, 11)



#min_frame, max_frame = 0, frame_count - 1
min_frame, max_frame = 0, 180

min_sample = round(min_frame * sample_rate / float(frame_rate))
max_sample = round(max_frame * sample_rate / float(frame_rate))
#min_sample, max_sample = 0, sample_count - 1
print('min_sample: {0}, max_sample: {1}'.format(min_sample, max_sample))

params = x264.X264EncoderParams(preset=args.preset, width=720, height=480,
    frame_rate=frame_rate, constant_ratefactor=args.crf, sample_aspect_ratio=sar,
    annex_b=False, repeat_headers=False, interlaced=True, vbv_max_bitrate=(args.max_bitrate or -1))
video_encoder = x264.X264VideoEncoder(mpeg2_subsample, min_frame, max_frame, params)
audio_encoder = faac.AACAudioEncoder(audio_decoder, min_sample, max_sample, 48000, 2)

muxer = libav.AVMuxer(str(args.out_path), 'mp4')
muxer.add_video_stream(video_encoder, libav.CODEC_ID_H264, frame_rate=frame_rate,
    frame_size = v2i(720, 480), sample_aspect_ratio=sar, bit_rate=10000)
muxer.add_audio_stream(audio_encoder, libav.CODEC_ID_AAC, sample_rate=sample_rate, channels=2, bit_rate=10000)

muxer.run()



