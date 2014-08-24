from fluggo.media import process
from fluggo.media.basetypes import *

videro = process.FFVideoSource('/home/james/Videos/NSC - Green Screen Pre Long 2.avi')

pulldown = process.Pulldown23RemovalFilter(videro, 0)

from fluggo.media import qt
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtOpenGL import *
import sys

app = QApplication(sys.argv)

clock = process.SystemPresentationClock()

format = QGLFormat()
video_widget = qt.VideoWidget(format)

video_widget.setDisplayWindow(box2i(0, -1, 719, 478))
video_widget.setPixelAspectRatio(640.0/704.0*4.0/3.0)
video_widget.setPresentationClock(clock)
video_widget.setVideoSource(pulldown)

video_widget.show()

clock.seek(10*1000000000)

quit(app.exec_())

