# -*- coding: utf-8 -*-
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

import fractions, math
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from fluggo import sortlist, signal
from fluggo.media import process, timecode
from fluggo.editor import canvas
from fluggo.media.basetypes import *
from . import ruler

_queue = process.VideoPullQueue()

def _rectf_to_rect(rect):
    '''Finds the smallest QRect enclosing the given QRectF.'''
    return QRect(
        math.floor(rect.x()),
        math.floor(rect.y()),
        math.ceil(rect.width() + rect.x() - math.floor(rect.x())),
        math.ceil(rect.height() + rect.y() - math.floor(rect.y())))

class Scene(QGraphicsScene):
    DEFAULT_HEIGHT = 40

    def __init__(self, space, source_list):
        QGraphicsScene.__init__(self)
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

    def get_rate(self, item_type):
        if item_type == 'video':
            return self.frame_rate
        elif item_type == 'audio':
            return self.sample_rate

    def handle_item_added(self, item):
        ui_item = None

        if isinstance(item, canvas.Clip):
            if item.type() == 'video':
                ui_item = VideoItem(item, 'Clip')
            elif item.type() == 'audio':
                ui_item = AudioItem(item, 'Clip')
        elif isinstance(item, canvas.Timeline):
            return
        else:
            return

        self.addItem(ui_item)
        ui_item.added_to_scene()

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

            items.append(canvas.Clip(type=item.stream_format.type,
                source=canvas.StreamSourceRef(source_name=item.source_name, stream_index=item.stream_format.index),
                x=int(round(item.pos().x() * float(rate))), y=item.pos().y(), width=item.width, height=item.height))

            self.removeItem(item)

        self.space[0:0] = items

    @property
    def scene_top(self):
        return -20000.0

    @property
    def scene_bottom(self):
        return 20000.0

class ForegroundMarker(object):
    def boundingRect(self, view):
        '''
        Return the bounding rectangle of the marker in view coordinates.
        '''
        raise NotImplementedError

    def paint(self, view, painter, rect):
        '''
        Paint the marker for the given view using the painter and rect, which are both in scene coordinates.
        '''
        pass

class HorizontalSnapMarker(ForegroundMarker):
    def __init__(self, y):
        self.y = y

    def bounding_rect(self, view):
        pos_y = view.viewportTransform().map(QPointF(0.0, float(self.y))).y()
        return QRectF(0.0, pos_y - (view_snap_marker_width / 2.0), view.viewport().width(), view.snap_marker_width)

    def paint(self, view, painter, rect):
        pos_y = painter.transform().map(QPointF(0.0, float(self.y))).y()
        rect = painter.transform().mapRect(rect)

        painter.save()
        painter.resetTransform()

        gradient = QLinearGradient(0.0, pos_y, 0.0, pos_y + view.snap_marker_width / 2.0)
        gradient.setSpread(QGradient.ReflectSpread)
        gradient.setStops([
            (0.0, QColor.fromRgbF(1.0, 1.0, 1.0, 1.0)),
            (0.5, QColor.fromRgbF(view.snap_marker_color.redF(), view.snap_marker_color.greenF(), view.snap_marker_color.blueF(), 0.5)),
            (1.0, QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))])

        painter.setPen(Qt.transparent)
        painter.setBrush(QBrush(gradient))
        painter.drawRect(QRectF(rect.x(), pos_y - (view.snap_marker_width / 2.0), rect.width(), view.snap_marker_width))

        painter.restore()

class VerticalSnapMarker(ForegroundMarker):
    def __init__(self, time):
        self.time = time

    def bounding_rect(self, view):
        pos_x = view.viewportTransform().map(QPointF(float(self.time), 0.0)).x()
        return QRectF(pos_x - (view.snap_marker_width / 2.0), 0.0, view.snap_marker_width, view.viewport().height())

    def paint(self, view, painter, rect):
        pos_x = painter.transform().map(QPointF(float(self.time), 0.0)).x()
        rect = painter.transform().mapRect(rect)

        painter.save()
        painter.resetTransform()

        gradient = QLinearGradient(pos_x, 0.0, pos_x + view.snap_marker_width / 2.0, 0.0)
        gradient.setSpread(QGradient.ReflectSpread)
        gradient.setStops([
            (0.0, QColor.fromRgbF(1.0, 1.0, 1.0, 1.0)),
            (0.5, QColor.fromRgbF(view.snap_marker_color.redF(), view.snap_marker_color.greenF(), view.snap_marker_color.blueF(), 0.5)),
            (1.0, QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))])

        painter.setPen(Qt.transparent)
        painter.setBrush(QBrush(gradient))
        painter.drawRect(QRectF(pos_x - (view.snap_marker_width / 2.0), rect.y(), view.snap_marker_width, rect.height()))

        painter.restore()

class View(QGraphicsView):
    black_pen = QPen(QColor.fromRgbF(0.0, 0.0, 0.0))
    white_pen = QPen(QColor.fromRgbF(1.0, 1.0, 1.0))
    handle_width = 10.0
    snap_marker_color = QColor.fromRgbF(0.0, 1.0, 0.0)
    snap_marker_width = 5.0
    snap_distance = 8.0
    max_zoom_x = 100000.0
    min_zoom_x = 0.01

    def __init__(self, clock, space, source_list):
        QGraphicsView.__init__(self)
        self.setScene(Scene(space, source_list))
        self.setViewportMargins(0, 30, 0, 0)
        self.setAlignment(Qt.AlignLeft | Qt.AlignTop)
        self.setViewportUpdateMode(self.FullViewportUpdate)

        self.ruler = ruler.TimeRuler(self, timecode=timecode.NtscDropFrame())
        self.ruler.move(self.frameWidth(), self.frameWidth())
        self._reset_ruler_scroll()

        # A warning: clock and clock_callback_handle will create a pointer cycle here,
        # which probably won't be freed unless the callback handle is explicitly
        # destroyed with self.clock_callback_handle.unregister() and self.clock = None
        self.playback_timer = None
        self.clock = clock
        self.clock_callback_handle = self.clock.register_callback(self._clock_changed, None)
        self.clock_frame = 0

        self.white = False
        self.frame = 0
        self.set_current_frame(0)
        self.blink_timer = self.startTimer(1000)

        self.scene().sceneRectChanged.connect(self.handle_scene_rect_changed)
        self.scene().marker_added.connect(self._handle_marker_changed)
        self.scene().marker_removed.connect(self._handle_marker_changed)
        self.ruler.current_frame_changed.connect(self.handle_ruler_current_frame_changed)

        self.scale_x = fractions.Fraction(1)
        self.scale_y = fractions.Fraction(1)

        self.scale(4 * 24, 1)

    def _clock_changed(self, speed, time, data):
        if speed.numerator and self.playback_timer is None:
            self.playback_timer = self.startTimer(20)
        elif not speed.numerator and self.playback_timer is not None:
            self.killTimer(self.playback_timer)
            self.playback_timer = None

        self._update_clock_frame(time)

    def selected_items(self):
        return self.scene().selected_items()

    def scale(self, sx, sy, scale_center=None):
        # Find the current center
        center = QRectF(self.mapToScene(0, 0), self.mapToScene(self.width(), self.height())).center()

        if scale_center is None:
            scale_center = center.x()

        self.scale_x = fractions.Fraction(sx)
        self.scale_y = fractions.Fraction(sy)

        self.ruler.set_scale(sx / self.scene().frame_rate)

        self.resetTransform()
        QGraphicsView.scale(self, float(sx), float(sy))
        self.centerOn(scale_center, center.x())
        self._reset_ruler_scroll()

    def set_current_frame(self, frame):
        '''
        view.set_current_frame(frame)

        Moves the view's current frame marker.
        '''
        self._invalidate_marker(self.frame)
        self.frame = frame
        self._invalidate_marker(frame)

        self.ruler.set_current_frame(frame)
        self.clock.seek(process.get_frame_time(self.scene().frame_rate, int(frame)))

    def _update_clock_frame(self, time=None):
        if not time:
            time = self.clock.get_presentation_time()

        frame = process.get_time_frame(self.scene().frame_rate, time)
        self._set_clock_frame(frame)

    def _set_clock_frame(self, frame):
        '''
        view._set_clock_frame(frame)

        Moves the view's current clock frame marker.
        '''
        self._invalidate_marker(self.clock_frame)
        self.clock_frame = frame
        self._invalidate_marker(frame)

    def resizeEvent(self, event):
        self.ruler.resize(self.width() - self.frameWidth(), 30)

    def wheelEvent(self, event):
        if event.delta() > 0:
            factor = 2 ** (event.delta() / 120)

            if self.scale_x * factor > self.max_zoom_x:
                return

            self.scale(self.scale_x * factor, self.scale_y, scale_center=self.mapToScene(event.pos()).x())
        else:
            factor = 2 ** (-event.delta() / 120)

            if self.scale_x / factor < self.min_zoom_x:
                return

            self.scale(self.scale_x / factor, self.scale_y, scale_center=self.mapToScene(event.pos()).x())

    def handle_scene_rect_changed(self, rect):
        self._reset_ruler_scroll()

    def handle_ruler_current_frame_changed(self, frame):
        self.set_current_frame(frame)

    def updateSceneRect(self, rect):
        QGraphicsView.updateSceneRect(self, rect)
        self._reset_ruler_scroll()

    def scrollContentsBy(self, dx, dy):
        QGraphicsView.scrollContentsBy(self, dx, dy)

        if dx:
            self._reset_ruler_scroll()

    def _reset_ruler_scroll(self):
        left = self.mapToScene(0, 0).x() * float(self.scene().frame_rate)
        self.ruler.set_left_frame(left)

    def _invalidate_marker(self, frame):
        # BJC: No, for some reason, invalidateScene() did not work here
        top = self.mapFromScene(frame / float(self.scene().frame_rate), self.scene().scene_top)
        bottom = self.mapFromScene(frame / float(self.scene().frame_rate), self.scene().scene_bottom)

        top = self.mapToScene(top.x() - 1, top.y())
        bottom = self.mapToScene(bottom.x() + 1, bottom.y())

        self.updateScene([QRectF(top, bottom)])

    def timerEvent(self, event):
        if event.timerId() == self.blink_timer:
            self.white = not self.white
            self._invalidate_marker(self.frame)
        elif event.timerId() == self.playback_timer:
            self._update_clock_frame()

    def drawForeground(self, painter, rect):
        '''
        Draws the marker in the foreground.
        '''
        QGraphicsView.drawForeground(self, painter, rect)

        # Clock frame line
        x = self.clock_frame / float(self.scene().frame_rate)
        painter.setPen(self.black_pen)
        painter.drawLine(QPointF(x, rect.y()), QPointF(x, rect.y() + rect.height()))

        # Current frame line, which blinks
        x = self.frame / float(self.scene().frame_rate)
        painter.setPen(self.white_pen if self.white else self.black_pen)
        painter.drawLine(QPointF(x, rect.y()), QPointF(x, rect.y() + rect.height()))

        for marker in self.scene().markers:
            marker.paint(self, painter, rect)

    def _handle_marker_changed(self, marker):
        rect = self.viewportTransform().inverted()[0].mapRect(marker.bounding_rect(self))
        self.updateScene([rect])

    def find_snap_items_horizontal(self, item, time):
        '''
        Find the nearest horizontal snap point for the given item and time. (The
        item is only used to avoid finding it as its own snap point.)
        '''
        top = self.mapFromScene(time, self.scene().scene_top)
        bottom = self.mapFromScene(time, self.scene().scene_bottom)

        items = self.items(QRect(top.x() - self.snap_distance, top.y(), self.snap_distance * 2, bottom.y() - top.y()), Qt.IntersectsItemBoundingRect)

        # TODO: Find something more generic than video items
        items = [a for a in items if isinstance(a, ClipItem) and a is not item]

        # Transform the snap_distance into time units
        distance = self.viewportTransform().inverted()[0].mapRect(QRectF(0.0, 0.0, self.snap_distance, 1.0)).width()
        x = None

        #if distance < 1.0:
        #    distance = 1.0

        for item in items:
            if abs(item.item.x / item.units_per_second - time) < distance:
                x = item.item.x / item.units_per_second
                distance = abs(x - time)

            if abs((item.item.x + item.item.width) / item.units_per_second - time) < distance:
                x = (item.item.x + item.item.width) / item.units_per_second
                distance = abs(x - time)

        return x

class Draggable(object):
    def __init__(self, drag_base=None):
        self.drag_active = False
        self.drag_down = False
        self.drag_start_pos = None
        self.drag_start_screen_pos = None
        self._drag_base = drag_base

    def drag_start(self, view):
        pass

    def drag_move(self, view, abs_pos, rel_pos):
        pass

    def drag_end(self, view, abs_pos, rel_pos):
        pass

    def mousePressEvent(self, event):
        if self._drag_base:
            self._drag_base.mousePressEvent(self, event)

        if event.button() == Qt.LeftButton:
            self.drag_down = True
            self.drag_start_pos = event.scenePos()
            self.drag_start_screen_pos = event.screenPos()

    def mouseReleaseEvent(self, event):
        if self._drag_base:
            self._drag_base.mouseReleaseEvent(self, event)

        if event.button() == Qt.LeftButton:
            if self.drag_active:
                view = event.widget().parent()
                pos = event.scenePos()

                self.drag_end(view, pos, pos - self.drag_start_pos)
                self.drag_active = False

            self.drag_down = False

    def mouseMoveEvent(self, event):
        if self._drag_base:
            self._drag_base.mouseMoveEvent(self, event)

        if not self.drag_down:
            return

        view = event.widget().parent()
        pos = event.scenePos()
        screen_pos = event.screenPos()

        if not self.drag_active:
            if abs(screen_pos.x() - self.drag_start_screen_pos.x()) >= QApplication.startDragDistance() or \
                    abs(screen_pos.y() - self.drag_start_screen_pos.y()) >= QApplication.startDragDistance():
                self.drag_active = True
                self.drag_start(view)
            else:
                return

        self.drag_move(view, pos, pos - self.drag_start_pos)

class _Handle(QGraphicsRectItem, Draggable):
    invisibrush = QBrush(QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))
    horizontal = True

    def __init__(self, rect, parent):
        QGraphicsRectItem.__init__(self, rect, parent)
        Draggable.__init__(self)
        self.brush = QBrush(QColor.fromRgbF(0.0, 1.0, 0.0))
        self.setAcceptHoverEvents(True)
        self.setOpacity(0.45)
        self.setBrush(self.invisibrush)
        self.setPen(QColor.fromRgbF(0.0, 0.0, 0.0, 0.0))
        self.setCursor(self.horizontal and Qt.SizeHorCursor or Qt.SizeVerCursor)

        self.original_x = None
        self.original_width = None
        self.original_offset = None
        self.original_y = None
        self.original_height = None

    def drag_start(self, view):
        self.original_x = int(round(self.parentItem().pos().x() * self.parentItem().units_per_second))
        self.original_width = self.parentItem().item.width
        self.original_offset = self.parentItem().item.offset
        self.original_y = self.parentItem().pos().y()
        self.original_height = self.parentItem().item.height

    def hoverEnterEvent(self, event):
        self.setBrush(self.brush)

    def hoverLeaveEvent(self, event):
        self.setBrush(self.invisibrush)

class _LeftHandle(_Handle):
    def drag_move(self, view, abs_pos, rel_pos):
        x = int(round(rel_pos.x() * self.parentItem().units_per_second))

        if self.original_offset + x < 0:
            self.parentItem().item.update(x=self.original_x - self.original_offset, width=self.original_width + self.original_offset,
                offset=0)
        elif self.original_width > x:
            self.parentItem().item.update(x=self.original_x + x, width=self.original_width - x,
                offset=self.original_offset + x)
        else:
            self.parentItem().item.update(x=self.original_x + self.original_width - 1, width=1,
                offset=self.original_offset + self.original_width - 1)

class _RightHandle(_Handle):
    def drag_move(self, view, abs_pos, rel_pos):
        x = int(round(rel_pos.x() * self.parentItem().units_per_second))

        if self.original_width + x > self.parentItem().max_length:
            self.parentItem().item.update(width=self.parentItem().max_length)
        elif self.original_width > -x:
            self.parentItem().item.update(width=self.original_width + x)
        else:
            self.parentItem().item.update(width=1)

class _TopHandle(_Handle):
    horizontal = False

    def drag_move(self, view, abs_pos, rel_pos):
        y = rel_pos.y()

        if self.original_height > y:
            self.parentItem().item.update(y=self.original_y + y, height=self.original_height - y)
        else:
            self.parentItem().item.update(y=self.original_y + self.original_height - 1, height=1)

class _BottomHandle(_Handle):
    horizontal = False

    def drag_move(self, view, abs_pos, rel_pos):
        y = rel_pos.y()

        if self.original_height > -y:
            self.parentItem().item.update(height=self.original_height + y)
        else:
            self.parentItem().item.update(height=1)

class ClipItem(QGraphicsItem, Draggable):
    def __init__(self, item, name):
        QGraphicsItem.__init__(self)
        Draggable.__init__(self, QGraphicsItem)
        self.item = item
        self.item.updated.connect(self._update)

        self.name = name
        self.setFlags(QGraphicsItem.ItemIsSelectable |
            QGraphicsItem.ItemUsesExtendedStyleOption)
        self.setAcceptHoverEvents(True)

        self._stream = None

        self.left_handle = _LeftHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.right_handle = _RightHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.right_handle.setPos(self.item.width, 0.0)
        self.top_handle = _TopHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.bottom_handle = _BottomHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.bottom_handle.setPos(0.0, self.item.height)

        self.view_reset_needed = False

    def added_to_scene(self):
        # Set the things we couldn't without self.units_per_second
        self.setPos(self.item.x / self.units_per_second, self.item.y)
        self.right_handle.setPos(self.item.width / self.units_per_second, 0.0)

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
        if 'width' in kw or 'height' in kw:
            self.right_handle.setPos(self.item.width / self.units_per_second, 0.0)
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
        if not self._stream:
            if isinstance(self.item.source, canvas.StreamSourceRef):
                self._stream = self.scene().source_list.get_stream(self.item.source.source_name, self.item.source.stream_index)

        return self._stream

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

        self.left_handle.setRect(QRectF(0.0, 0.0, hx, self.item.height))
        self.right_handle.setRect(QRectF(-hx, 0.0, hx, self.item.height))
        self.top_handle.setRect(QRectF(0.0, 0.0, self.item.width / self.units_per_second, hy))
        self.bottom_handle.setRect(QRectF(0.0, -hy, self.item.width / self.units_per_second, hy))

    def hoverEnterEvent(self, event):
        view = event.widget().parentWidget()
        self.view_scale_changed(view)

    def hoverMoveEvent(self, event):
        if self.view_reset_needed:
            view = event.widget().parentWidget()
            self.view_scale_changed(view)
            self.view_reset_needed = False

    def boundingRect(self):
        return QRectF(0.0, 0.0, self.item.width / self.units_per_second, self.item.height)

    def drag_start(self, view):
        self._drag_start_x = self.item.x
        self._drag_start_y = self.item.y
        self._left_snap_marker = None
        self._right_snap_marker = None

    def _clear_snap_markers(self):
        if self._left_snap_marker:
            self.scene().remove_marker(self._left_snap_marker)
            self._left_snap_marker = None

        if self._right_snap_marker:
            self.scene().remove_marker(self._right_snap_marker)
            self._right_snap_marker = None

    def drag_move(self, view, abs_pos, rel_pos):
        pos_x = int(round(rel_pos.x() * self.units_per_second)) + self._drag_start_x
        pos_y = rel_pos.y() + self._drag_start_y

        self._clear_snap_markers()

        left_snap = view.find_snap_items_horizontal(self, pos_x / self.units_per_second)
        right_snap = view.find_snap_items_horizontal(self, (pos_x + self.item.width) / self.units_per_second)

        # left_snap and right_snap are in seconds; convert back to our units
        if left_snap is not None:
            left_snap = int(round(left_snap * self.units_per_second))

        if right_snap is not None:
            right_snap = int(round(right_snap * self.units_per_second))

        # Place the snap markers and accept snaps
        if left_snap is not None:
            if right_snap is not None:
                if abs(left_snap - pos_x) < abs(right_snap - pos_x + self.item.width):
                    self._left_snap_marker = VerticalSnapMarker(left_snap / self.units_per_second)
                    self.scene().add_marker(self._left_snap_marker)
                    pos_x = left_snap
                elif abs(left_snap - pos_x) > abs(right_snap - pos_x + self.item.width):
                    self._right_snap_marker = VerticalSnapMarker(right_snap / self.units_per_second)
                    self.scene().add_marker(self._right_snap_marker)
                    pos_x = right_snap - self.item.width
                else:
                    self._left_snap_marker = VerticalSnapMarker(left_snap / self.units_per_second)
                    self.scene().add_marker(self._left_snap_marker)
                    self._right_snap_marker = VerticalSnapMarker(right_snap / self.units_per_second)
                    self.scene().add_marker(self._right_snap_marker)
                    pos_x = left_snap
            else:
                self._left_snap_marker = VerticalSnapMarker(left_snap / self.units_per_second)
                self.scene().add_marker(self._left_snap_marker)
                pos_x = left_snap
        elif right_snap is not None:
            self._right_snap_marker = VerticalSnapMarker(right_snap / self.units_per_second)
            self.scene().add_marker(self._right_snap_marker)
            pos_x = right_snap - self.item.width

        self.item.update(x=pos_x, y=pos_y)

    def drag_end(self, view, abs_pos, rel_pos):
        self._clear_snap_markers()

class VideoItem(ClipItem):
    def __init__(self, item, name):
        ClipItem.__init__(self, item, name)
        self.thumbnails = []
        self.thumbnail_indexes = []
        self.thumbnail_width = 1.0

    @property
    def units_per_second(self):
        return float(self.scene().frame_rate)

    def _update(self, **kw):
        '''
        Called by the item model to update our appearance.
        '''
        # Changes requiring a reset of the thumbnails
        # TODO: This resets thumbnails *way* more than is necessary
        if 'height' in kw or 'width' in kw or 'offset' in kw:
            self._cancel_all_thumbnails()
            self.thumbnails = []

        ClipItem._update(self, **kw)

    def _cancel_all_thumbnails(self):
        for frame in self.thumbnails:
            if hasattr(frame, 'cancel'):
                frame.cancel()

    def _create_thumbnails(self, total_width):
        # Calculate how many thumbnails fit
        box = self.stream.format.thumbnail_box
        aspect = self.stream.format.pixel_aspect_ratio
        start_frame = self.item.offset
        frame_count = self.item.width

        self.thumbnail_width = (self.item.height * float(box.width) * float(aspect)) / float(box.height)
        count = min(max(int(total_width / self.thumbnail_width), 1), frame_count)

        if len(self.thumbnails) == count:
            return

        self._cancel_all_thumbnails()
        self.thumbnails = [None for a in range(count)]

        if count == 1:
            self.thumbnail_indexes = [start_frame]
        else:
            self.thumbnail_indexes = [start_frame + int(float(a) * frame_count / (count - 1)) for a in range(count)]

    def paint(self, painter, option, widget):
        rect = painter.transform().mapRect(self.boundingRect())
        clip_rect = painter.transform().mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.fillRect(rect, QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QColor.fromRgbF(0.9, 0.9, 0.8))

        # Figure out which thumbnails belong here and paint them
        # The thumbnail lefts are at (i * (rect.width - thumbnail_width) / (len(thumbnails) - 1)) + rect.x()
        # Rights are at left + thumbnail_width
        stream = self.stream

        if stream:
            self._create_thumbnails(rect.width())
            box = stream.format.thumbnail_box

            left_nail = int((clip_rect.x() - self.thumbnail_width - rect.x()) *
                (len(self.thumbnails) - 1) / (rect.width() - self.thumbnail_width))
            right_nail = int((clip_rect.x() + clip_rect.width() - rect.x()) *
                (len(self.thumbnails) - 1) / (rect.width() - self.thumbnail_width)) + 1
            left_nail = max(0, left_nail)
            right_nail = min(len(self.thumbnails), right_nail)

            scale = process.VideoScaler(stream,
                target_point=v2f(0, 0), source_point=box.min,
                scale_factors=v2f(rect.height() * float(stream.format.pixel_aspect_ratio) / box.height,
                    rect.height() / box.height),
                source_rect=box)

            def callback(frame_index, frame, user_data):
                (thumbnails, i) = user_data

                size = frame.current_window.size()
                img_str = frame.to_argb32_string()

                thumbnails[i] = QImage(img_str, size.x, size.y, QImage.Format_ARGB32_Premultiplied).copy()

                # TODO: limit to thumbnail's area
                self.update()

            for i in range(left_nail, right_nail):
                # Later we'll delegate this to another thread
                if not self.thumbnails[i]:
                    self.thumbnails[i] = _queue.enqueue(source=scale, frame_index=self.thumbnail_indexes[i],
                        window=stream.format.thumbnail_box,
                        callback=callback, user_data=(self.thumbnails, i))

                # TODO: Scale existing thumbnails to fit (removing last thumbnails = [] in _update)
                if isinstance(self.thumbnails[i], QImage):
                    if len(self.thumbnails) == 1:
                        painter.drawImage(rect.x() + (i * (rect.width() - self.thumbnail_width)),
                            rect.y(), self.thumbnails[i])
                    else:
                        painter.drawImage(rect.x() + (i * (rect.width() - self.thumbnail_width) / (len(self.thumbnails) - 1)),
                            rect.y(), self.thumbnails[i])
        else:
            # TODO: Show a slug or something?
            pass

        if self.isSelected():
            painter.fillRect(rect, QColor.fromRgbF(1.0, 0, 0, 0.5))

        if self.name:
            painter.setBrush(QColor.fromRgbF(0.0, 0.0, 0.0))
            painter.drawText(rect, Qt.TextSingleLine, self.name)

        painter.restore()

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

        painter.fillRect(rect, QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QColor.fromRgbF(0.9, 0.9, 0.8))

        painter.setBrush(QColor.fromRgbF(0.0, 0.0, 0.0))
        painter.drawText(rect, Qt.TextSingleLine, self.name)

        painter.restore()

class PlaceholderItem(QGraphicsItem):
    def __init__(self, source_name, stream_format, x, y, height):
        QGraphicsItem.__init__(self)

        self.source_name = source_name
        self.stream_format = stream_format
        self.height = height
        self.width = self.stream_format.adjusted_length
        self.setPos(x, y)

    def boundingRect(self):
        return QRectF(0.0, 0.0, self.width, self.height)

    def paint(self, painter, option, widget):
        WIDTH = 3.0
        rect = painter.transform().mapRect(self.boundingRect())
        clip_rect = painter.transform().mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.setPen(QPen(QColor.fromRgbF(1.0, 1.0, 1.0), WIDTH))
        painter.drawRect(QRectF(rect.x() + WIDTH/2, rect.y() + WIDTH/2, rect.width() - WIDTH, rect.height() - WIDTH))

        painter.restore()

