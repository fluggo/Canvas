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

from .scene import *
from ..ruler import TimeRuler
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from fluggo.media import timecode, process

class RulerView(QtGui.QWidget):
    '''This is a view combined with a ruler separately. It's a workaround for
    a bug in whatever version of Qt is running on Ubuntu 10.04. Qt ignores the
    viewport margins for determining where a drag operation has hit the scene;
    it works just fine for normal mouse ops.'''
    def __init__(self, clock):
        QtGui.QWidget.__init__(self)
        self.vbox = QtGui.QVBoxLayout(self)
        self.vbox.setStretch(1, 1)
        self.vbox.setSpacing(0)

        width = self.style().pixelMetric(QtGui.QStyle.PM_DefaultFrameWidth)
        self.vbox.setContentsMargins(width, 0, width, 0)

        self.ruler = TimeRuler(self, timecode=timecode.NtscDropFrame())
        self.view = View(clock, ruler=self.ruler)
        self.view.setFrameShape(QtGui.QFrame.NoFrame)

        self.vbox.addWidget(self.ruler)
        self.vbox.addWidget(self.view)
        self.setLayout(self.vbox)

    def __getattr__(self, name):
        # Pass on to the view
        return getattr(self.view, name)

class View(QtGui.QGraphicsView):
    black_pen = QtGui.QPen(QtGui.QColor.fromRgbF(0.0, 0.0, 0.0))
    white_pen = QtGui.QPen(QtGui.QColor.fromRgbF(1.0, 1.0, 1.0))
    handle_width = 10.0
    snap_marker_color = QtGui.QColor.fromRgbF(0.0, 1.0, 0.0)
    snap_marker_width = 5.0
    snap_distance = 8.0
    max_zoom_x = 100000.0
    min_zoom_x = 0.01

    def __init__(self, clock, ruler=None):
        QtGui.QGraphicsView.__init__(self)
        self.setAlignment(Qt.AlignLeft | Qt.AlignTop)
        self.setViewportUpdateMode(self.FullViewportUpdate)
        self.setResizeAnchor(self.AnchorUnderMouse)
        self.setTransformationAnchor(self.AnchorUnderMouse)

        self.ruler = ruler

        if not self.ruler:
            self.setViewportMargins(0, 30, 0, 0)
            self.ruler = TimeRuler(self, timecode=timecode.NtscDropFrame())
            self.ruler.move(self.frameWidth(), self.frameWidth())

        # A warning: clock and clock_callback_handle will create a pointer cycle here,
        # which probably won't be freed unless the callback handle is explicitly
        # destroyed with self.clock_callback_handle.unregister() and self.clock = None
        self.playback_timer = None
        self.clock = clock
        self.clock_callback_handle = self.clock.register_callback(self._clock_changed, None)
        self.clock_frame = 0

        self.white = False
        self.frame = 0
        self.blink_timer = self.startTimer(1000)

        self.ruler.current_frame_changed.connect(self.handle_ruler_current_frame_changed)

    def _clock_changed(self, speed, time, data):
        if speed.numerator and self.playback_timer is None:
            self.playback_timer = self.startTimer(20)
        elif not speed.numerator and self.playback_timer is not None:
            self.killTimer(self.playback_timer)
            self.playback_timer = None

        self._update_clock_frame(time)

    def set_space(self, space, source_list, undo_stack):
        if not space:
            self.setScene(None)
            return

        self.setScene(Scene(space, source_list, undo_stack))

        self._reset_ruler_scroll()
        self.set_current_frame(0)

        self.scene().sceneRectChanged.connect(self.handle_scene_rect_changed)
        self.scene().marker_added.connect(self._handle_marker_changed)
        self.scene().marker_removed.connect(self._handle_marker_changed)

        self.scale(4 * 24, 1)

    def selected_model_items(self):
        return self.scene().selected_model_items()

    def scale(self, sx, sy):
        self.scale_x = fractions.Fraction(sx)
        self.scale_y = fractions.Fraction(sy)

        self.ruler.set_scale(sx / self.scene().frame_rate)

        self.setTransform(QtGui.QTransform.fromScale(float(sx), float(sy)))
        self._reset_ruler_scroll()
        self.scene().update_view_decorations(self)

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

            self.scale(self.scale_x * factor, self.scale_y)
        else:
            factor = 2 ** (-event.delta() / 120)

            if self.scale_x / factor < self.min_zoom_x:
                return

            self.scale(self.scale_x / factor, self.scale_y)

    def handle_scene_rect_changed(self, rect):
        self._reset_ruler_scroll()

    def handle_ruler_current_frame_changed(self, frame):
        self.set_current_frame(frame)

    def updateSceneRect(self, rect):
        QGraphicsView.updateSceneRect(self, rect)
        self._reset_ruler_scroll()

    def scrollContentsBy(self, dx, dy):
        QtGui.QGraphicsView.scrollContentsBy(self, dx, dy)

        if dx and self.scene():
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

        self.updateScene([QtCore.QRectF(top, bottom)])

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
        QtGui.QGraphicsView.drawForeground(self, painter, rect)

        # Clock frame line
        x = self.clock_frame / float(self.scene().frame_rate)
        painter.setPen(self.black_pen)
        painter.drawLine(QtCore.QPointF(x, rect.y()), QtCore.QPointF(x, rect.y() + rect.height()))

        # Current frame line, which blinks
        x = self.frame / float(self.scene().frame_rate)
        painter.setPen(self.white_pen if self.white else self.black_pen)
        painter.drawLine(QtCore.QPointF(x, rect.y()), QtCore.QPointF(x, rect.y() + rect.height()))

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

        items = self.items(QtCore.QRect(top.x() - self.snap_distance, top.y(), self.snap_distance * 2, bottom.y() - top.y()), Qt.IntersectsItemBoundingRect)

        # TODO: Find something more generic than video items
        items = [a for a in items if isinstance(a, ClipItem) and a is not item]

        # Transform the snap_distance into time units
        distance = self.viewportTransform().inverted()[0].mapRect(QtCore.QRectF(0.0, 0.0, self.snap_distance, 1.0)).width()
        x = None

        #if distance < 1.0:
        #    distance = 1.0

        for item in items:
            if abs(item.item.x / item.units_per_second - time) < distance:
                x = item.item.x / item.units_per_second
                distance = abs(x - time)

            if abs((item.item.x + item.item.length) / item.units_per_second - time) < distance:
                x = (item.item.x + item.item.length) / item.units_per_second
                distance = abs(x - time)

        return x

