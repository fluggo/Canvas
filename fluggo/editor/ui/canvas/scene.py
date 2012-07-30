# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2010 Brian J. Crowell <brian@fluggo.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import fractions, traceback
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from ..canvas import *
from fluggo import sortlist, signal
from fluggo.editor import model
from fluggo.media import process
import fluggo.editor

from fluggo import logging

_log = logging.getLogger(__name__)

class Scene(QtGui.QGraphicsScene):
    DEFAULT_HEIGHT = 40.0

    class AssetAddManipulator:
        def __init__(self, space, source, asset_path):
            self.source = source
            self.space = space

            self.add_op = None
            self.item_manip = None

            default_streams = source.get_default_streams()

            self.items = []
            commands = []

            for i, stream in enumerate(default_streams):
                # TODO: Handle streams with infinite ranges
                item = model.Clip(type=stream.stream_type,
                    source=model.AssetStreamRef(asset_path=asset_path, stream=stream.name),
                    x=stream.defined_range[0], offset=stream.defined_range[0],
                    length=stream.defined_range[1] - stream.defined_range[0] + 1,
                    y=i * Scene.DEFAULT_HEIGHT, height=Scene.DEFAULT_HEIGHT)

                if i != 0:
                    # Set two-way anchors to match for defined_range differences
                    offset_ns = (process.get_frame_time(space.rate(item.type()), item.x)
                        - process.get_frame_time(space.rate(self.items[0].type()), self.items[0].x))

                    item.update(anchor=model.Anchor(target=self.items[0], offset_ns=offset_ns, two_way=True))

                self.items.append(item)
                commands.append(model.InsertItemCommand(space, item, i))

            self.add_commands = commands

        def add_to_space(self):
            if not self.add_op:
                self.add_op = model.CompoundCommand('Add asset to space', self.add_commands)
                self.add_op.redo()

                self.item_manip = model.ItemManipulator(self.items, self.items[0].x, Scene.DEFAULT_HEIGHT * 0.5)

        def set_space_item(self, space, x, y):
            self.add_to_space()
            self.item_manip.set_space_item(space, x, y)

        def set_sequence_item(self, sequence, x, y, operation):
            self.add_to_space()
            self.item_manip.set_sequence_item(sequence, x, y, operation)

        def reset(self):
            if self.item_manip:
                self.item_manip.reset()
                self.item_manip = None

            if self.add_op:
                self.add_op.undo()
                self.add_op = None

        def finish(self):
            if not self.add_op:
                raise RuntimeError('Operation not in correct state for finish')

            return model.CompoundCommand('Drag asset to canvas', [self.add_op, self.item_manip.finish()], done=True)

    def __init__(self, space, source_list, undo_stack):
        QtGui.QGraphicsScene.__init__(self)
        self.source_list = source_list
        self.space = space
        self.space.item_added.connect(self._handle_item_added)
        self.space.item_removed.connect(self._handle_item_removed)
        self.drag_op = None
        self.drag_exc = None
        self.drag_is_offline = False
        self.undo_stack = undo_stack
        self.sort_list = sortlist.SortedList(keyfunc=lambda a: a.item.z, index_attr='z_order')
        self.marker_added = signal.Signal()
        self.marker_removed = signal.Signal()
        self.markers = set()

        self.frame_rate = fractions.Fraction(24000, 1001)
        self.sample_rate = fractions.Fraction(48000, 1)

        for item in self.space:
            self._handle_item_added(item)

    def get_rate(self, type_):
        if type_ == 'video':
            return self.frame_rate
        elif type_ == 'audio':
            return self.sample_rate

    def _handle_item_added(self, item):
        ui_item = None

        if isinstance(item, model.Clip):
            ui_item = ClipItem(item, 'Clip', self.get_rate(item.type()))
        elif isinstance(item, model.Sequence):
            if item.type() == 'video':
                ui_item = VideoSequence(item, self.get_rate(item.type()))
        else:
            return

        self.addItem(ui_item)
        self.sort_list.add(ui_item)

    def selected_model_items(self):
        '''Model items that are selected.'''
        return [item.model_item for item in self.selectedItems() if hasattr(item, 'model_item') and item.model_item is not None]

    def _scene_item_for_model_item(self, item):
        # TODO: Might find a more efficient way to do this
        for ui_item in self.sort_list:
            if ui_item.item is not item:
                continue

            return ui_item

        return None

    def load_selection(self, saved_selection):
        '''Clear the selection and select the given objects in the scene.'''
        self.clearSelection()

        for item in saved_selection:
            ui_item = self._scene_item_for_model_item(item)

            if ui_item:
                ui_item.setSelected(True)

    def _handle_item_removed(self, item):
        ui_item = self._scene_item_for_model_item(item)

        if ui_item:
            self.removeItem(ui_item)
            self.sort_list.remove(ui_item)
        else:
            _log.warning('Got removed signal for item not in scene.')

    def resort_item(self, item):
        '''Update an item's position in the z-order.'''
        self.sort_list.move(item.z_order)

    def add_marker(self, marker):
        self.markers.add(marker)
        self.marker_added(marker)

    def remove_marker(self, marker):
        self.markers.remove(marker)
        self.marker_removed(marker)

    def do_drag(self, event):
        '''Start a drag op from the current selection. Used by individual items
        when they find out they are being dragged.'''
        drag = QtGui.QDrag(event.widget())
        data = QtCore.QMimeData()
        data.obj = DragDropSelection(self.space, self.selected_model_items(), event.scenePos())
        drag.setMimeData(data)

        pixmap = QtGui.QPixmap(1, 1)
        pixmap.fill(QtGui.QColor(Qt.transparent))
        drag.setPixmap(pixmap)

        drop_action = drag.exec_(Qt.CopyAction | Qt.MoveAction)

    def dragMoveEvent(self, event):
        QtGui.QGraphicsScene.dragMoveEvent(self, event)

        if self.drag_exc or self.drag_is_offline:
            # An error has occurred, which we'll report if they drop here
            event.ignore()
            return

        if not self.drag_op:
            data = event.mimeData()
            obj = data.obj if hasattr(data, 'obj') else None

            if isinstance(obj, DragDropSelection) and obj.space == self.space:
                # Our own drag-and-drop items
                self.drag_op = model.ItemManipulator(obj.objects, event.scenePos().x(), obj.grab_pos.y())
            elif isinstance(obj, fluggo.editor.DragDropAsset) and obj.asset.is_source:
                source = obj.asset.get_source()

                if source.offline:
                    try:
                        source.bring_online()

                        if source.offline:
                            _log.warning('Failed to bring source online')
                            self.drag_is_offline = True
                            event.ignore()
                            return
                    except Exception:
                        _log.warning('Exception bringing source online', exc_info=True)
                        self.drag_exc = traceback.format_exc()
                        event.ignore()
                        return

                # Use the ItemManipulator to move these around
                self.drag_op = Scene.AssetAddManipulator(self.space, source, obj.asset.path)
                event.accept()

        if not self.drag_op:
            event.ignore()
            return

        cursor_items = self.items(event.scenePos(), Qt.IntersectsItemShape, Qt.DescendingOrder)
        cursor_items = [item for item in cursor_items if isinstance(item, SceneItem) and item.drop_opaque and not item.item.in_motion]
        top_item = cursor_items and cursor_items[0]

        x = event.scenePos().x()
        y = event.scenePos().y()

        if top_item and isinstance(top_item, VideoSequence):
            self.drag_op.set_sequence_item(top_item.item, x, y, 'add')
            event.accept()
            return
        else:
            self.drag_op.set_space_item(self.space, x, y)
            event.accept()
            return





#            if not event.isAccepted():
#                event.accept()
#                self.drag_op.lay_out(event.scenePos())
#            else:
#                self.drag_op = None
#        elif not event.isAccepted():
#            data = event.mimeData()
#            obj = data.obj if hasattr(data, 'obj') else None
#
#            if isinstance(obj, fluggo.editor.DragDropSource):
#                event.accept()
#                self.drag_op = Scene.SourceDragOp(self, obj.asset_path)
#                self.drag_op.lay_out(event.scenePos())
#            elif isinstance(obj, DragDropSelection) and obj.space == self.space:
#                # Our own drag-and-drop items
#                event.accept()
#                self.drag_op = Scene.SelectionDragOp(self, obj.objects, obj.grab_pos)
#                self.drag_op.lay_out(event.scenePos())
#            else:
#                event.ignore()

    def dragLeaveEvent(self, event):
        QtGui.QGraphicsScene.dragLeaveEvent(self, event)

        if self.drag_op:
            try:
                event.accept()
                self.drag_op.reset()
            finally:
                self.drag_op = None

        self.drag_exc = None
        self.drag_is_offline = False

    def dropEvent(self, event):
        QtGui.QGraphicsScene.dropEvent(self, event)

        if self.drag_exc:
            mbox = QMessageBox(self,
                text='Could not bring the asset online',
                informativeText='An unexpected error happened while trying to bring the asset online.',
                icon=QMessageBox.Warning,
                detailedText=self.drag_exc)
            mbox.exec_()

            event.ignore()

        if self.drag_is_offline:
            # TODO: Show alerts from the source?
            mbox = QMessageBox(self,
                text='Could not bring the asset online',
                informativeText='The asset failed to come online.',
                icon=QMessageBox.Warning)
            mbox.exec_()

            event.ignore()

        if self.drag_op:
            try:
                event.accept()
                command = self.drag_op.finish()

                if command:
                    self.undo_stack.push(command)
            except:
                self.drag_op.reset()
            finally:
                self.drag_op = None

        self.drag_exc = None
        self.drag_is_offline = False

    @property
    def scene_top(self):
        return -20000.0

    @property
    def scene_bottom(self):
        return 20000.0

    def update_view_decorations(self, view):
        for item in list(self.items()):
            if isinstance(item, SceneItem):
                item.update_view_decorations(view)

