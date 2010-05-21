
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtOpenGL import *
from fluggo.media import process, timecode, qt
from fluggo.media.basetypes import *
import sys, fractions, array
from fluggo.editor.ui import canvas

class VideoClip(process.VideoPassThroughFilter):
    def __init__(self, source, length, pixel_aspect_ratio, thumbnail_box):
        process.VideoPassThroughFilter.__init__(self, source)
        self.thumbnail_box = thumbnail_box
        self.pixel_aspect_ratio = pixel_aspect_ratio
        self.length = length

videro = process.FFVideoSource('/home/james/Videos/Soft Boiled/Sources/softboiled01;17;55;12.avi')
pulldown = process.Pulldown23RemovalFilter(videro, 0);

clip = VideoClip(pulldown, 300, fractions.Fraction(640, 704), box2i(0, -1, 719, 478))

#videro = process.FFVideoSource('/home/james/Videos/tape-1999-uil-football-fame.dv')
#clip = VideoClip(videro, 300, fractions.Fraction(640, 704), box2i(0, -1, 719, 478))

red = process.SolidColorVideoSource(rgba(1.0, 0.0, 0.0, 0.25), box2i(20, 20, 318, 277))
green = process.SolidColorVideoSource(rgba(0.0, 1.0, 0.0, 0.75), box2i(200, 200, 518, 477))

frame = videro.get_frame_f32(0, box2i(200, 100, 519, 278))

workspace = process.Workspace()
workspace_item = workspace.add(source=clip, x=0, width=100, z=0, offset=0)
workspace.add(source=red, x=50, width=100, z=1)
workspace.add(source=green, x=75, width=100, z=2)
workspace.add(source=frame, x=125, width=100, z=0, offset=500)

#videro = process.VideoMixFilter(src_a=process.SolidColorVideoSource((1.0, 0.0, 0.0, 0.25), (1, 0, 718, 477)), src_b=process.SolidColorVideoSource((0.0, 1.0, 0.0, 0.75), (2, 1, 717, 476)), mix_b=process.LinearFrameFunc(a=1/300.0, b=0))
audio = process.FFAudioSource('/home/james/Videos/Soft Boiled/Sources/BWFF/1B_1.wav')
player = process.AlsaPlayer(48000, source=audio)
clock = player

NS_PER_SEC = fractions.Fraction(1000000000L, 1)

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)

        self.view = canvas.View(clock)
        #self.view.setViewport(QGLWidget())
        self.view.setBackgroundBrush(QBrush(QColor.fromRgbF(0.5, 0.5, 0.5)))
        self.setCentralWidget(self.view)

        item = canvas.VideoItem(workspace_item, 'Clip')
        self.view.scene().addItem(item)
        item.setSelected(True)

        format = QGLFormat()
        self.video_widget = qt.VideoWidget(format)
        self.video_dock = QDockWidget('Preview')
        self.video_dock.setWidget(self.video_widget)

        self.video_widget.setDisplayWindow(box2i(0, -1, 719, 478))

        self.video_widget.setRenderingIntent(1.5)
        self.video_widget.setPixelAspectRatio(640.0/704.0)
        self.video_widget.setPresentationClock(clock)
        self.video_widget.setVideoSource(workspace)

        clock.seek(0)

        self.addDockWidget(Qt.BottomDockWidgetArea, self.video_dock)

app = QApplication(sys.argv)

window = MainWindow()
window.show()

quit(app.exec_())

