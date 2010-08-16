from fluggo.media import process, ffmpeg
from fluggo.media.basetypes import *
import fractions
import sys

packet_source = ffmpeg.FFDemuxer(sys.argv[1], 0)
coded_image = ffmpeg.FFVideoDecoder(packet_source, 'dvvideo')
packet_source = ffmpeg.FFVideoEncoder(coded_image, 'dvvideo', start_frame=0, end_frame=200, frame_size=v2i(720, 480),
    sample_aspect_ratio=fractions.Fraction(33, 40), interlaced=True, top_field_first=False, frame_rate=fractions.Fraction(30000/1001))

muxer = ffmpeg.FFMuxer('/home/james/software/fluggo-media/test_packet.avi', 'avi')
muxer.add_video_stream(packet_source, 'dvvideo', frame_rate=fractions.Fraction(30000, 1001),
    frame_size = v2i(720, 480), sample_aspect_ratio=fractions.Fraction(33, 40))
#muxer.run()

if False:
    #print len(packet_source.get_header())

    with open('/home/james/software/fluggo-media/test_packet.dv', 'w') as file_:
        while True:
            packet = packet_source.get_next_packet()

            if not packet:
                break

            file_.write(str(packet.data))
            #print packet
            #print packet.dts, packet.pts

from PyQt4.QtCore import *
from PyQt4.QtGui import *
from fluggo.editor.ui import renderprogress

app = QApplication(sys.argv)

window = renderprogress.RenderProgressDialog(muxer, [packet_source])
window.show()

quit(app.exec_())

