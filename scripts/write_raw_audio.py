import logging
handler = logging.StreamHandler()
handler.setLevel(logging.INFO)
handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

root_logger = logging.getLogger()
root_logger.setLevel(logging.INFO)
root_logger.addHandler(handler)

from fluggo.media import process, libav, matroska
import sys
import datetime
import struct
import fractions
import collections
import math

process.enable_glib_logging(True)

audio_packet_source = libav.AVDemuxer(sys.argv[1], 1)
audio_decoder = libav.AVAudioDecoder(audio_packet_source, 'pcm_s16le', 2)

matroska.write_audio_pcm_float('test_pcm.mkv', audio_decoder, 0, 1000000,
    sample_rate=48000, channels=2)

