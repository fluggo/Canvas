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

import fractions
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from ..canvas import *
from fluggo import sortlist, signal
from fluggo.editor import model
import fluggo.editor

class Scene(QtGui.QGraphicsScene):
    DEFAULT_HEIGHT = 40

    class DragOp(object):
        def lay_out(self, pos):
            raise NotImplementedError

        def leave(self):
            raise NotImplementedError

        def drop(self):
            raise NotImplementedError

    class SourceDragOp(DragOp):
        def __init__(self, scene, source_name):
            self.source_name = source_name
            self.scene = scene
            self.present = False

            default_streams = scene.source_list.get_default_streams(source_name)

            self.drag_items = [PlaceholderItem(source_name, stream_format, 0, 0, self.scene.DEFAULT_HEIGHT) for stream_format in default_streams]
            self.drag_key_index = 0

        def lay_out(self, pos):
            # All other items are placed in relation to the item at drag_key_index
            item = self.drag_items[self.drag_key_index]
            time = round(pos.x() * self.scene.get_rate(item.stream_format.type)) / self.scene.get_rate(item.stream_format.type)

            for i, item in enumerate(self.drag_items):
                item_time = round(time * self.scene.get_rate(item.stream_format.type)) / self.scene.get_rate(item.stream_format.type)
                item.setPos(item_time, pos.y() + self.scene.DEFAULT_HEIGHT * (float(i) - 0.5))

            if not self.present:
                for placeholder in self.drag_items:
                    self.scene.addItem(placeholder)

                self.present = True

        def leave(self):
            for item in self.drag_items:
                self.scene.removeItem(item)

            self.present = False

        def drop(self):
            # Turn them into real boys and girls
            items = []

            for item in self.drag_items:
                rate = self.scene.get_rate(item.stream_format.type)

                items.append(model.Clip(type=item.stream_format.type,
                    source=model.StreamSourceRef(source_name=item.source_name, stream_index=item.stream_format.index),
                    x=int(round(item.pos().x() * float(rate))), y=item.pos().y(), length=item.width, height=item.height))

                self.scene.removeItem(item)

            self.scene.space[0:0] = items

    def __init__(self, space, source_list):
        QtGui.QGraphicsScene.__init__(self)
        self.source_list = source_list
        self.space = space
        self.space.item_added.connect(self.handle_item_added)
        self.space.item_removed.connect(self.handle_item_removed)
        self.drag_op = None
        self.sort_list = sortlist.SortedList(keyfunc=lambda a: a.item.z, index_attr='z_order')
        self.marker_added = signal.Signal()
        self.marker_removed = signal.Signal()
        self.markers = set()

        self.frame_rate = fractions.Fraction(24000, 1001)
        self.sample_rate = fractions.Fraction(48000, 1)

        for item in self.space:
            self.handle_item_added(item)

    def get_rate(self, type_):
        if type_ == 'video':
            return self.frame_rate
        elif type_ == 'audio':
            return self.sample_rate

    def handle_item_added(self, item):
        ui_item = None

        if isinstance(item, model.Clip):
            ui_item = ClipItem(item, 'Clip')
        elif isinstance(item, model.Sequence):
            if item.type() == 'video':
                ui_item = VideoSequence(item)
        else:
            return

        self.addItem(ui_item)
        self.sort_list.add(ui_item)

    def selected_model_items(self):
        '''Model items that are selected.'''
        return [item.model_item for item in self.selectedItems() if hasattr(item, 'model_item') and item.model_item is not None]

    def handle_item_removed(self, item):
        for ui_item in self.sort_list:
            if ui_item.item is not item:
                continue

            self.removeItem(ui_item)
            self.sort_list.remove(ui_item)

    def resort_item(self, item):
        self.sort_list.move(item.z_order)

    def add_marker(self, marker):
        self.markers.add(marker)
        self.marker_added(marker)

    def remove_marker(self, marker):
        self.markers.remove(marker)
        self.marker_removed(marker)

    def do_drag(self, event):
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

        if not self.drag_op:
            data = event.mimeData()
            obj = data.obj if hasattr(data, 'obj') else None

            if isinstance(obj, DragDropSelection) and obj.space == self.space:
                # Our own drag-and-drop items
                rate = self.get_rate(obj.objects[0].type())
                self.drag_op = model.ItemManipulator(obj.objects, int(round(obj.grab_pos.x() * float(rate))), obj.grab_pos.y())

        if not self.drag_op:
            event.ignore()
            return

        cursor_items = self.items(event.scenePos(), Qt.IntersectsItemShape, Qt.DescendingOrder)
        cursor_items = [item for item in cursor_items if isinstance(item, SceneItem) and item.drop_opaque and not item.item.in_motion]
        top_item = cursor_items and cursor_items[0]

        main_item = self.drag_op.items[0]
        rate = self.get_rate(main_item.type())

        x = int(round(event.scenePos().x() * float(rate)))
        y = event.scenePos().y()

        if top_item and isinstance(top_item, VideoSequence) and main_item.type() == 'video' and self.drag_op.can_set_sequence_item(top_item.item, x, 'add'):
            if self.drag_op.set_sequence_item(top_item.item, x, 'add'):
                event.accept()
                return
        else:
            if self.drag_op.set_space_item(self.space, x, y):
                event.accept()
                return

        event.ignore()



                

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
#                self.drag_op = Scene.SourceDragOp(self, obj.source_name)
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
            event.accept()
            self.drag_op.reset()
            self.drag_op = None

    def dropEvent(self, event):
        QtGui.QGraphicsScene.dropEvent(self, event)

        if self.drag_op:
            event.accept()
            self.drag_op.finish()
            self.drag_op = None

    @property
    def scene_top(self):
        return -20000.0

    @property
    def scene_bottom(self):
        return 20000.0

    def update_view_decorations(self, view):
        for item in self.items():
            if isinstance(item, SceneItem):
                item.update_view_decorations(view)

