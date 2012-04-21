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

from ..canvas import *
from .markers import *
from fluggo.editor import model, graph
from fluggo.media import process
from fluggo.media.basetypes import *
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from .thumbnails import ThumbnailPainter

(LEVEL_ITEMS, LEVEL_MOVE_HANDLES, LEVEL_TRANSITION_HANDLES, LEVEL_TIME_HANDLES, LEVEL_HEIGHT_HANDLES) = range(5)

class _ItemLeftController(Controller1D):
    def __init__(self, handler, view):
        self.item = handler.item
        self.prev_item = self.item.sequence[self.item.index - 1] if self.item.index > 0 else None
        self.next_item = self.item.sequence[self.item.index + 1] if self.item.index < len(self.item.sequence) - 1 else None
        self.sequence = self.item.sequence

        self.original_x = self.item.x
        self.original_length = self.item.length
        self.original_offset = self.item.offset
        self.original_trans_length = self.item.transition_length
        self.original_seq_x = self.sequence.x

        self.min_frame = handler.stream.defined_range[0]

    def move(self, delta):
        # Don't move past the beginning of the clip
        if self.min_frame is not None and self.original_offset + delta < self.min_frame:
            delta = self.min_frame - self.original_offset

        # Don't make the clip shorter than one frame
        if delta >= self.original_length:
            delta = self.original_length - 1

        if self.next_item:
            # Don't let it get shorter than next_item.transition_length
            if self.original_length - delta < self.next_item.transition_length:
                delta = self.original_length - self.next_item.transition_length

        if self.prev_item:
            # transition_length > prev_item.length - prev_item.transition_length: Don't overrun previous item
            if self.original_trans_length - delta > self.prev_item.length - self.prev_item.transition_length:
                delta = self.original_trans_length - (self.prev_item.length - self.prev_item.transition_length)

        self.item.update(
            length=self.original_length - delta,
            offset=self.original_offset + delta,
            transition_length=self.original_trans_length - delta if self.prev_item else 0)

        if not self.prev_item:
            # Adjust the sequence's beginning
            self.sequence.update(x=self.original_seq_x + delta)

class _ItemRightController(Controller1D):
    def __init__(self, handler, view):
        self.item = handler.item
        self.sequence = self.item.sequence
        self.prev_item = self.sequence[self.item.index - 1] if self.item.index > 0 else None
        self.next_item = self.sequence[self.item.index + 1] if self.item.index < len(self.sequence) - 1 else None

        self.original_length = self.item.length
        self.max_frame = handler.stream.defined_range[1]
        self.original_trans_length = self.next_item.transition_length if self.next_item else 0

    def move(self, delta):
        # Don't move past the end of the clip
        if self.max_frame is not None and self.original_length + self.item.offset + delta > self.max_frame:
            delta = self.max_frame - self.original_length - self.item.offset

        # Don't make the clip shorter than one frame
        if self.original_length + delta < 1:
            delta = 1 - self.original_length

        # Also don't make it shorter than the transition_length
        if self.original_length + delta < self.item.transition_length:
            delta = self.item.transition_length - self.original_length

        if self.next_item:
            if self.original_trans_length + delta > self.next_item.length:
                delta = self.next_item.length - self.original_trans_length

        self.item.update(length=self.original_length + delta)

        if self.next_item:
            self.next_item.update(transition_length=self.original_trans_length + delta)

class _SequenceItemHandler(SceneItem):
    drop_opaque = False

    def __init__(self, item, owner):
        SceneItem.__init__(self, item, ThumbnailPainter(), None)

        self.owner = owner
        self.item = item

        self.left_handle = HorizontalHandle(owner, _ItemLeftController, self)
        self.left_handle.setZValue(LEVEL_TIME_HANDLES)
        self.right_handle = HorizontalHandle(owner, _ItemRightController, self)
        self.right_handle.setZValue(LEVEL_TIME_HANDLES)

    @property
    def length(self):
        return self.item.length

    @property
    def units_per_second(self):
        return self.owner.units_per_second

    @property
    def height(self):
        return self.owner.item_display_height

    @property
    def stream_type(self):
        return self.owner.stream_type

    @property
    def source_ref(self):
        return self.item.source

    @property
    def offset(self):
        return self.item.offset

    def added_to_scene(self):
        SceneItem.added_to_scene(self)

        self.setPos(float(self.item.x / self.owner.units_per_second), 0.0 if (self.item.index & 1 == 0) else self.owner.height - self.height)
        self.left_handle.setPos(float(self.item.x / self.owner.units_per_second), self.y())
        self.right_handle.setPos(float((self.item.x + self.item.length) / self.owner.units_per_second), self.y())

    def removed_from_scene(self):
        SceneItem.removed_from_scene(self)

        for a in (self.left_handle, self.right_handle):
            a.scene().removeItem(a)

    def update_view_decorations(self, view):
        hx = view.handle_width / float(view.scale_x)

        self.left_handle.setRect(QtCore.QRectF(0.0, 0.0, hx, self.owner.item_display_height))
        self.right_handle.setRect(QtCore.QRectF(-hx, 0.0, hx, self.owner.item_display_height))

    def y(self):
        return 0.0 if (self.item.index & 1 == 0) else self.owner.height - self.height

    def item_updated(self, **kw):
        if 'x' in kw:
            self.left_handle.setPos(float(self.item.x / self.owner.units_per_second), self.y())
            self.right_handle.setPos(float((self.item.x + self.item.length) / self.owner.units_per_second), self.y())
        elif 'length' in kw:
            self.right_handle.setPos(float((self.item.x + self.item.length) / self.owner.units_per_second), self.y())

        if 'length' in kw or 'height' in kw or 'index' in kw:
            self.setPos(float(self.item.x / self.owner.units_per_second), 0.0 if (self.item.index & 1 == 0) else self.owner.height - self.height)
            self.left_handle.setPos(float(self.item.x / self.owner.units_per_second), self.y())
            self.right_handle.setPos(float((self.item.x + self.item.length) / self.owner.units_per_second), self.y())

            self.reset_view_decorations()
            self.prepareGeometryChange()

        if 'offset' in kw and self._stream:
            self._stream.offset = self.item.offset

        # Changes requiring a reset of the thumbnails
        # TODO: This resets thumbnails *way* more than is necessary
        if self.painter and 'length' in kw:
            self.painter.set_length(self.length)

        if self.painter and 'offset' in kw:
            self.painter.set_offset(self.offset)

        if 'x' in kw or 'height' in kw:
            self.setPos(float(self.item.x / self.owner.units_per_second), self.y())

    def kill(self):
        self.owner.scene().removeItem(self.left_handle)
        self.owner.scene().removeItem(self.right_handle)

class VideoSequence(ClipItem):
    def __init__(self, sequence):
        ClipItem.__init__(self, sequence, None)
        self.manager = None

        self.item.item_added.connect(self._handle_item_added)
        self.item.items_removed.connect(self._handle_items_removed)
        self.item.item_updated.connect(self._handle_item_updated)

        self.left_handle.hide()
        self.right_handle.hide()
        self.top_handle.setZValue(LEVEL_HEIGHT_HANDLES)
        self.bottom_handle.setZValue(LEVEL_HEIGHT_HANDLES)

        self.seq_items = [_SequenceItemHandler(item, self) for item in sequence]

        for seq_item in self.seq_items:
            seq_item.setParentItem(self)
            seq_item.setVisible(self.item.expanded)
            seq_item.setZValue(LEVEL_ITEMS)

    @property
    def item_display_height(self):
        if self.item.expanded:
            # Temporary: 3 top, 3 bottom, 1 middle
            return self.item.height * 3.0 / 7.0
        else:
            return self.item.height

    def _update(self, **kw):
        if 'height' in kw:
            height = kw['height']

            for seq_item in self.seq_items:
                seq_item.item_updated(height=height)

        ClipItem._update(self, **kw)

    def paint(self, painter, option, widget):
        if self.item.expanded:
            if self.view_reset_needed:
                view = widget.parentWidget()
                self.update_view_decorations(view)
                self.view_reset_needed = False

            painter.fillRect(self.boundingRect(), QtGui.QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QtGui.QColor.fromRgbF(0.9, 0.9, 0.8))
        else:
            ClipItem.paint(self, painter, option, widget)

    @property
    def stream(self):
        if not self.manager:
            self.manager = graph.SequenceVideoManager(self.item, self.scene().source_list, self.scene().space.video_format)

        return self.manager

    @property
    def format(self):
        return self.scene().space.video_format

    @property
    def offset(self):
        return 0

    def added_to_scene(self):
        ClipItem.added_to_scene(self)

        for item in self.seq_items:
            item.added_to_scene()

    def _handle_item_added(self, item):
        seq_item = _SequenceItemHandler(item, self)
        self.seq_items.insert(item.index, seq_item)

        seq_item.setParentItem(self)
        seq_item.setVisible(self.item.expanded)
        seq_item.setZValue(LEVEL_ITEMS)

    def _handle_items_removed(self, start, stop):
        for a in self.seq_items[start:stop]:
            self.scene().removeItem(a)

        del self.seq_items[start:stop]

    def _handle_item_updated(self, item, **kw):
        self.seq_items[item.index].item_updated(**kw)


