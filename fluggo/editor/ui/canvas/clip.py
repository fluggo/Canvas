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
from fluggo.editor import model
from fluggo.media import sources
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from .thumbnails import ThumbnailPainter

class Handle(QtGui.QGraphicsRectItem, Draggable):
    invisibrush = QtGui.QBrush(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))

    def __init__(self, parent):
        QtGui.QGraphicsRectItem.__init__(self, QtCore.QRectF(), parent)
        Draggable.__init__(self)
        self.brush = QtGui.QBrush(QtGui.QColor.fromRgbF(0.0, 1.0, 0.0))
        self.setAcceptHoverEvents(True)
        self.setOpacity(0.45)
        self.setBrush(self.invisibrush)
        self.setPen(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))

    def hoverEnterEvent(self, event):
        self.setBrush(self.brush)

    def hoverLeaveEvent(self, event):
        self.setBrush(self.invisibrush)

class HorizontalHandle(Handle):
    def __init__(self, parent, ctrlcls):
        Handle.__init__(self, parent)
        self.controller = None
        self.ctrlcls = ctrlcls
        self.setCursor(Qt.SizeHorCursor)

    def drag_start(self, view):
        self.controller = self.ctrlcls(self.parentItem())

    def drag_move(self, view, abs_pos, rel_pos):
        self.controller.move(int(round(rel_pos.x() * self.parentItem().units_per_second)))

    def drag_end(self, view, abs_pos, rel_pos):
        self.controller.finalize()
        self.controller = None

class VerticalHandle(Handle):
    def __init__(self, parent, ctrlcls):
        Handle.__init__(self, parent)
        self.controller = None
        self.ctrlcls = ctrlcls
        self.setCursor(Qt.SizeVerCursor)

    def drag_start(self, view):
        self.controller = self.ctrlcls(self.parentItem())

    def drag_move(self, view, abs_pos, rel_pos):
        self.controller.move(rel_pos.y())

    def drag_end(self, view, abs_pos, rel_pos):
        self.controller.finalize()
        self.controller = None

class ItemPositionController(Controller2D):
    def __init__(self, item, view, units_per_second):
        self.item = item
        self.view = view
        self.units_per_second = units_per_second
        self._left_snap_marker = None
        self._right_snap_marker = None

    def _clear_snap_markers(self):
        scene = self.view.scene()

        if self._left_snap_marker:
            scene.remove_marker(self._left_snap_marker)
            self._left_snap_marker = None

        if self._right_snap_marker:
            scene.remove_marker(self._right_snap_marker)
            self._right_snap_marker = None

    def move(self, x, y):
        item = self.item.item
        view = self.view
        units_per_second = self.units_per_second

        self._clear_snap_markers()

        left_snap = view.find_snap_items_horizontal(self.item, x / units_per_second)
        right_snap = view.find_snap_items_horizontal(self.item, (x + item.length) / units_per_second)

        # left_snap and right_snap are in seconds; convert back to our units
        if left_snap is not None:
            left_snap = int(round(left_snap * units_per_second))

        if right_snap is not None:
            right_snap = int(round(right_snap * units_per_second))

        # Place the snap markers and accept snaps
        if left_snap is not None:
            if right_snap is not None:
                if abs(left_snap - x) < abs(right_snap - x + item.length):
                    self._left_snap_marker = VerticalSnapMarker(left_snap / units_per_second)
                    view.scene().add_marker(self._left_snap_marker)
                    x = left_snap
                elif abs(left_snap - pos_x) > abs(right_snap - pos_x + item.length):
                    self._right_snap_marker = VerticalSnapMarker(right_snap / units_per_second)
                    view.scene().add_marker(self._right_snap_marker)
                    x = right_snap - item.length
                else:
                    self._left_snap_marker = VerticalSnapMarker(left_snap / units_per_second)
                    view.scene().add_marker(self._left_snap_marker)
                    self._right_snap_marker = VerticalSnapMarker(right_snap / units_per_second)
                    view.scene().add_marker(self._right_snap_marker)
                    x = left_snap
            else:
                self._left_snap_marker = VerticalSnapMarker(left_snap / units_per_second)
                view.scene().add_marker(self._left_snap_marker)
                x = left_snap
        elif right_snap is not None:
            self._right_snap_marker = VerticalSnapMarker(right_snap / units_per_second)
            view.scene().add_marker(self._right_snap_marker)
            x = right_snap - item.length

        item.update(x=x, y=y)

    def finalize(self):
        self._clear_snap_markers()

class ClipItem(QtGui.QGraphicsItem, Draggable):
    class LeftController(Controller1D):
        def __init__(self, item):
            self.item = item.item
            self.original_x = self.item.x
            self.original_length = self.item.length
            self.original_offset = self.item.offset

        def move(self, x):
            if self.original_offset + x < 0:
                self.item.update(x=self.original_x - self.original_offset, length=self.original_length + self.original_offset,
                    offset=0)
            elif self.original_length > x:
                self.item.update(x=self.original_x + x, length=self.original_length - x,
                    offset=self.original_offset + x)
            else:
                self.item.update(x=self.original_x + self.original_length - 1, length=1,
                    offset=self.original_offset + self.original_length - 1)

    class RightController(Controller1D):
        def __init__(self, item):
            self.item = item.item
            self.original_length = self.item.length
            self.max_length = item.max_length

        def move(self, x):
            if self.original_length + x > self.max_length:
                self.item.update(length=self.max_length)
            elif self.original_length > -x:
                self.item.update(length=self.original_length + x)
            else:
                self.item.update(length=1)

    class TopController(Controller1D):
        def __init__(self, item):
            self.item = item.item
            self.original_y = self.item.y
            self.original_height = self.item.height

        def move(self, y):
            if self.original_height > y:
                self.item.update(y=self.original_y + y, height=self.original_height - y)
            else:
                self.item.update(y=self.original_y + self.original_height - 1, height=1)

    class BottomController(Controller1D):
        def __init__(self, item):
            self.item = item.item
            self.original_height = self.item.height

        def move(self, y):
            if self.original_height > -y:
                self.item.update(height=self.original_height + y)
            else:
                self.item.update(height=1)

    def __init__(self, item, name):
        QtGui.QGraphicsItem.__init__(self)
        Draggable.__init__(self, QtGui.QGraphicsItem)
        self.item = item

        if hasattr(self.item, 'updated'):
            self.item.updated.connect(self._update)

        self.name = name
        self.setFlags(QtGui.QGraphicsItem.ItemIsSelectable |
            QtGui.QGraphicsItem.ItemUsesExtendedStyleOption)
        self.setAcceptHoverEvents(True)

        self.left_handle = HorizontalHandle(self, self.LeftController)
        self.right_handle = HorizontalHandle(self, self.RightController)
        self.top_handle = VerticalHandle(self, self.TopController)
        self.bottom_handle = VerticalHandle(self, self.BottomController)
        self.bottom_handle.setPos(0.0, self.height)

        self.view_reset_needed = False

    @property
    def height(self):
        return self.item.height

    def itemChange(self, change, value):
        if change == QtGui.QGraphicsItem.ItemSceneHasChanged:
            self._added_to_scene()

        return value

    def _added_to_scene(self):
        # Set the things we couldn't without self.units_per_second
        self.setPos(self.item.x / self.units_per_second, self.item.y)
        self.right_handle.setPos(self.item.length / self.units_per_second, 0.0)

    def _update(self, **kw):
        '''
        Called by the item model to update our appearance.
        '''
        # Alter the apparent Z-order of the item
        if 'z' in kw:
            self.scene().resort_item(self)

        # Changes in item position
        pos = self.pos()

        if 'x' in kw or 'y' in kw:
            self.setPos(kw.get('x', pos.x() * self.units_per_second) / self.units_per_second, kw.get('y', pos.y()))

        # Changes in item size
        if 'length' in kw or 'height' in kw:
            self.right_handle.setPos(self.item.length / self.units_per_second, 0.0)
            self.bottom_handle.setPos(0.0, self.item.height)
            self.view_reset_needed = True

            self.prepareGeometryChange()

    @property
    def units_per_second(self):
        '''
        A float giving the number of units per second in the X axis.
        This will typically be float(scene().frame_rate) or float(scene().sample_rate).
        '''
        raise NotImplementedError

    @property
    def stream(self):
        return None

    @property
    def max_length(self):
        return self.stream.length

    @property
    def z_order(self):
        return self._z_order

    @z_order.setter
    def z_order(self, value):
        self._z_order = value

        if -value != self.zValue():
            self.setZValue(-value)

    def view_scale_changed(self, view):
        # BJC I tried to keep it view-independent, but the handles need to have different sizes
        # depending on the level of zoom in the view (not to mention separate sets of thumbnails)
        hx = view.handle_width / float(view.scale_x)
        hy = view.handle_width / float(view.scale_y)

        self.left_handle.setRect(QtCore.QRectF(0.0, 0.0, hx, self.item.height))
        self.right_handle.setRect(QtCore.QRectF(-hx, 0.0, hx, self.item.height))
        self.top_handle.setRect(QtCore.QRectF(0.0, 0.0, self.item.length / self.units_per_second, hy))
        self.bottom_handle.setRect(QtCore.QRectF(0.0, -hy, self.item.length / self.units_per_second, hy))

    def hoverEnterEvent(self, event):
        view = event.widget().parentWidget()
        self.view_scale_changed(view)

    def hoverMoveEvent(self, event):
        if self.view_reset_needed:
            view = event.widget().parentWidget()
            self.view_scale_changed(view)
            self.view_reset_needed = False

    def boundingRect(self):
        return QtCore.QRectF(0.0, 0.0, self.item.length / self.units_per_second, self.height)

    def drag_start(self, view):
        self._drag_start_x = self.item.x
        self._drag_start_y = self.item.y
        self._drag_controller = ItemPositionController(self, view, self.units_per_second)

    def drag_move(self, view, abs_pos, rel_pos):
        pos_x = int(round(rel_pos.x() * self.units_per_second)) + self._drag_start_x
        pos_y = rel_pos.y() + self._drag_start_y

        self._drag_controller.move(pos_x, pos_y)

    def drag_end(self, view, abs_pos, rel_pos):
        self._drag_controller.finalize()

class VideoItem(ClipItem):
    def __init__(self, item, name):
        ClipItem.__init__(self, item, name)
        self._thumbnail_painter = ThumbnailPainter()
        self._thumbnail_painter.updated.connect(self._handle_thumbnails_updated)
        self._thumbnail_painter.set_length(self.item.length)

    @property
    def units_per_second(self):
        return float(self.scene().frame_rate)

    def _added_to_scene(self):
        ClipItem._added_to_scene(self)
        self._thumbnail_painter.set_stream(self.stream)

    def _handle_thumbnails_updated(self):
        self.update()

    def _update(self, **kw):
        '''
        Called by the item model to update our appearance.
        '''
        # Changes requiring a reset of the thumbnails
        # TODO: This resets thumbnails *way* more than is necessary
        if 'length' in kw:
            self._thumbnail_painter.set_length(self.item.length)

        if 'offset' in kw:
            self._thumbnail_painter.clear()

        ClipItem._update(self, **kw)

    def paint(self, painter, option, widget):
        rect = painter.transform().mapRect(self.boundingRect())
        clip_rect = painter.transform().mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.fillRect(rect, QtGui.QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QtGui.QColor.fromRgbF(0.9, 0.9, 0.8))

        self._thumbnail_painter.paint(painter, rect, clip_rect)

        if self.isSelected():
            painter.fillRect(rect, QtGui.QColor.fromRgbF(1.0, 0, 0, 0.5))

        if self.name:
            painter.setBrush(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0))
            painter.drawText(rect, Qt.TextSingleLine, self.name)

        painter.restore()

class VideoClip(VideoItem):
    def __init__(self, clip, name):
        VideoItem.__init__(self, clip, name)
        self._stream = None

    @property
    def stream(self):
        if not self._stream:
            self._stream = sources.VideoSource(self.scene().source_list.get_stream(self.item.source.source_name, self.item.source.stream_index))
            self._stream.offset = self.item.offset

        return self._stream

class AudioItem(ClipItem):
    def __init__(self, item, name):
        ClipItem.__init__(self, item, name)

    @property
    def units_per_second(self):
        return float(self.scene().sample_rate)

    def paint(self, painter, option, widget):
        rect = painter.transform().mapRect(self.boundingRect())
        clip_rect = painter.transform().mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.fillRect(rect, QtGui.QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QtGui.QColor.fromRgbF(0.9, 0.9, 0.8))

        painter.setBrush(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0))
        painter.drawText(rect, Qt.TextSingleLine, self.name)

        painter.restore()

class AudioClip(AudioItem):
    pass

