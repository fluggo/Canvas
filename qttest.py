
from PyQt4 import QtGui, QtOpenGL
from fluggo.media import process, timelines, sources, transitions, qt
import sys

app = QtGui.QApplication(sys.argv)

videro = process.FFVideoSource('/home/james/Videos/Soft Boiled/Sources/softboiled01;03;21;24.avi')
pulldown = process.Pulldown23RemovalFilter(videro, 0);
#videro = process.VideoMixFilter(src_a=process.SolidColorVideoSource((1.0, 0.0, 0.0, 0.25), (1, 0, 718, 477)), src_b=process.SolidColorVideoSource((0.0, 1.0, 0.0, 0.75), (2, 1, 717, 476)), mix_b=process.LinearFrameFunc(a=1/300.0, b=0))
audio = process.FFAudioSource('/home/james/Videos/Soft Boiled/Sources/BWFF/1B_1.wav')
player = process.AlsaPlayer(48000, source=audio)
clock = player

format = QtOpenGL.QGLFormat()
widget = qt.VideoWidget(format)
widget.show()

widget.setDisplayWindow((0, -1, 719, 478))

widget.setPixelAspectRatio(640.0/704.0)
widget.setPresentationClock(clock)
widget.setVideoSource(pulldown)
widget.stop()

clock.play(1)
widget.play()

quit(app.exec_())
