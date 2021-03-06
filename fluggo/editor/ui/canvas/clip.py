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
from fluggo import logging
from fluggo.editor import model
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from .thumbnails import ThumbnailPainter

_log = logging.getLogger(__name__)

class Handle(QtGui.QGraphicsRectItem, Draggable):
    invisibrush = QtGui.QBrush(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))

    def __init__(self, parent, ctrlcls, item=None):
        QtGui.QGraphicsRectItem.__init__(self, QtCore.QRectF(), parent)
        Draggable.__init__(self)
        self.brush = QtGui.QBrush(QtGui.QColor.fromRgbF(0.0, 1.0, 0.0))
        self.setAcceptHoverEvents(True)
        self.setOpacity(0.45)
        self.setBrush(self.invisibrush)
        self.setPen(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))
        self.setCursor(Qt.ArrowCursor)
        self.controller = None
        self.ctrlcls = ctrlcls
        self.item = item or parent

    def hoverEnterEvent(self, event):
        self.setBrush(self.brush)

    def hoverLeaveEvent(self, event):
        self.setBrush(self.invisibrush)

    def drag_start(self, view):
        self.controller = self.ctrlcls(self.item, view)

    def drag_move(self, view, abs_pos, rel_pos):
        self.controller.move(int(round(rel_pos.x() * self.parentItem().units_per_second)), rel_pos.y())

    def drag_end(self, view, abs_pos, rel_pos):
        command = self.controller.finish()
        self.controller = None

        if command:
            self.scene().undo_stack.push(command)

class HorizontalHandle(Handle):
    def __init__(self, parent, ctrlcls, item=None):
        Handle.__init__(self, parent, ctrlcls, item=item)
        self.setCursor(Qt.SplitHCursor)

    def drag_move(self, view, abs_pos, rel_pos):
        self.controller.move(int(round(rel_pos.x() * self.parentItem().units_per_second)))

class VerticalHandle(Handle):
    def __init__(self, parent, ctrlcls, item=None):
        Handle.__init__(self, parent, ctrlcls, item=item)
        self.setCursor(Qt.SplitVCursor)

    def drag_move(self, view, abs_pos, rel_pos):
        self.controller.move(rel_pos.y())

class SceneItem(QtGui.QGraphicsItem):
    drop_opaque = True

    def __init__(self, model_item, painter, name, units_per_second):
        QtGui.QGraphicsItem.__init__(self)

        self.units_per_second = units_per_second

        self.model_item = model_item
        self.name = name
        self.painter = painter

        if self.painter:
            self.painter.updated.connect(self._update_from_painter)

        self._stream = None
        self._format = None

        self.setFlags(QtGui.QGraphicsItem.ItemIsSelectable |
            QtGui.QGraphicsItem.ItemUsesExtendedStyleOption)
        self.setAcceptHoverEvents(True)
        self.setCursor(Qt.ArrowCursor)

        self.view_reset_needed = True

    def _update_from_painter(self, rect):
        if self.scene():
            self.update(rect)

    def itemChange(self, change, value):
        if change == QtGui.QGraphicsItem.ItemSceneHasChanged:
            if self.scene():
                self.added_to_scene()
            else:
                self.removed_from_scene()

        return value

    def reset_view_decorations(self):
        self.view_reset_needed = True

    @property
    def stream_key(self):
        '''Return a key that uniquely identifies the stream represented here,
        for the purposes of caching thumbnails.

        The base implementation returns id(self), but then the cache has to be
        repopulated if this clip is destroyed and re-created in another form.
        Specify a better implementation in a subclass to improve on this.'''
        return ('SceneItem', id(self))

    def added_to_scene(self):
        '''
        Called when the item has been added to the scene, which is a fine time
        to update any properties that require the scene, including the frame rate.
        '''
        if self.painter:
            self.painter.set_stream(self.stream_key, self.stream)
            self.painter.set_length(self.length)
            self.painter.set_offset(self.offset)

    def removed_from_scene(self):
        if self.painter:
            self.painter.clear()

    @property
    def source_ref(self):
        raise NotImplementedError

    @property
    def format(self):
        if not self._format and self.source_ref:
            self._format = self.stream.format

        return self._format

    @property
    def stream(self):
        if not self._stream and self.source_ref:
            source_ref = self.source_ref

            if self.stream_type == 'video':
                self._stream = model.VideoSourceRefConnector(self.scene().source_list, source_ref, model_obj=self.item)

        return self._stream

    @property
    def stream_type(self):
        raise NotImplementedError

    @property
    def length(self):
        raise NotImplementedError

    @property
    def min_frame(self):
        return self.stream.defined_range[0]

    @property
    def max_frame(self):
        return self.stream.defined_range[1]

    @property
    def offset(self):
        raise NotImplementedError

    @property
    def z_order(self):
        return self._z_order

    @z_order.setter
    def z_order(self, value):
        self._z_order = value

        if -value != self.zValue():
            self.setZValue(-value)

    def update_view_decorations(self, view):
        pass

    def boundingRect(self):
        return QtCore.QRectF(0.0, 0.0, self.length / self.units_per_second, self.height)

    def paint(self, painter, option, widget):
        if self.view_reset_needed:
            view = widget.parentWidget()
            self.update_view_decorations(view)
            self.view_reset_needed = False

        if not self.scene():
            return

        transform = painter.transform()

        rect = transform.mapRect(self.boundingRect())
        clip_rect = transform.mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.fillRect(rect, QtGui.QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QtGui.QColor.fromRgbF(0.9, 0.9, 0.8))

        if self.painter:
            self.painter.paint(painter, rect, clip_rect, transform)

        if self.isSelected():
            painter.fillRect(rect, QtGui.QColor.fromRgbF(1.0, 0, 0, 0.5))

        if self.name:
            painter.setBrush(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0))
            painter.drawText(rect, Qt.TextSingleLine, self.name)

        painter.restore()

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.LeftButton != Qt.LeftButton:
            return

        self.scene().do_drag(event)

class ClipItem(SceneItem):
    class LeftController(Controller1D):
        def __init__(self, item, view):
            self.item = item.item
            self.original_x = self.item.x
            self.min_frame = item.min_frame
            self.command = None

        def move(self, x):
            offset = min(x + self.original_x - self.item.x, self.item.length - 1)

            if self.min_frame is not None:
                offset = max(offset, self.min_frame - self.item.offset)

            command = model.AdjustClipStartCommand(self.item, offset)
            command.redo()

            if self.command:
                self.command.mergeWith(command)
            else:
                self.command = command

        def finish(self):
            return self.command

        def reset(self):
            if self.command:
                self.command.undo()
                self.command = None

    class RightController(Controller1D):
        def __init__(self, item, view):
            self.item = item.item
            self.original_length = self.item.length
            self.max_frame = item.max_frame
            self.command = None

        def move(self, x):
            offset = max(x + self.original_length - self.item.length, 1 - self.item.length)

            if self.max_frame is not None:
                offset = min(offset, self.max_frame - (self.item.offset + self.item.length) + 1)

            command = model.AdjustClipLengthCommand(self.item, offset)
            command.redo()

            if self.command:
                self.command.mergeWith(command)
            else:
                self.command = command

        def finish(self):
            return self.command

        def reset(self):
            if self.command:
                self.command.undo()
                self.command = None

    class TopController(Controller1D):
        def __init__(self, item, view):
            self.item = item.item
            self.original_y = self.item.y
            self.original_height = self.item.height
            self.command = None

        def move(self, y):
            offset = min(y + self.original_y - self.item.y, self.item.height - 20.0)

            command = model.AdjustClipTopCommand(self.item, offset)
            command.redo()

            if self.command:
                self.command.mergeWith(command)
            else:
                self.command = command

        def finish(self):
            return self.command

        def reset(self):
            if self.command:
                self.command.undo()
                self.command = None

    class BottomController(Controller1D):
        def __init__(self, item, view):
            self.item = item.item
            self.original_height = self.item.height
            self.command = None

        def move(self, y):
            offset = max(y + self.original_height - self.item.height, 20.0 - self.item.height)

            command = model.AdjustClipHeightCommand(self.item, offset)
            command.redo()

            if self.command:
                self.command.mergeWith(command)
            else:
                self.command = command

        def finish(self):
            return self.command

        def reset(self):
            if self.command:
                self.command.undo()
                self.command = None

    def __init__(self, item, name, units_per_second):
        painter = None

        if item.type() == 'video':
            painter = ThumbnailPainter()

        SceneItem.__init__(self, item, painter, name, units_per_second)

        self.item = item
        self.item.updated.connect(self._update)

        self.left_handle = HorizontalHandle(self, self.LeftController)
        self.right_handle = HorizontalHandle(self, self.RightController)
        self.top_handle = VerticalHandle(self, self.TopController)
        self.bottom_handle = VerticalHandle(self, self.BottomController)

    @property
    def height(self):
        return self.item.height

    @property
    def length(self):
        return self.item.length

    @property
    def stream_type(self):
        return self.item.type()

    @property
    def source_ref(self):
        return self.item.source

    @property
    def offset(self):
        return self.item.offset

    def added_to_scene(self):
        SceneItem.added_to_scene(self)

        # Set the things we couldn't without a parent
        self.setPos(self.item.x / self.units_per_second, self.item.y)
        self.bottom_handle.setPos(0.0, self.height)
        self.right_handle.setPos(self.length / self.units_per_second, 0.0)

    @property
    def stream_key(self):
        # Provide a more intelligent key
        if isinstance(self.source_ref, model.AssetStreamRef):
            return (self.source_ref.asset_path, self.source_ref.stream)

        return SceneItem.stream_key(self)

    def _update(self, **kw):
        try:
            '''
            Called by the item model to update our appearance.
            '''
            if not self.scene():
                return

            # Alter the apparent Z-order of the item
            if 'z' in kw:
                self.scene().resort_item(self)

            # Changes in item position
            pos = self.pos()

            if 'x' in kw or 'y' in kw:
                self.setPos(kw.get('x', pos.x() * self.units_per_second) / self.units_per_second, kw.get('y', pos.y()))

            # Changes in item size
            # Changes requiring a reset of the thumbnails
            # TODO: This resets thumbnails *way* more than is necessary
            if self.painter and 'length' in kw:
                self.painter.set_length(self.length)

            if self.painter and 'offset' in kw:
                self.painter.set_offset(self.offset)

            if 'length' in kw or 'height' in kw:
                self.right_handle.setPos(self.length / self.units_per_second, 0.0)
                self.bottom_handle.setPos(0.0, self.height)
                self.reset_view_decorations()

                self.prepareGeometryChange()

            if 'in_motion' in kw:
                self.setOpacity(0.5 if self.item.in_motion else 1.0)
        except:
            _log.error('Error while updating clip item display', exc_info=True)

    def update_view_decorations(self, view):
        # BJC I tried to keep it view-independent, but the handles need to have different sizes
        # depending on the level of zoom in the view (not to mention separate sets of thumbnails)
        hx = view.handle_width / float(view.scale_x)
        hy = view.handle_width / float(view.scale_y)

        self.left_handle.setRect(QtCore.QRectF(0.0, 0.0, hx, self.item.height))
        self.right_handle.setRect(QtCore.QRectF(-hx, 0.0, hx, self.item.height))
        self.top_handle.setRect(QtCore.QRectF(0.0, 0.0, self.item.length / self.units_per_second, hy))
        self.bottom_handle.setRect(QtCore.QRectF(0.0, -hy, self.item.length / self.units_per_second, hy))

