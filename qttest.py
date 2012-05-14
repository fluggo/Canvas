# Setup sip API usage (remove for Python 3)
import sip
sip.setapi('QDate', 2)
sip.setapi('QDateTime', 2)
sip.setapi('QTime', 2)
sip.setapi('QUrl', 2)
sip.setapi('QString', 2)
sip.setapi('QVariant', 2)

# Grab command-line arguments
import argparse

argparser = argparse.ArgumentParser()
argparser.add_argument('--log', dest='log_level',
                        choices=['debug', 'info', 'warning', 'error', 'critical'],
                        default='warning',
                        help='Logging level to use.')
argparser.add_argument('--break-exc', dest='break_exc', action='store_true', default=False,
                        help='Instructs PDB to break at all exceptions.')

args = argparser.parse_args()

# Set up logging
import logging
logging.basicConfig(level=args.log_level.upper())

# Set up pdb debugging if requested
if args.break_exc:
    import pdb, sys

    def excepthook(*args):
        pdb.set_trace()

    sys.excepthook = excepthook

# Identify ourselves to Qt
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from PyQt4.QtOpenGL import *

QCoreApplication.setOrganizationName('Fluggo Productions')
QCoreApplication.setOrganizationDomain('fluggo.com')
QCoreApplication.setApplicationName('Canvas')

from fluggo import signal, sortlist, logging
from fluggo.media import process, timecode, qt, alsa
from fluggo.media.basetypes import *
import sys, fractions, array, collections
import os.path
from fluggo.editor import ui, model, graph, plugins
from fluggo.editor.ui import notificationwidget
import fluggo.editor

_log = logging.getLogger(__name__)

# Load all plugins
plugins.PluginManager.load_all()

class SourceSearchModel(QAbstractTableModel):
    # TODO: Show if source is offline

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
        data.obj = fluggo.editor.DragDropSource(self.current_list[index.row()])

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
        self.setAcceptDrops(True)
        self._dropSources = None

    def set_source_list(self, source_list):
        self.source_list = source_list
        self.model = SourceSearchModel(source_list)
        self.view.setModel(self.model)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            if self._dropSources is None:
                sources = []

                for url in event.mimeData().urls():
                    _log.debug(u'Url:' + unicode(url))

                    if url.scheme() != 'file':
                        sources.append(url + u' is not a local file.')
                        continue

                    path = os.path.normpath(url.toLocalFile())

                    if not os.path.isfile(path):
                        sources.append(u"Can't find the file at \"" + path + u"\".")
                        continue

                    name = os.path.splitext(os.path.basename(path))[0]
                    source = None
                    error = None

                    _log.debug('Loading {0} as {1}', path, name)

                    for plugin in plugins.PluginManager.find_plugins(plugins.SourcePlugin):
                        _log.debug('Trying {0}', plugin.name)
                        try:
                            source = plugin.create_source_from_file(name, path)
                        except Exception as ex:
                            _log.debug(u'Error opening source {0}', path, exc_info=True)
                            error = unicode(ex)

                        _log.debug('Accepting {0}', plugin.name)

                        if source is not None:
                            break

                    sources.append(source or error)

                self._dropSources = sources

            if any(isinstance(source, plugins.Source) for source in self._dropSources):
                _log.debug('Accepting in drag enter')
                event.acceptProposedAction()
                return

    def dragLeaveEvent(self, event):
        _log.debug('Left drag')
        self._dropSources = None

    def dropEvent(self, event):
        _log.debug('In dropevent')

        if not self._dropSources:
            _log.debug('_dropSources not set')
            return

        if not any(isinstance(source, plugins.Source) for source in self._dropSources):
            _log.debug('Only errors')

            error_box = QMessageBox(QMessageBox.Warning,
                QCoreApplication.applicationName(),
                u'None of the dropped files could be opened.',
                buttons=QMessageBox.Ok, parent=self,
                detailedText='\n'.join(errors))

            error_box.exec_()

            self._dropSources = None
            return

        event.acceptProposedAction()

        # TODO: Handle dropping onto other sources, such as folders
        errors = []

        for source in self._dropSources:
            if not isinstance(source, plugins.Source):
                errors.append(source)
                continue

            try:
                base_name = source.name
                name = base_name
                i = 1

                while name in self.source_list:
                    name = u'{0} ({1})'.format(base_name, i)
                    i += 1

                source.name = name

                self.source_list[name] = model.PluginSource.from_plugin_source(source)
            except Exception as ex:
                errors.append(u'Error while importing "{0}": {1}'.format(source.name, unicode(ex)))
                _log.warning('Error creating PluginSource', exc_info=True)

        # TODO: Show errors
        if len(errors):
            error_box = QMessageBox(QMessageBox.Information,
                QCoreApplication.applicationName(),
                u'Some of the dropped files could not be opened.',
                buttons=QMessageBox.Ok, parent=self,
                detailedText='\n'.join(errors))

            error_box.exec_()

        self._dropSources = None
        return

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)
        self.setMinimumHeight(600)

        self.source_list = model.SourceList()

        self.audio_player = alsa.AlsaPlayer(48000, 2, None)

        self.alert_publisher = plugins.AlertPublisher()
        self.alert_publisher.follow_alerts(plugins.PluginManager.alert_manager)

        self.video_graph_manager = None

        # Set up canvas
        #self.clock = process.SystemPresentationClock()
        self.clock = self.audio_player

        # Workaround for Qt bug (see RulerView)
        #self.view = ui.canvas.View(self.clock)
        self.view = ui.canvas.RulerView(self.clock)

        #self.view.setViewport(QGLWidget())
        self.view.setBackgroundBrush(QBrush(QColor.fromRgbF(0.5, 0.5, 0.5)))

        format = QGLFormat()
        self.video_dock = QDockWidget('Video Preview', self)
        self.video_widget = qt.VideoWidget(format, self.video_dock)
        self.video_dock.setWidget(self.video_widget)

        self.video_widget.setRenderingIntent(1.5)
        self.video_widget.setPresentationClock(self.clock)

        self.addDockWidget(Qt.BottomDockWidgetArea, self.video_dock)

        self.search_dock = SourceSearchWidget(self.source_list)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.search_dock)

        self.notify_dock = notificationwidget.NotificationWidget(self.alert_publisher)
        self.tabifyDockWidget(self.search_dock, self.notify_dock)

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

        # Set up the defaults
        # Only one space for now, we'll do multiple later
        vidformat = plugins.VideoFormat(interlaced=True,
            full_frame=box2i(-8, -1, -8 + 720 - 1, -1 + 480 - 1),
            active_area=box2i(0, -1, 704 - 1, -1 + 480 - 1),
            pixel_aspect_ratio=fractions.Fraction(40, 33),
            white_point='D65',
            frame_rate=fractions.Fraction(24000, 1001))

        audformat = plugins.AudioFormat(sample_rate=48000,
            channel_assignment=('FrontLeft', 'FrontRight'))

        self.space = model.Space(u'', vidformat, audformat)
        self.setup_space()

        # FOR TESTING
        self.open_file('test_timeline.yaml')

    def setup_source_list(self):
        self.space = None
        self.setup_space()

        self.search_dock.set_source_list(self.source_list)

    def setup_space(self):
        '''Set up the work environment for the new value of self.space.'''
        if not self.space:
            self.audio_player.set_audio_source(None)
            self.video_widget.setVideoSource(None)
            self.view.set_space(None, self.source_list)
            return

        if self.video_graph_manager:
            self.alert_publisher.unfollow_alerts(self.video_graph_manager)

        self.audio_graph_manager = graph.SpaceAudioManager(self.space, self.source_list)
        self.audio_player.set_audio_source(self.audio_graph_manager)

        self.video_graph_manager = graph.SpaceVideoManager(self.space, self.source_list, self.space.video_format)
        self.video_graph_manager.frames_updated.connect(self.handle_update_frames)
        self.video_widget.setDisplayWindow(self.space.video_format.active_area)
        self.video_widget.setPixelAspectRatio(self.space.video_format.pixel_aspect_ratio)
        self.video_widget.setVideoSource(self.video_graph_manager)

        self.alert_publisher.follow_alerts(self.video_graph_manager)

        self.view.set_space(self.space, self.source_list)

        self.clock.seek(0)

    def create_actions(self):
        self.open_space_action = QAction('&Open...', self,
            statusTip='Open a Canvas file', triggered=self.open_space)
        self.save_space_action = QAction('&Save...', self,
            statusTip='Save a Canvas file', triggered=self.save_space)
        self.quit_action = QAction('&Quit', self, shortcut=QKeySequence.Quit,
            statusTip='Quit the application', triggered=self.close, menuRole=QAction.QuitRole)

        self.render_dv_action = QAction('&Render DV...', self,
            statusTip='Render the entire canvas to a DV video', triggered=self.render_dv)

        self.tools_edit_plugins = QAction('Edit &plugins...', self,
            statusTip='Enable, disable, or configure plugins', triggered=self.edit_plugins)
        self.tools_edit_decoders = QAction('Edit &decoders...', self,
            statusTip='Enable, disable, or prioritize decoders', triggered=self.edit_decoders)

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

        self.tools_menu = self.menuBar().addMenu('&Tools')
        self.tools_menu.addAction(self.tools_edit_plugins)
        self.tools_menu.addAction(self.tools_edit_decoders)

    def handle_update_frames(self, min_frame, max_frame):
        if not self.space:
            return

        # If the current frame was in this set, re-seek to it
        speed = self.clock.get_speed()

        if speed.numerator:
            return

        time = self.clock.get_presentation_time()
        frame = process.get_time_frame(self.space.video_format.frame_rate, time)

        if frame >= min_frame and frame <= max_frame:
            self.clock.seek(process.get_frame_time(self.space.video_format.frame_rate, int(frame)))

    def open_space(self):
        path = QFileDialog.getOpenFileName(self, "Open File", filter='YAML Files (*.yaml)')

        if path:
            self.open_file(path)

    def save_space(self):
        path = QFileDialog.getSaveFileName(self, "Save File", filter='YAML Files (*.yaml)')

        if path:
            self.save_file(path)

    def open_file(self, path):
        project = None

        with open(path) as stream:
            project = yaml.load(stream)
            project.fixup()

        self.source_list = project.sources
        self.setup_source_list()

        self.space = self.source_list['test']
        self.setup_space()

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

            from fluggo.media import libav

            right = max(item.x + item.width for item in self.space)

            # TODO: Put black at the bottom so that we always composite against it

            packet_source = libav.AVVideoEncoder(process.DVSubsampleFilter(workspace),
                'dvvideo', start_frame=0, end_frame=right - 1,
                frame_size=v2i(720, 480), sample_aspect_ratio=fractions.Fraction(33, 40),
                interlaced=True, top_field_first=False, frame_rate=fractions.Fraction(30000/1001))

            muxer = libav.AVMuxer(str(path), 'avi')
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
        for item in self.view.selected_model_items():
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
        for item in self.view.selected_model_items():
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

    def edit_plugins(self):
        try:
            from fluggo.editor.ui.plugineditor import PluginEditorDialog

            dialog = PluginEditorDialog()
            dialog.exec_()
        except:
            _log.warning('Error executing plugin editor dialog', exc_info=True)

    def edit_decoders(self):
        try:
            from fluggo.editor.ui.codeceditor import DecoderEditorDialog

            dialog = DecoderEditorDialog()
            dialog.exec_()
        except:
            _log.warning('Error executing codec editor dialog', exc_info=True)


app = QApplication(sys.argv)

window = MainWindow()
window.show()

quit(app.exec_())

