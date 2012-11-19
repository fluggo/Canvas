import logging
handler = logging.StreamHandler()
handler.setLevel(logging.DEBUG)
handler.setFormatter(logging.Formatter('{levelname}:{name}:{msg}', style='{'))

root_logger = logging.getLogger()
root_logger.setLevel(logging.DEBUG)
root_logger.addHandler(handler)

from fluggo.media import process, libav, matroska
from PIL import Image
import sys

process.enable_glib_logging(True)

if not process.check_context_supported():
    print("Sorry, your drivers don't support the minimum")
    print("requirements for this library. Consider using a")
    print("software driver.")
    exit()

class FakeDVImageSource(process.CodedImageSource):
    def get_frame(self, frame):
        result = [process.CodedImage(bytearray(720*480), 720, 480), # Y
            process.CodedImage(b'\x80'*(180*480), 180, 480),    # Cb
            process.CodedImage(b'\x80'*(180*480), 180, 480)]    # Cr

        yp = result[0].data

        for y in range(480):
            for x in range(720):
                value = 190

                if x == 0 or x == 719 or y == 0 or y == 479:
                    value = 0

                yp[y*720 + x] = value

        return result

packet_source = libav.AVDemuxer(sys.argv[1], 0)
dv_decoder = libav.AVVideoDecoder(packet_source, 'dvvideo')
dv_reconstruct = process.DVReconstructionFilter(dv_decoder)
f = process.VideoGainOffsetFilter(dv_reconstruct, 1.0, 0.0)
mpeg2_subsample = process.MPEG2SubsampleFilter(f)

dv_frame = dv_decoder.get_frame(63986)
mpeg2_frame = mpeg2_subsample.get_frame(63986)

def save_plane(plane, filename):
    image = Image.frombuffer('L', (plane.stride, plane.line_count), plane.data, 'raw', 'L', 0, 1)
    image.save(filename, 'PNG')

frame = mpeg2_frame

save_plane(frame[0], 'plane_luma.png')
save_plane(frame[1], 'plane_cb.png')
save_plane(frame[2], 'plane_cr.png')

# Run "convert -size 720x480 -depth 8 r:plane0 plane0.png" on the resulting
# image to get something you can look at, since PIL doesn't work in Python 3

