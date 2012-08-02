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

class AssetSearchModel(QAbstractTableModel):
    # TODO: Show if asset is offline

    def __init__(self, asset_list):
        QAbstractTableModel.__init__(self)
        self.asset_list = asset_list
        self.current_list = []
        self.asset_list.added.connect(self._item_added)
        self.asset_list.removed.connect(self._item_removed)
        self.setSupportedDragActions(Qt.LinkAction)

        self.search_string = None
        self.search('')

    def _item_added(self, name):
        print('Added ' + name)
        if self._match(name):
            length = len(self.current_list)
            self.beginInsertRows(QModelIndex(), length, length)
            self.current_list.append(name)
            self.endInsertRows()

    def _item_removed(self, name):
        print('Removed ' + name)
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
        self.current_list = [name for name in self.asset_list.keys() if self._match(name)]
        self.endResetModel()

    def data(self, index, role=Qt.DisplayRole):
        if role == Qt.DisplayRole:
            if index.column() == 0:
                return self.current_list[index.row()]

    def mimeData(self, indexes):
        index = indexes[0]
        data = QMimeData()
        data.obj = fluggo.editor.DragDropAsset(self.asset_list[self.current_list[index.row()]])

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

class AssetSearchWidget(QDockWidget):
    def __init__(self, uimgr):
        QDockWidget.__init__(self, 'Assets')
        widget = QWidget()

        self.uimgr = uimgr

        self.view = QListView(self)
        self.view.setDragEnabled(True)

        layout = QVBoxLayout(widget)
        layout.addWidget(self.view)

        self.setWidget(widget)
        self.setAcceptDrops(True)
        self._dropAssets = None

        self.update_asset_list()
        uimgr.asset_list_changed.connect(self.update_asset_list)

    def update_asset_list(self):
        self.asset_list = self.uimgr.asset_list
        self.model = AssetSearchModel(self.uimgr.asset_list)
        self.view.setModel(self.model)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            if self._dropAssets is None:
                sources = []

                for url in event.mimeData().urls():
                    _log.debug('Url:' + str(url))

                    if url.scheme() != 'file':
                        sources.append(url + ' is not a local file.')
                        continue

                    path = os.path.normpath(url.toLocalFile())

                    if not os.path.isfile(path):
                        sources.append("Can't find the file at \"" + path + "\".")
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
                            _log.debug('Error opening source {0}', path, exc_info=True)
                            error = str(ex)

                        _log.debug('Accepting {0}', plugin.name)

                        if source is not None:
                            break

                    sources.append(source or error)

                self._dropAssets = sources

            if any(isinstance(source, plugins.Source) for source in self._dropAssets):
                _log.debug('Accepting in drag enter')
                event.acceptProposedAction()
                return

    def dragLeaveEvent(self, event):
        _log.debug('Left drag')
        self._dropAssets = None

    def dropEvent(self, event):
        _log.debug('In dropevent')

        if not self._dropAssets:
            _log.warning('_dropAssets not set')
            return

        if not any(isinstance(source, plugins.Source) for source in self._dropAssets):
            _log.debug('Only errors')

            error_box = QMessageBox(QMessageBox.Warning,
                QCoreApplication.applicationName(),
                'None of the dropped files could be opened.',
                buttons=QMessageBox.Ok, parent=self,
                detailedText='\n'.join(errors))

            error_box.exec_()

            self._dropAssets = None
            return

        event.acceptProposedAction()

        # TODO: Handle dropping onto other sources, such as folders
        errors = []

        for source in self._dropAssets:
            if not isinstance(source, plugins.Source):
                errors.append(source)
                continue

            try:
                base_name = source.name
                name = base_name
                i = 1

                while name in self.asset_list:
                    name = '{0} ({1})'.format(base_name, i)
                    i += 1

                source.name = name

                self.asset_list[name] = model.PluginSource.from_plugin_source(source)
            except Exception as ex:
                errors.append('Error while importing "{0}": {1}'.format(source.name, str(ex)))
                _log.warning('Error creating PluginSource', exc_info=True)

        # TODO: Show errors
        if len(errors):
            error_box = QMessageBox(QMessageBox.Information,
                QCoreApplication.applicationName(),
                'Some of the dropped files could not be opened.',
                buttons=QMessageBox.Ok, parent=self,
                detailedText='\n'.join(errors))

            error_box.exec_()

        self._dropAssets = None
        return

class UndoDockWidget(QDockWidget):
    def __init__(self, uimgr):
        QDockWidget.__init__(self, 'Undo')
        self._undo_group = uimgr.undo_group
        self._undo_widget = QUndoView(self._undo_group, self)

        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.addWidget(self._undo_widget)

        self.setWidget(widget)

class UIManager:
    '''
    Provides top-level services to the rest of the UI.

    What this means will probably expand in the future, but for now it means
    providing access to the clock (which includes controlling playback), the
    project and asset list, the current composition, and the top-level undo stack.
    '''

    def __init__(self):
        self._clock = None
        self.clock_state_changed = signal.Signal()
        self._clock_callback_handle = None
        self.set_clock(None)

        self._asset_list = None
        self.asset_list_changed = signal.Signal()

        self._undo_group = QUndoGroup()
        self._project_undo_stack = QUndoStack(self._undo_group)

        self._editors = []
        self.editor_added = signal.Signal()
        self.editor_removed = signal.Signal()

        self._current_editor = None
        self.current_editor_changed = signal.Signal()

    @property
    def asset_list(self):
        return self._asset_list

    def set_asset_list(self, asset_list):
        self._asset_list = asset_list
        self.asset_list_changed()

    def add_editor(self, editor):
        if editor in self._editors:
            _log.warning('Trying to add editor already in UIManager')
            return

        undo_stack = editor.get_undo_stack()
        self.undo_group.addStack(undo_stack)
        self._editors.append(editor)
        self.editor_added(editor)

    def remove_editor(self, editor):
        if editor not in self._editors:
            _log.warning('Trying to remove editor not in UIManager')
            return

        if editor == self._current_editor:
            self.set_current_editor(None)

        undo_stack = editor.get_undo_stack()
        self.undo_group.removeStack(undo_stack)
        self._editors.remove(editor)
        self.editor_removed(editor)

    @property
    def current_editor(self):
        return self._current_editor

    def set_current_editor(self, editor):
        # TODO: Add this comp to the undo group somehow
        # For now, we cheat here by adding only one other item to the undo group
        # TODO: Really in the near future we should have the UIManager aware of
        # all
        if editor == self._current_editor:
            return

        if editor not in self._editors:
            _log.warning('Trying to set a current editor not in UIManager')
            return

        undo_stack = editor.get_undo_stack()
        self.undo_group.setActiveStack(undo_stack)

        self._current_editor = editor
        self.current_editor_changed()

    @property
    def undo_group(self):
        return self._undo_group

    @property
    def project_undo_stack(self):
        return self._project_undo_stack

    def set_clock(self, clock):
        # A warning: clock and clock_callback_handle will create a pointer cycle here,
        # which probably won't be freed unless someone calls UIManager.set_clock(None)
        if self._clock:
            self._clock.stop()

        if self._clock_callback_handle:
            self._clock_callback_handle.unregister()
            self._clock_callback_handle = None

        if not clock:
            clock = process.SystemPresentationClock()

        self._clock = clock

        if self._clock:
            self._clock_callback_handle = self._clock.register_callback(self.clock_state_changed, None)

        # TODO: To be thorough, call clock_changed with the new speed and time
        # (or seek the new clock to the old clock's time)

    def seek(self, time_ns):
        return self._clock.seek(time_ns)

    def get_presentation_time(self):
        return self._clock.get_presentation_time()

    def get_playback_speed(self):
        return self._clock.get_speed()

    def play(self, speed):
        return self._clock.play(speed)

    def stop(self):
        return self._clock.stop()

class CompositionEditor:
    @property
    def name(self):
        '''Return the name of the composition.'''
        raise NotImplementedError

    @property
    def asset(self):
        '''Returns the asset being edited.'''
        return None

    def get_source(self):
        '''Return the source being edited, if any. This is used to provide the
        video and audio previews.'''
        raise NotImplementedError

    def get_undo_stack(self):
        '''Return the QUndoStack for this editor, or None if it doesn't have one.'''
        return None

    def get_widget(self):
        '''Return the widget to display in the main area of the editor.'''
        raise NotImplementedError

    def get_toolbars(self):
        '''Return a list of toolbars provided by the composition editor.

        Multiple composition editors are expected to return the same toolbars (that
        is, the same instances). If the toolbar's object is named (the toolbar's
        objectName property is not an empty string), the UIManager may make an effort
        to preserve the toolbar's placement when running the application again.'''
        return []

class SpaceEditor(CompositionEditor):
    def __init__(self, uimgr, space_asset):
        self.uimgr = uimgr
        self._asset = space_asset
        self.view = None

    @property
    def name(self):
        return self.asset.name

    @property
    def asset(self):
        return self._asset

    def get_source(self):
        return self.asset.get_source()

    def _create_widget(self):
        if not self.view:
            # Workaround for Qt bug (see RulerView)
            #self.view = ui.canvas.View(self.clock)
            self.view = ui.canvas.RulerView(self.uimgr, self.asset.space)

            #self.view.setViewport(QGLWidget())
            self.view.setBackgroundBrush(QBrush(QColor.fromRgbF(0.5, 0.5, 0.5)))

    def get_widget(self):
        self._create_widget()
        return self.view

    def get_undo_stack(self):
        self._create_widget()
        return self.view.undo_stack

    def get_toolbars(self):
        self._create_widget()
        return self.view.get_toolbars()

class MainWindow(QMainWindow):
    def __init__(self):
        QMainWindow.__init__(self)
        self.setMinimumHeight(600)

        self.audio_player = alsa.AlsaPlayer(48000, 2, None)

        self.alert_publisher = plugins.AlertPublisher()
        self.alert_publisher.follow_alerts(plugins.PluginManager.alert_manager)

        # Set up canvas
        #self.clock = process.SystemPresentationClock()
        self.uimgr = UIManager()
        self.uimgr.set_clock(self.audio_player)
        self.uimgr.set_asset_list(model.AssetList())
        self.uimgr.editor_added.connect(self.handle_editor_added)
        self.uimgr.current_editor_changed.connect(self.handle_editor_changed)
        self.uimgr.editor_removed.connect(self.handle_editor_removed)

        # TODO: Go away when multiple compositions arrive
        self.view = None

        self.video_stream = None
        self.audio_stream = None
        self.current_source = None

        format = QGLFormat()
        self.video_dock = QDockWidget('Video Preview', self)
        self.video_widget = qt.VideoWidget(format, self.video_dock)
        self.video_dock.setWidget(self.video_widget)

        self.video_widget.setRenderingIntent(1.5)
        self.video_widget.setPresentationClock(self.audio_player)

        self.addDockWidget(Qt.BottomDockWidgetArea, self.video_dock)

        self.search_dock = AssetSearchWidget(self.uimgr)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.search_dock)

        self.notify_dock = notificationwidget.NotificationWidget(self.alert_publisher)
        self.tabifyDockWidget(self.search_dock, self.notify_dock)

        self.undo_dock = UndoDockWidget(self.uimgr)
        self.tabifyDockWidget(self.search_dock, self.undo_dock)

        # Set up UI
        self.document_tabs = QTabWidget(self,
            tabsClosable=True, documentMode=True, elideMode=Qt.ElideRight)
        self.document_tabs.currentChanged.connect(self.handle_current_tab_changed)

        self.create_actions()
        self.create_menus()

        transport_toolbar = QToolBar(self)

        for action in self.transport_group.actions():
            transport_toolbar.addAction(action)

        self.addToolBar(Qt.TopToolBarArea, transport_toolbar)

        self.setCentralWidget(self.document_tabs)

        if False:
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

            self.space_asset = model.Space('', vidformat, audformat)

        self.source_alert = None

        self._seen_toolbars = set()
        self._current_toolbars = set()
        self._visible_toolbars = set()

        # FOR TESTING
        self.open_file('test_timeline.yaml')

    def create_actions(self):
        self.open_space_action = QAction('&Open...', self,
            statusTip='Open a Canvas file', triggered=self.open_space)
        self.save_space_action = QAction('&Save...', self,
            statusTip='Save a Canvas file', triggered=self.save_space)
        self.quit_action = QAction('&Quit', self, shortcut=QKeySequence.Quit,
            statusTip='Quit the application', triggered=self.close, menuRole=QAction.QuitRole)

        self.undo_action = self.uimgr.undo_group.createUndoAction(self)
        self.redo_action = self.uimgr.undo_group.createRedoAction(self)

        self.render_dv_action = QAction('&Render DV...', self,
            statusTip='Render the entire canvas to a DV video', triggered=self.render_dv)

        self.tools_edit_plugins = QAction('Edit &plugins...', self,
            statusTip='Enable, disable, or configure plugins', triggered=self.edit_plugins)
        self.tools_edit_decoders = QAction('Edit &decoders...', self,
            statusTip='Enable, disable, or prioritize decoders', triggered=self.edit_decoders)

        self.view_video_preview = self.video_dock.toggleViewAction()
        self.view_video_preview.setText('Video &Preview')
        self.view_asset_list = self.search_dock.toggleViewAction()
        self.view_asset_list.setText('&Assets')

        self.transport_group = QActionGroup(self)
        self.transport_rewind_action = QAction('Rewind', self.transport_group,
            statusTip='Play the current timeline backwards', triggered=self.transport_rewind,
            icon=self.style().standardIcon(QStyle.SP_MediaSeekBackward), checkable=True,
            shortcutContext=Qt.ApplicationShortcut)
        self.transport_play_action = QAction('Play', self.transport_group,
            statusTip='Play the current timeline', triggered=self.transport_play,
            icon=self.style().standardIcon(QStyle.SP_MediaPlay), checkable=True,
            shortcutContext=Qt.ApplicationShortcut)
        self.transport_pause_action = QAction('Pause', self.transport_group,
            statusTip='Pause the current timeline', triggered=self.transport_pause,
            icon=self.style().standardIcon(QStyle.SP_MediaPause), checked=True, checkable=True,
            shortcutContext=Qt.ApplicationShortcut)
        self.transport_fastforward_action = QAction('Rewind', self.transport_group,
            statusTip='Play the current timeline at double speed', triggered=self.transport_fastforward,
            icon=self.style().standardIcon(QStyle.SP_MediaSeekForward), checkable=True,
            shortcutContext=Qt.ApplicationShortcut)

    def create_menus(self):
        self.file_menu = self.menuBar().addMenu('&File')
        self.file_menu.addAction(self.open_space_action)
        self.file_menu.addAction(self.save_space_action)
        self.file_menu.addAction(self.render_dv_action)
        self.file_menu.addSeparator()
        self.file_menu.addAction(self.quit_action)

        self.edit_menu = self.menuBar().addMenu('&Edit')
        self.edit_menu.addAction(self.undo_action)
        self.edit_menu.addAction(self.redo_action)

        self.view_menu = self.menuBar().addMenu('&View')
        self.view_menu.addAction(self.view_asset_list)
        self.view_menu.addAction(self.view_video_preview)

        self.tools_menu = self.menuBar().addMenu('&Tools')
        self.tools_menu.addAction(self.tools_edit_plugins)
        self.tools_menu.addAction(self.tools_edit_decoders)

    def handle_editor_added(self, editor):
        widget = editor.get_widget()
        self.document_tabs.addTab(widget, editor.name)

    def handle_editor_removed(self, editor):
        i = self.document_tabs.indexOf(editor.get_widget())

        if i != -1:
            self.document_tabs.removeTab(i)

    def handle_current_tab_changed(self, index):
        # GAK
        pass

    def handle_editor_changed(self):
        if self.source_alert:
            self.alert_publisher.hide_alert(self.source_alert)
            self.source_alert = None

        # TODO: Maybe save off which items we were looking at in
        # the video preview before
        if self.video_stream:
            self.video_stream.frames_updated.disconnect(self.handle_update_frames)
            self.alert_publisher.unfollow_alerts(self.video_stream)
            self.video_widget.setVideoSource(None)
            self.video_stream = None

        if self.audio_stream:
            self.alert_publisher.unfollow_alerts(self.audio_stream)
            self.audio_player.set_audio_source(None)
            self.audio_stream = None

        if self.current_source:
            self.alert_publisher.unfollow_alerts(self.current_source)
            self.current_source = None

        if not self.uimgr.current_editor:
            return

        # Show toolbars
        # Once upon a time, I considered caching these. I don't do that now because
        # each editor creates its own copy of each action. I do want those to be
        # garbage-collected. I would love to figure out a good caching scheme,
        # but I think that will probably best come from the editors themselves.
        new_toolbars = set(self.uimgr.current_editor.get_toolbars())

        for toolbar in (self._current_toolbars - new_toolbars):
            toolbar.removeToolBar(toolbar)
            self._current_toolbars.remove(toolbar)

        for toolbar in (new_toolbars - self._current_toolbars):
            # TODO: Remember positions, if that's possible
            if toolbar.objectName() != '' and toolbar.objectName() not in self._seen_toolbars:
                self._seen_toolbars.add(toolbar.objectName())
                self._visible_toolbars.add(toolbar.objectName())

            self._current_toolbars.add(toolbar)
            self.addToolBar(Qt.TopToolBarArea, toolbar)

        for toolbar in new_toolbars:
            if toolbar.objectName() != '':
                toolbar.setVisible(toolbar.objectName() in self._visible_toolbars)

        # Set correct tab
        if self.document_tabs.currentWidget() != self.uimgr.current_editor.get_widget():
            self.document_tabs.setCurrentWidget(self.uimgr.current_editor.get_widget())

        # Set up source and previews
        source = self.uimgr.current_editor.get_source()

        if not source:
            _log.warning("Current editor doesn't have a source")
            return

        self.current_source = source
        self.alert_publisher.follow_alerts(source)

        if source.offline:
            try:
                source.bring_online()

                if source.offline:
                    # TODO: Make retry action
                    self.source_alert = plugins.Alert(
                        'Unable to bring composition source online',
                        icon=plugins.AlertIcon.Warning,
                        source=self.uimgr.current_editor.name,
                        model_obj=self.uimgr.current_editor.asset,
                        actions=[])

                    self.alert_publisher.show_alert(self.source_alert)
                    return
            except Exception as ex:
                # TODO: Make retry action
                self.source_alert = plugins.Alert(
                    'Unexpected ' + ex.__class__.__name__ + ' while bringing source online: ' + str(ex),
                    icon=plugins.AlertIcon.Error,
                    source='',
                    model_obj=self.uimgr.current_editor.name,
                    actions=[],
                    exc_info=True)

                self.alert_publisher.show_alert(self.source_alert)
                return

        def first_or_default(items, cond):
            for item in items:
                if cond(item):
                    return item

            return None

        streams = source.get_streams()

        video = first_or_default(streams, lambda a: a.stream_type == 'video')
        audio = first_or_default(streams, lambda a: a.stream_type == 'audio')

        # TODO: Set channels, sample rate, current time
        self.audio_stream = audio
        self.alert_publisher.follow_alerts(audio)
        self.audio_player.set_audio_source(audio)

        # TODO: Set frame rate, progressive/interlaced, etc.
        self.video_stream = video
        video.frames_updated.connect(self.handle_update_frames)
        self.alert_publisher.follow_alerts(video)

        self.video_widget.setDisplayWindow(video.format.active_area)
        self.video_widget.setPixelAspectRatio(video.format.pixel_aspect_ratio)
        self.video_widget.setVideoSource(video)

    def handle_update_frames(self, min_frame, max_frame):
        if not self.space_asset:
            return

        # If the current frame was in this set, re-seek to it
        speed = self.uimgr.get_playback_speed()

        if speed.numerator:
            return

        time = self.uimgr.get_presentation_time()
        frame = process.get_time_frame(self.space_asset.space.video_format.frame_rate, time)

        if frame >= min_frame and frame <= max_frame:
            self.uimgr.seek(process.get_frame_time(self.space_asset.space.video_format.frame_rate, int(frame)))

    def open_space(self):
        path = QFileDialog.getOpenFileName(self, "Open File", filter='YAML Files (*.yaml)')

        if path:
            self.open_file(path)

    def save_space(self):
        path = QFileDialog.getSaveFileName(self, "Save File", filter='YAML Files (*.yaml)')

        if path:
            try:
                self.save_file(path)
            except Exception as ex:
                _log.warning('Failed to save file', exc_info=True)
                QMessageBox.error(self, 'Canvas', str(ex))

    def open_file(self, path):
        project = None

        with open(path) as stream:
            project = yaml.load(stream)
            project.fixup()

        self.uimgr.set_asset_list(project.assets)
        self.space_asset = self.uimgr.asset_list['test']

        editor = SpaceEditor(self.uimgr, self.space_asset)
        self.uimgr.add_editor(editor)
        self.uimgr.set_current_editor(editor)

    def save_file(self, path):
        with open(path, 'w') as stream:
            yaml.dump_all(self.uimgr.asset_list.get_asset_list(), stream)

    def render_dv(self):
        # FIXME: This code path has LONG since been outdated
        if not len(self.space_asset.space):
            return

        path = QFileDialog.getSaveFileName(self, "Render DV", filter='AVI Files (*.avi)')

        if path:
            # Create a private workspace for this render
            # TODO: Changes to *filters* in the original workspace may alter this one, too
            # Be sure to check for that before making this process asynchronous
            workspace = process.VideoWorkspace()
            items = sorted(self.space, key=lambda a: a.z_sort_key())

            for i, item in enumerate(items):
                source = self.asset_list.get_stream(item.asset_path, item.stream)
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
        self.uimgr.play(1)
        self.transport_play_action.setChecked(True)

    def transport_pause(self):
        self.uimgr.stop()
        self.transport_pause_action.setChecked(True)

    def transport_fastforward(self):
        self.uimgr.play(2)
        self.transport_fastforward_action.setChecked(True)

    def transport_rewind(self):
        self.uimgr.play(-2)
        self.transport_rewind_action.setChecked(True)

    @_log.warnonerror('Error executing plugin editor dialog')
    def edit_plugins(self, event):
        from fluggo.editor.ui.plugineditor import PluginEditorDialog

        dialog = PluginEditorDialog()
        dialog.exec_()

    @_log.warnonerror('Error executing codec editor dialog')
    def edit_decoders(self, event):
        from fluggo.editor.ui.codeceditor import DecoderEditorDialog

        dialog = DecoderEditorDialog()
        dialog.exec_()


app = QApplication(sys.argv)

window = MainWindow()
window.show()

quit(app.exec_())

