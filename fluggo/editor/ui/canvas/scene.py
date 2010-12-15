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

class Scene(QtGui.QGraphicsScene):
    DEFAULT_HEIGHT = 40

    def __init__(self, space, source_list):
        QtGui.QGraphicsScene.__init__(self)
        self.source_list = source_list
        self.space = space
        self.space.item_added.connect(self.handle_item_added)
        self.space.item_removed.connect(self.handle_item_removed)
        self.drag_items = None
        self.drag_key_index = 0
        self.sort_list = sortlist.SortedList(keyfunc=lambda a: a.item.z, index_attr='z_order')
        self.marker_added = signal.Signal()
        self.marker_removed = signal.Signal()
        self.markers = set()

        self.frame_rate = fractions.Fraction(24000, 1001)
        self.sample_rate = fractions.Fraction(48000, 1)

        for item in self.space:
            self.handle_item_added(item)

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

    def selected_items(self):
        return [item.item for item in self.selectedItems() if isinstance(item, ClipItem)]

    def handle_item_removed(self, item):
        if item.type() != 'video':
            return

        for ui_item in self.sort_list:
            if ui_item.item is not item:
                continue

            self.removeItem(ui_item)
            self.sort_list.remove(ui_item)

    def resort_item(self, item):
        self.sort_list.move(item.z_order)

    def _lay_out_drag_items(self, pos):
        '''
        Arrange the drag items in the scene.

        pos - Position of the cursor in scene coordinates.
        '''
        # All other items are placed in relation to the item at drag_key_index
        item = self.drag_items[self.drag_key_index]
        time = round(pos.x() * self.get_rate(item.stream_format.type)) / self.get_rate(item.stream_format.type)

        for i, item in enumerate(self.drag_items):
            item_time = round(time * self.get_rate(item.stream_format.type)) / self.get_rate(item.stream_format.type)
            item.setPos(item_time, pos.y() + self.DEFAULT_HEIGHT * (float(i) - 0.5))

    def add_marker(self, marker):
        self.markers.add(marker)
        self.marker_added(marker)

    def remove_marker(self, marker):
        self.markers.remove(marker)
        self.marker_removed(marker)

    def dragEnterEvent(self, event):
        data = event.mimeData()

        if hasattr(data, 'source_name'):
            event.accept()
            source_name = data.source_name
            default_streams = self.source_list.get_default_streams(source_name)

            self.drag_items = [PlaceholderItem(source_name, stream_format, 0, 0, self.DEFAULT_HEIGHT) for stream_format in default_streams]

            for placeholder in self.drag_items:
                self.addItem(placeholder)

            self._lay_out_drag_items(event.scenePos())
        else:
            event.ignore()

    def dragMoveEvent(self, event):
        self._lay_out_drag_items(event.scenePos())

    def dragLeaveEvent(self, event):
        if self.drag_items:
            for item in self.drag_items:
                self.removeItem(item)

            self.drag_items = None

    def dropEvent(self, event):
        # Turn them into real boys and girls
        items = []

        for item in self.drag_items:
            rate = self.frame_rate

            if item.stream_format.type == 'audio':
                rate = self.sample_rate

            items.append(model.Clip(type=item.stream_format.type,
                source=model.StreamSourceRef(source_name=item.source_name, stream_index=item.stream_format.index),
                x=int(round(item.pos().x() * float(rate))), y=item.pos().y(), length=item.width, height=item.height))

            self.removeItem(item)

        self.space[0:0] = items

    @property
    def scene_top(self):
        return -20000.0

    @property
    def scene_bottom(self):
        return 20000.0


