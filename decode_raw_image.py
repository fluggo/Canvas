from fluggo.media import process
from PIL import Image
import sys

packet_source = process.FFDemuxer(sys.argv[1], 0)
dv_decoder = process.FFVideoDecoder(packet_source, 'dvvideo')

for i, plane in enumerate(dv_decoder.get_frame(0)):
    image = Image.frombuffer('L', (plane.stride, plane.line_count), plane.data, 'raw', 'L', 0, 1)
    image.save('plane' + str(i) + '.png', 'PNG')


