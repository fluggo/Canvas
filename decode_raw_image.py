import logging
handler = logging.StreamHandler()
handler.setLevel(logging.NOTSET)
handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

root_logger = logging.getLogger()
root_logger.setLevel(logging.NOTSET)
root_logger.addHandler(handler)

from fluggo.media import process, libav, x264
import sys

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

encoder = x264.X264VideoEncoder(mpeg2_subsample, 0, 10000, preset='veryfast')

with open('test.264', mode='wb') as myfile:
    myfile.write(encoder.get_header())
    packet = encoder.get_next_packet()

    while packet:
        print('Writing packet pts {0} dts {1}'.format(packet.pts, packet.dts))
        myfile.write(packet.data)
        packet = encoder.get_next_packet()

if False:
    dv_frame = dv_decoder.get_frame(1000)
    mpeg2_frame = mpeg2_subsample.get_frame(1000)

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

