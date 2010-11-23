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

from fluggo.media import process
from PyQt4 import QtCore, QtGui

#import fractions, math
#from PyQt4.QtCore import *
#from PyQt4.QtGui import *
#from fluggo import sortlist, signal
#from fluggo.media import process, timecode, sources
#from fluggo.editor import canvas
#from fluggo.media.basetypes import *
#from . import ruler

def _rectf_to_rect(rect):
    '''Finds the smallest QRect enclosing the given QRectF.'''
    return QtCore.QRect(
        math.floor(rect.x()),
        math.floor(rect.y()),
        math.ceil(rect.width() + rect.x() - math.floor(rect.x())),
        math.ceil(rect.height() + rect.y() - math.floor(rect.y())))

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
            if abs(screen_pos.x() - self.drag_start_screen_pos.x()) >= QtGui.QApplication.startDragDistance() or \
                    abs(screen_pos.y() - self.drag_start_screen_pos.y()) >= QtGui.QApplication.startDragDistance():
                self.drag_active = True
                self.drag_start(view)
            else:
                return

        self.drag_move(view, pos, pos - self.drag_start_pos)

from .clip import *

class VideoItem(ClipItem):
    def __init__(self, item, name):
        ClipItem.__init__(self, item, name)
        self._thumbnail_painter = ThumbnailPainter()
        self._thumbnail_painter.updated.connect(self._handle_thumbnails_updated)
        self._thumbnail_painter.set_width(self.item.width)

    @property
    def units_per_second(self):
        return float(self.scene().frame_rate)

    def added_to_scene(self):
        ClipItem.added_to_scene(self)
        self._thumbnail_painter.set_stream(self.stream)

    def _handle_thumbnails_updated(self):
        self.update()

    def _update(self, **kw):
        '''
        Called by the item model to update our appearance.
        '''
        # Changes requiring a reset of the thumbnails
        # TODO: This resets thumbnails *way* more than is necessary
        if 'width' in kw:
            self._thumbnail_painter.set_width(self.item.width)

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

class VideoSequnce(VideoItem):
    def __init__(self, sequence):
        VideoItem.__init__(self, sequence, None)

    @property
    def stream(self):
        return None

class PlaceholderItem(QtGui.QGraphicsItem):
    def __init__(self, source_name, stream_format, x, y, height):
        QtGui.QGraphicsItem.__init__(self)

        self.source_name = source_name
        self.stream_format = stream_format
        self.height = height
        self.width = self.stream_format.adjusted_length
        self.setPos(x, y)

    def boundingRect(self):
        return QtCore.QRectF(0.0, 0.0, self.width, self.height)

    def paint(self, painter, option, widget):
        WIDTH = 3.0
        rect = painter.transform().mapRect(self.boundingRect())
        clip_rect = painter.transform().mapRect(option.exposedRect)

        painter.save()
        painter.resetTransform()

        painter.setPen(QtGui.QPen(QtGui.QColor.fromRgbF(1.0, 1.0, 1.0), WIDTH))
        painter.drawRect(QtCore.QRectF(rect.x() + WIDTH/2, rect.y() + WIDTH/2, rect.width() - WIDTH, rect.height() - WIDTH))

        painter.restore()

from .view import *
from .thumbnails import ThumbnailPainter

