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

class Scene(QGraphicsScene):
    frame_range_changed = pyqtSignal(int, int, name='frameRangeChanged')

    def __init__(self):
        QGraphicsScene.__init__(self)

    def update_frames(self, min_frame, max_frame):
        self.frame_range_changed.emit(min_frame, max_frame)

class View(QGraphicsView):
    black_pen = QPen(QColor.fromRgbF(0.0, 0.0, 0.0))
    white_pen = QPen(QColor.fromRgbF(1.0, 1.0, 1.0))

    def __init__(self, scene, clock):
        QGraphicsView.__init__(self, scene)
        self.setViewportMargins(0, 30, 0, 0)
        self.setAlignment(Qt.AlignLeft | Qt.AlignTop)

        self.ruler = ruler.TimeRuler(self, timecode=timecode.NtscDropFrame())
        self.ruler.move(self.frameWidth(), self.frameWidth())

        self.clock = clock
        self.frame_rate = fractions.Fraction(24000, 1001)

        self.white = False
        self.frame = 0
        self.set_current_frame(0)
        self.startTimer(1000)

        scene.sceneRectChanged.connect(self.handle_scene_rect_changed)
        scene.frame_range_changed.connect(self.handle_update_frames)
        self.ruler.current_frame_changed.connect(self.handle_ruler_current_frame_changed)

        self.scale_x = fractions.Fraction(1)
        self.scale_y = fractions.Fraction(1)

        self.scale(4, 1)

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

    def resizeEvent(self, event):
        self.ruler.resize(self.width() - self.frameWidth(), 30)

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
        self.white = not self.white
        self._invalidate_marker(self.frame)

    def drawForeground(self, painter, rect):
        '''
        Draws the marker in the foreground.
        '''
        QGraphicsView.drawForeground(self, painter, rect)

        painter.setPen(self.white_pen if self.white else self.black_pen)
        painter.drawLine(self.frame, rect.y(), self.frame, rect.y() + rect.height())

class VideoItem(QGraphicsItem):
    def __init__(self, item, name):
        # BJC: This class currently has both the model and the view,
        # so it will need to be split
        QGraphicsItem.__init__(self)
        self.height = 40.0
        self._y = 0.0
        self.item = item
        self.name = name
        self.setPos(self.item.x, self._y)
        self.setFlags(QGraphicsItem.ItemIsMovable | QGraphicsItem.ItemIsSelectable |
            QGraphicsItem.ItemUsesExtendedStyleOption)
        self.thumbnails = []
        self.thumbnail_indexes = []
        self.thumbnail_width = 1.0

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

        for i in range(left_nail, right_nail):
            # Later we'll delegate this to another thread
            if not self.thumbnails[i]:
                frame = scale.get_frame_f16(
                    self.thumbnail_indexes[i], self.item.source.thumbnail_box)
                size = frame.current_data_window.size()
                img_str = frame.to_argb32_string()

                self.thumbnails[i] = QImage(img_str, size.x, size.y, QImage.Format_ARGB32_Premultiplied).copy()

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

