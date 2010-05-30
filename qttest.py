
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtOpenGL import *
from fluggo.media import process, timecode, qt, formats
from fluggo.media.basetypes import *
import sys, fractions, array
from fluggo.editor.ui import canvas

from fluggo.media.muxers.ffmpeg import FFMuxPlugin

muxers = (FFMuxPlugin,)

def find_muxer(type_):
    for muxer in muxers:
        if type_ in muxer.supported_muxers:
            return muxer

    return None

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)

        # BJC: This is the initial test code; as soon as load/save support is in, this goes away
        path = '/home/james/Videos/Soft Boiled/Sources/softboiled01;17;55;12.avi'
        container = None

        for muxer in muxers:
            container = muxer.detect_container(path)

            if container:
                break

        stream_format = container.streams[0]
        stream_format.override[formats.VideoAttribute.PULLDOWN_TYPE] = formats.PULLDOWN_23
        stream_format.override[formats.VideoAttribute.PULLDOWN_PHASE] = 3

        source = find_muxer(container.muxer).get_stream(container, 0)

        workspace = process.Workspace()
        workspace_item = workspace.add(source=source, x=0, width=100, z=0, offset=0)

        # Set up canvas
        self.clock = process.SystemPresentationClock()

        self.view = canvas.View(self.clock)
        #self.view.setViewport(QGLWidget())
        self.view.setBackgroundBrush(QBrush(QColor.fromRgbF(0.5, 0.5, 0.5)))

        item = canvas.VideoItem(workspace_item, 'Clip')
        self.view.scene().addItem(item)
        item.setSelected(True)

        format = QGLFormat()
        self.video_dock = QDockWidget('Video Preview', self)
        self.video_widget = qt.VideoWidget(format, self.video_dock)
        self.video_dock.setWidget(self.video_widget)

        self.video_widget.setDisplayWindow(box2i(0, -1, 719, 478))

        self.video_widget.setRenderingIntent(1.5)
        self.video_widget.setPixelAspectRatio(640.0/704.0)
        self.video_widget.setPresentationClock(self.clock)
        self.video_widget.setVideoSource(workspace)

        self.clock.seek(0)

        self.addDockWidget(Qt.BottomDockWidgetArea, self.video_dock)

        # Set up UI
        self.create_actions()
        self.create_menus()

        center_widget = QWidget(self)
        layout = QVBoxLayout(center_widget)
        layout.addWidget(self.view)

        transport_toolbar = QToolBar(self)

        for action in self.transport_group.actions():
            transport_toolbar.addAction(action)

        layout.addWidget(transport_toolbar)
        layout.setSpacing(0)
        layout.setContentsMargins(0, 0, 0, 0)
        center_widget.setLayout(layout)

        self.setCentralWidget(center_widget)

    def create_actions(self):
        self.add_clip_action = QAction('&Add Clip...', self,
            statusTip='Add a new clip to the canvas', triggered=self.add_clip)
        self.quit_action = QAction('&Quit', self, shortcut=QKeySequence.Quit,
            statusTip='Quit the application', triggered=self.close, menuRole=QAction.QuitRole)

        self.view_video_preview = self.video_dock.toggleViewAction()
        self.view_video_preview.setText('Video &Preview')

        self.transport_group = QActionGroup(self)
        self.transport_rewind_action = QAction('Rewind', self.transport_group,
            statusTip='Play the current timeline backwards', triggered=self.transport_rewind,
            icon=self.style().standardIcon(QStyle.SP_MediaSeekBackward), checkable=True)
        self.transport_play_action = QAction('Play', self.transport_group,
            statusTip='Play the current timeline', triggered=self.transport_play,
            icon=self.style().standardIcon(QStyle.SP_MediaPlay), checkable=True)
        self.transport_pause_action = QAction('Pause', self.transport_group,
            statusTip='Pause the current timeline', triggered=self.transport_pause,
            icon=self.style().standardIcon(QStyle.SP_MediaPause), checked=True, checkable=True)
        self.transport_fastforward_action = QAction('Rewind', self.transport_group,
            statusTip='Play the current timeline at double speed', triggered=self.transport_fastforward,
            icon=self.style().standardIcon(QStyle.SP_MediaSeekForward), checkable=True)

    def create_menus(self):
        self.file_menu = self.menuBar().addMenu('&File')
        self.file_menu.addAction(self.add_clip_action)
        self.file_menu.addSeparator()
        self.file_menu.addAction(self.quit_action)

        self.view_menu = self.menuBar().addMenu('&View')
        self.view_menu.addAction(self.view_video_preview)

    def add_clip(self):
        file_name = str(QFileDialog.getOpenFileName(self, 'Add Clip'))

        videro = process.FFVideoSource(file_name)

        clip = VideoClip(videro, 300, fractions.Fraction(640, 704), box2i(0, -1, 719, 478))
        workspace_item = workspace.add(source=clip, x=0, width=100, z=0, offset=0)
        item = canvas.VideoItem(workspace_item, 'Clip')
        self.view.scene().addItem(item)

    def transport_play(self):
        self.clock.play(1)
        self.transport_play_action.setChecked(True)

    def transport_pause(self):
        self.clock.stop()
        self.transport_pause_action.setChecked(True)

    def transport_fastforward(self):
        self.clock.play(2)
        self.transport_fastforward_action.setChecked(True)

    def transport_rewind(self):
        self.clock.play(-2)
        self.transport_rewind_action.setChecked(True)

app = QApplication(sys.argv)

window = MainWindow()
window.show()

quit(app.exec_())

