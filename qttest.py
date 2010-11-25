
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtOpenGL import *
from fluggo import signal, sortlist
from fluggo.media import process, timecode, qt, formats, sources, alsa
from fluggo.media.basetypes import *
import sys, fractions, array, collections
from fluggo.editor import ui, model, graph

from fluggo.media.muxers.ffmpeg import FFMuxPlugin


class SourceSearchModel(QAbstractTableModel):
    def __init__(self, source_list):
        QAbstractTableModel.__init__(self)
        self.source_list = source_list
        self.current_list = []
        self.source_list.added.connect(self._item_added)
        self.source_list.removed.connect(self._item_removed)
        self.setSupportedDragActions(Qt.LinkAction)

        self.search_string = None
        self.search('')

    def _item_added(self, name):
        print 'Added ' + name
        if self._match(name):
            length = len(self.current_list)
            self.beginInsertRows(QModelIndex(), length, length)
            self.current_list.append(name)
            self.endInsertRows()

    def _item_removed(self, name):
        print 'Removed ' + name
        if self._match(name):
            index = self.current_list.index(name)
            self.beginRemoveRows(QModelIndex(), index, index)
            del self.current_list[index]
            self.endRemoveRows()

    def _match(self, name):
        return self.search_string in name.lower()

    def search(self, search_string):
        self.search_string = search_string.lower()

        self.beginResetModel()
        self.current_list = [name for name in self.source_list.iterkeys() if self._match(name)]
        self.endResetModel()

    def data(self, index, role=Qt.DisplayRole):
        if role == Qt.DisplayRole:
            if index.column() == 0:
                return self.current_list[index.row()]

    def mimeData(self, indexes):
        index = indexes[0]
        data = QMimeData()
        data.source_name = self.current_list[index.row()]

        return data

    def flags(self, index):
        return Qt.ItemIsSelectable | Qt.ItemIsEnabled | Qt.ItemIsDragEnabled

    def rowCount(self, parent):
        if parent.isValid():
            return 0

        return len(self.current_list)

    def columnCount(self, parent):
        if parent.isValid():
            return 0

        return 1

class SourceSearchWidget(QDockWidget):
    def __init__(self, source_list):
        QDockWidget.__init__(self, 'Sources')
        self.source_list = source_list
        self.model = SourceSearchModel(source_list)

        widget = QWidget()

        self.view = QListView(self)
        self.view.setModel(self.model)
        self.view.setDragEnabled(True)

        layout = QVBoxLayout(widget)
        layout.addWidget(self.view)

        self.setWidget(widget)

muxers = (FFMuxPlugin,)

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)

        self.source_list = sources.SourceList(muxers)

        # Only one space for now, we'll do multiple later
        vidformat = formats.StreamFormat('video')
        vidformat.override[formats.VideoAttribute.SAMPLE_ASPECT_RATIO] = fractions.Fraction(40, 33)
        vidformat.override[formats.VideoAttribute.FRAME_RATE] = fractions.Fraction(24000, 1001)
        vidformat.override[formats.VideoAttribute.MAX_DATA_WINDOW] = box2i((0, -1), (719, 478))
        audformat = formats.StreamFormat('audio')
        audformat.override[formats.AudioAttribute.SAMPLE_RATE] = 48000
        audformat.override[formats.AudioAttribute.CHANNELS] = ['FL', 'FR']

        self.space = model.Space(vidformat, audformat)
        #self.space.append(clip)

        self.audio_graph_manager = graph.SpaceAudioManager(self.space, self.source_list)
        self.audio_player = alsa.AlsaPlayer(48000, 2, self.audio_graph_manager)

        self.video_graph_manager = graph.SpaceVideoManager(self.space, self.source_list, vidformat)
        self.video_graph_manager.frames_updated.connect(self.handle_update_frames)

        # Set up canvas
        #self.clock = process.SystemPresentationClock()
        self.clock = self.audio_player
        self.frame_rate = fractions.Fraction(24000, 1001)

        self.view = ui.canvas.View(self.clock, self.space, self.source_list)
        #self.view.setViewport(QGLWidget())
        self.view.setBackgroundBrush(QBrush(QColor.fromRgbF(0.5, 0.5, 0.5)))

        format = QGLFormat()
        self.video_dock = QDockWidget('Video Preview', self)
        self.video_widget = qt.VideoWidget(format, self.video_dock)
        self.video_dock.setWidget(self.video_widget)

        self.video_widget.setRenderingIntent(1.5)
        self.video_widget.setDisplayWindow(self.space.video_format.max_data_window)
        self.video_widget.setPixelAspectRatio(self.space.video_format.pixel_aspect_ratio)
        self.video_widget.setPresentationClock(self.clock)
        self.video_widget.setVideoSource(self.video_graph_manager)

        self.clock.seek(0)

        self.addDockWidget(Qt.BottomDockWidgetArea, self.video_dock)

        self.search_dock = SourceSearchWidget(self.source_list)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.search_dock)

        # Set up UI
        self.create_actions()
        self.create_menus()

        center_widget = QWidget(self)
        layout = QVBoxLayout(center_widget)

        top_toolbar = QToolBar(self)

        for action in self.canvas_group.actions():
            top_toolbar.addAction(action)

        layout.addWidget(top_toolbar)
        layout.addWidget(self.view)

        transport_toolbar = QToolBar(self)

        for action in self.transport_group.actions():
            transport_toolbar.addAction(action)

        layout.addWidget(transport_toolbar)
        layout.setSpacing(0)
        layout.setContentsMargins(0, 0, 0, 0)
        center_widget.setLayout(layout)

        self.setCentralWidget(center_widget)

        # FOR TESTING
        #self.open_file('test.yaml')

    def create_actions(self):
        self.open_space_action = QAction('&Open...', self,
            statusTip='Open a Canvas file', triggered=self.open_space)
        self.save_space_action = QAction('&Save...', self,
            statusTip='Save a Canvas file', triggered=self.save_space)
        self.quit_action = QAction('&Quit', self, shortcut=QKeySequence.Quit,
            statusTip='Quit the application', triggered=self.close, menuRole=QAction.QuitRole)

        self.render_dv_action = QAction('&Render DV...', self,
            statusTip='Render the entire canvas to a DV video', triggered=self.render_dv)

        self.view_video_preview = self.video_dock.toggleViewAction()
        self.view_video_preview.setText('Video &Preview')
        self.view_source_list = self.search_dock.toggleViewAction()
        self.view_source_list.setText('&Sources')

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

        self.canvas_group = QActionGroup(self)
        self.canvas_bring_forward_action = QAction('Bring Forward', self.canvas_group,
            statusTip='Bring the current item(s) forward', triggered=self.canvas_bring_forward,
            icon=self.style().standardIcon(QStyle.SP_ArrowUp))
        self.canvas_send_backward_action = QAction('Send Backward', self.canvas_group,
            statusTip='Bring the current item(s) forward', triggered=self.canvas_send_backward,
            icon=self.style().standardIcon(QStyle.SP_ArrowDown))

    def create_menus(self):
        self.file_menu = self.menuBar().addMenu('&File')
        self.file_menu.addAction(self.open_space_action)
        self.file_menu.addAction(self.save_space_action)
        self.file_menu.addAction(self.render_dv_action)
        self.file_menu.addSeparator()
        self.file_menu.addAction(self.quit_action)

        self.view_menu = self.menuBar().addMenu('&View')
        self.view_menu.addAction(self.view_source_list)
        self.view_menu.addAction(self.view_video_preview)

    def handle_update_frames(self, min_frame, max_frame):
        # If the current frame was in this set, re-seek to it
        speed = self.clock.get_speed()

        if speed.numerator:
            return

        time = self.clock.get_presentation_time()
        frame = process.get_time_frame(self.frame_rate, time)

        # FIXME: There's a race condition here where we might request
        # a repaint multiple times, but get one of the states in the middle,
        # not the final state
        if frame >= min_frame and frame <= max_frame:
            self.clock.seek(process.get_frame_time(self.frame_rate, int(frame)))

    def open_space(self):
        path = QFileDialog.getOpenFileName(self, "Open File", filter='YAML Files (*.yaml)')

        if path:
            self.open_file(path)

    def save_space(self):
        path = QFileDialog.getSaveFileName(self, "Save File", filter='YAML Files (*.yaml)')

        if path:
            self.save_file(path)

    def open_file(self, path):
        sources, space = None, None

        with open(path) as stream:
            (sources, space) = yaml.load_all(stream)

        self.space[:] = []

        self.source_list.clear()
        self.source_list.update(sources)

        space.fixup()

        # TODO: Replace the whole space here; this requires
        # swapping it out for the view and workspace managers also
        self.space[:] = space[:]
        self.video_widget.setDisplayWindow(self.space.video_format.max_data_window)
        self.video_widget.setPixelAspectRatio(self.space.video_format.pixel_aspect_ratio)

    def save_file(self, path):
        with open(path, 'w') as stream:
            yaml.dump_all((self.source_list.get_source_list(), self.space), stream)

    def render_dv(self):
        if not len(self.space):
            return

        path = QFileDialog.getSaveFileName(self, "Render DV", filter='AVI Files (*.avi)')

        if path:
            # Create a private workspace for this render
            # TODO: Changes to *filters* in the original workspace may alter this one, too
            # Be sure to check for that before making this process asynchronous
            workspace = process.VideoWorkspace()
            items = sorted(self.space, key=lambda a: a.z_sort_key())

            for i, item in enumerate(items):
                source = self.source_list.get_stream(item.source_name, item.source_stream_id)
                workspace.add(x=item.x, width=item.width, z=i, offset=item.offset, source=source)

            from fluggo.media import ffmpeg

            right = max(item.x + item.width for item in self.space)

            # TODO: Put black at the bottom so that we always composite against it

            packet_source = ffmpeg.FFVideoEncoder(process.DVSubsampleFilter(workspace),
                'dvvideo', start_frame=0, end_frame=right - 1,
                frame_size=v2i(720, 480), sample_aspect_ratio=fractions.Fraction(33, 40),
                interlaced=True, top_field_first=False, frame_rate=fractions.Fraction(30000/1001))

            muxer = ffmpeg.FFMuxer(str(path), 'avi')
            muxer.add_video_stream(packet_source, 'dvvideo', frame_rate=fractions.Fraction(30000, 1001),
                frame_size = v2i(720, 480), sample_aspect_ratio=fractions.Fraction(33, 40))

            from fluggo.editor.ui.renderprogress import RenderProgressDialog

            dialog = RenderProgressDialog(muxer, [packet_source])
            dialog.exec_()

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

    def canvas_bring_forward(self):
        for item in self.view.selected_items():
            key = item.z
            overlaps = item.overlap_items()
            above_items = [x.z for x in overlaps if x.z < key]

            if not above_items:
                continue

            bottom_z = max(above_items)

            z1 = bottom_z
            z2 = item.z

            temp_items = self.space[z1:z2 + 1]
            temp_items.insert(0, temp_items.pop())

            self.space[z1:z2 + 1] = temp_items

    def canvas_send_backward(self):
        for item in self.view.selected_items():
            key = item.z
            overlaps = item.overlap_items()
            below_items = [x.z for x in overlaps if x.z > key]

            if not below_items:
                continue

            top_z = min(below_items)

            z1 = item.z
            z2 = top_z

            temp_items = self.space[z1:z2 + 1]
            temp_items.append(temp_items.pop(0))

            self.space[z1:z2 + 1] = temp_items

app = QApplication(sys.argv)

window = MainWindow()
window.show()

quit(app.exec_())

