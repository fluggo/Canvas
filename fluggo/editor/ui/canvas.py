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

import fractions
from PyQt4.QtCore import *
from PyQt4.QtGui import *
from fluggo.media import process, timecode
from fluggo.media.basetypes import *
from . import ruler

_queue = process.VideoPullQueue()

class Scene(QGraphicsScene):
    frame_range_changed = pyqtSignal(int, int, name='frameRangeChanged')

    def update_frames(self, min_frame, max_frame):
        self.frame_range_changed.emit(min_frame, max_frame)

class View(QGraphicsView):
    black_pen = QPen(QColor.fromRgbF(0.0, 0.0, 0.0))
    white_pen = QPen(QColor.fromRgbF(1.0, 1.0, 1.0))
    handle_width = 10.0

    def __init__(self, clock):
        QGraphicsView.__init__(self)
        self.setScene(Scene())
        self.setViewportMargins(0, 30, 0, 0)
        self.setAlignment(Qt.AlignLeft | Qt.AlignTop)

        self.ruler = ruler.TimeRuler(self, timecode=timecode.NtscDropFrame())
        self.ruler.move(self.frameWidth(), self.frameWidth())

        # A warning: clock and clock_callback_handle will create a pointer cycle here,
        # which probably won't be freed unless the callback handle is explicitly
        # destroyed with self.clock_callback_handle.unregister() and self.clock = None
        self.playback_timer = None
        self.clock = clock
        self.clock_callback_handle = self.clock.register_callback(self._clock_changed, None)
        self.clock_frame = 0

        self.frame_rate = fractions.Fraction(24000, 1001)

        self.white = False
        self.frame = 0
        self.set_current_frame(0)
        self.blink_timer = self.startTimer(1000)

        self.scene().sceneRectChanged.connect(self.handle_scene_rect_changed)
        self.scene().frame_range_changed.connect(self.handle_update_frames)
        self.ruler.current_frame_changed.connect(self.handle_ruler_current_frame_changed)

        self.scale_x = fractions.Fraction(1)
        self.scale_y = fractions.Fraction(1)

        self.scale(4, 1)

    def _clock_changed(self, speed, time, data):
        if speed.numerator and self.playback_timer is None:
            self.playback_timer = self.startTimer(20)
        elif not speed.numerator and self.playback_timer is not None:
            self.killTimer(self.playback_timer)
            self.playback_timer = None

        self._update_clock_frame(time)

    def scale(self, sx, sy):
        self.scale_x = fractions.Fraction(sx)
        self.scale_y = fractions.Fraction(sy)

        self.ruler.set_scale(sx)

        self.resetTransform()
        QGraphicsView.scale(self, float(sx), float(sy))

    def set_current_frame(self, frame):
        '''
        view.set_current_frame(frame)

        Moves the view's current frame marker.
        '''
        self._invalidate_marker(self.frame)
        self.frame = frame
        self._invalidate_marker(frame)

        self.ruler.set_current_frame(frame)
        self.clock.seek(process.get_frame_time(self.frame_rate, int(frame)))

    def _update_clock_frame(self, time=None):
        if not time:
            time = self.clock.get_presentation_time()

        frame = process.get_time_frame(self.frame_rate, time)
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
            self.scale(self.scale_x * factor, self.scale_y)
        else:
            factor = 2 ** (-event.delta() / 120)
            self.scale(self.scale_x / factor, self.scale_y)

    def handle_scene_rect_changed(self, rect):
        left = self.mapToScene(0, 0).x()
        self.ruler.set_left_frame(left)

    def handle_update_frames(self, min_frame, max_frame):
        # If the current frame was in this set, re-seek to it
        # TODO: Verify that the clock isn't playing first
        if self.frame >= min_frame and self.frame <= max_frame:
            self.clock.seek(process.get_frame_time(self.frame_rate, int(self.frame)))

    def handle_ruler_current_frame_changed(self, frame):
        self.set_current_frame(frame)

    def updateSceneRect(self, rect):
        QGraphicsView.updateSceneRect(self, rect)

        left = self.mapToScene(0, 0).x()
        self.ruler.setLeftFrame(left)

    def scrollContentsBy(self, dx, dy):
        QGraphicsView.scrollContentsBy(self, dx, dy)

        if dx:
            left = self.mapToScene(0, 0).x()
            self.ruler.set_left_frame(left)

    def _invalidate_marker(self, frame):
        # BJC: No, for some reason, invalidateScene() did not work here
        self.scene().invalidate(QRectF(frame - 0.5, -20000.0, 1.0, 40000.0), QGraphicsScene.ForegroundLayer)

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
        painter.setPen(self.black_pen)
        painter.drawLine(self.clock_frame, rect.y(), self.clock_frame, rect.y() + rect.height())

        # Current frame line, which blinks
        painter.setPen(self.white_pen if self.white else self.black_pen)
        painter.drawLine(self.frame, rect.y(), self.frame, rect.y() + rect.height())

class Draggable(object):
    DRAG_START_DISTANCE = 2

    def __init__(self):
        self.drag_active = False
        self.drag_down = False
        self.drag_start_pos = None
        self.drag_start_screen_pos = None

    def drag_start(self):
        pass

    def drag_move(self, abs_pos, rel_pos):
        pass

    def drag_end(self, abs_pos, rel_pos):
        pass

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.drag_down = True
            self.drag_start_pos = event.scenePos()
            self.drag_start_screen_pos = event.screenPos()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            if self.drag_active:
                pos = event.scenePos()
                self.drag_end(pos, pos - self.drag_start_pos)
                self.drag_active = False

            self.drag_down = False

    def mouseMoveEvent(self, event):
        if not self.drag_down:
            return

        pos = event.scenePos()
        screen_pos = event.screenPos()

        if not self.drag_active:
            if abs(screen_pos.x() - self.drag_start_screen_pos.x()) >= self.DRAG_START_DISTANCE or \
                    abs(screen_pos.y() - self.drag_start_screen_pos.y()) >= self.DRAG_START_DISTANCE:
                self.drag_active = True
                self.drag_start()
            else:
                return

        self.drag_move(pos, pos - self.drag_start_pos)

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

    def drag_start(self):
        self.original_x = int(self.parentItem().pos().x())
        self.original_width = self.parentItem().item.width
        self.original_offset = self.parentItem().item.offset
        self.original_y = self.parentItem().pos().y()
        self.original_height = self.parentItem().height

    def hoverEnterEvent(self, event):
        self.setBrush(self.brush)

    def hoverLeaveEvent(self, event):
        self.setBrush(self.invisibrush)

class _LeftHandle(_Handle):
    def drag_move(self, abs_pos, rel_pos):
        x = int(rel_pos.x())

        if self.original_width > x:
            self.parentItem()._update(x=self.original_x + x, width=self.original_width - x,
                offset=self.original_offset + x)
        else:
            self.parentItem()._update(x=self.original_x + self.original_width - 1, width=1,
                offset=self.original_offset + self.original_width - 1)

class _RightHandle(_Handle):
    def drag_move(self, abs_pos, rel_pos):
        x = int(rel_pos.x())

        if self.original_width > -x:
            self.parentItem()._update(width=self.original_width + x)
        else:
            self.parentItem()._update(width=1)

class _TopHandle(_Handle):
    horizontal = False

    def drag_move(self, abs_pos, rel_pos):
        y = rel_pos.y()

        if self.original_height > y:
            self.parentItem()._update(y=self.original_y + y, height=self.original_height - y)
        else:
            self.parentItem()._update(y=self.original_y + self.original_height - 1, height=1)

class _BottomHandle(_Handle):
    horizontal = False

    def drag_move(self, abs_pos, rel_pos):
        y = rel_pos.y()

        if self.original_height > -y:
            self.parentItem()._update(height=self.original_height + y)
        else:
            self.parentItem()._update(height=1)

class VideoItem(QGraphicsItem):
    def __init__(self, item, name):
        # BJC: This class currently has both the model and the view,
        # so it will need to be split
        QGraphicsItem.__init__(self)
        self.height = 40.0
        self.item = item
        self.name = name
        self.setPos(self.item.x, 0.0)
        self.setFlags(QGraphicsItem.ItemIsMovable | QGraphicsItem.ItemIsSelectable |
            QGraphicsItem.ItemUsesExtendedStyleOption)
        self.setAcceptHoverEvents(True)
        self.thumbnails = []
        self.thumbnail_indexes = []
        self.thumbnail_width = 1.0

        self.left_handle = _LeftHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.right_handle = _RightHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.right_handle.setPos(self.item.width, 0.0)
        self.top_handle = _TopHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.bottom_handle = _BottomHandle(QRectF(0.0, 0.0, 0.0, 0.0), self)
        self.bottom_handle.setPos(0.0, self.height)

    def view_scale_changed(self, view):
        # BJC I tried to keep it view-independent, but the handles need to have different sizes
        # depending on the level of zoom in the view (not to mention separate sets of thumbnails)
        hx = view.handle_width / float(view.scale_x)
        hy = view.handle_width / float(view.scale_y)

        self.left_handle.setRect(QRectF(0.0, 0.0, hx, self.height))
        self.right_handle.setRect(QRectF(-hx, 0.0, hx, self.height))
        self.top_handle.setRect(QRectF(0.0, 0.0, self.item.width, hy))
        self.bottom_handle.setRect(QRectF(0.0, -hy, self.item.width, hy))

    def hoverEnterEvent(self, event):
        view = event.widget().parentWidget()
        self.view_scale_changed(view)

    def _update(self, **kw):
        '''
        Called by handles to update the item's properties all at once.
        '''
        # Changes in item position
        pos = self.pos()

        if 'x' in kw or 'y' in kw:
            self.setPos(kw.get('x', pos.x()), kw.get('y', pos.y()))

        # Changes to the underlying workspace item
        if 'x' in kw or 'width' in kw or 'offset' in kw:
            old_x, old_width, old_offset = self.item.x, self.item.width, self.item.offset
            new_x, new_width, new_offset = kw.get('x', old_x), kw.get('width', old_width), kw.get('offset', old_offset)
            old_right, new_right = old_x + old_width, new_x + new_width

            self.item.update(x=kw.get('x', self.item.x),
                width=kw.get('width', self.item.width),
                offset=kw.get('offset', self.item.offset))

            for frame in self.thumbnails:
                if hasattr(frame, 'cancel'):
                    frame.cancel()

            self.thumbnails = []

            # Update the currently displayed frame if it's in a changed region
            if old_x != new_x:
                self.scene().update_frames(min(old_x, new_x), max(old_x, new_x) - 1)

            if old_right != new_right:
                self.scene().update_frames(min(old_right, new_right), max(old_right, new_right) - 1)

            if old_x - old_offset != new_x - new_offset:
                self.scene().update_frames(max(old_x, new_x), min(old_right, new_right) - 1)

        # Changes in item size
        if 'width' in kw or 'height' in kw:
            self.height = kw.get('height', self.height)

            self.right_handle.setPos(self.item.width, 0.0)
            self.bottom_handle.setPos(0.0, self.height)

            self.prepareGeometryChange()

            if 'height' in kw:
                for frame in self.thumbnails:
                    if hasattr(frame, 'cancel'):
                        frame.cancel()

                self.thumbnails = []

    def _create_thumbnails(self, total_width):
        # Calculate how many thumbnails fit
        box = self.item.source.thumbnail_box
        aspect = self.item.source.pixel_aspect_ratio
        start_frame = self.item.offset
        frame_count = self.item.width

        self.thumbnail_width = (self.height * float(box.width()) * float(aspect)) / float(box.height())
        count = min(max(int(total_width / self.thumbnail_width), 1), frame_count)

        if len(self.thumbnails) == count:
            return

        self.thumbnails = [None for a in range(count)]

        if count == 1:
            self.thumbnail_indexes = [start_frame]
        else:
            self.thumbnail_indexes = [start_frame + int(float(a) * frame_count / (count - 1)) for a in range(count)]

    def boundingRect(self):
        return QRectF(0.0, 0.0, self.item.width, self.height)

    def paint(self, painter, option, widget):
        rect = painter.transform().mapRect(self.boundingRect())
        clip_rect = painter.transform().mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.fillRect(rect, QColor.fromRgbF(1.0, 0, 0) if self.isSelected() else QColor.fromRgbF(0.9, 0.9, 0.8))

        painter.setBrush(QColor.fromRgbF(0.0, 0.0, 0.0))
        painter.drawText(rect, Qt.TextSingleLine, self.name)

        # Figure out which thumbnails belong here and paint them
        # The thumbnail lefts are at (i * (rect.width - thumbnail_width) / (len(thumbnails) - 1)) + rect.x()
        # Rights are at left + thumbnail_width
        self._create_thumbnails(rect.width())

        box = self.item.source.thumbnail_box

        left_nail = int((clip_rect.x() - self.thumbnail_width - rect.x()) *
            (len(self.thumbnails) - 1) / (rect.width() - self.thumbnail_width))
        right_nail = int((clip_rect.x() + clip_rect.width() - rect.x()) *
            (len(self.thumbnails) - 1) / (rect.width() - self.thumbnail_width)) + 1
        left_nail = max(0, left_nail)
        right_nail = min(len(self.thumbnails), right_nail)

        scale = process.VideoScaler(self.item.source,
            target_point=v2f(0, 0), source_point=box.min,
            scale_factors=v2f(rect.height() * float(self.item.source.pixel_aspect_ratio) / box.height(),
                rect.height() / box.height()),
            source_rect=box)

        def callback(frame_index, frame, user_data):
            (thumbnails, i) = user_data

            size = frame.current_data_window.size()
            img_str = frame.to_argb32_string()

            thumbnails[i] = QImage(img_str, size.x, size.y, QImage.Format_ARGB32_Premultiplied).copy()

            # TODO: limit to thumbnail's area
            self.update()

        for i in range(left_nail, right_nail):
            # Later we'll delegate this to another thread
            if not self.thumbnails[i]:
                self.thumbnails[i] = _queue.enqueue(source=scale, frame_index=self.thumbnail_indexes[i],
                    window=self.item.source.thumbnail_box,
                    callback=callback, user_data=(self.thumbnails, i))

            # TODO: Scale existing thumbnails to fit (removing last thumbnails = [] in _update)
            if isinstance(self.thumbnails[i], QImage):
                if len(self.thumbnails) == 1:
                    painter.drawImage(rect.x() + (i * (rect.width() - self.thumbnail_width)),
                        rect.y(), self.thumbnails[i])
                else:
                    painter.drawImage(rect.x() + (i * (rect.width() - self.thumbnail_width) / (len(self.thumbnails) - 1)),
                        rect.y(), self.thumbnails[i])

        painter.restore()

    def mouseMoveEvent(self, event):
        # There's a drag operation of some kind going on
        old_x = self.pos().x()

        QGraphicsItem.mouseMoveEvent(self, event)

        pos = self.pos()
        pos.setX(round(pos.x()))
        self.setPos(pos)

        self.scene().update_frames(min(old_x, pos.x()), max(old_x, pos.x()) + self.item.width - 1)
        self.item.update(x=int(pos.x()))

