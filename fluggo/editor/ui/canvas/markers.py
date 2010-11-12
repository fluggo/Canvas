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

from PyQt4.QtCore import *
from PyQt4.QtGui import *

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

