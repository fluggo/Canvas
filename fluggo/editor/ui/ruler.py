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
from fluggo import signal

SMALL_TICK_THRESHOLD = 2

class TimeRuler(QWidget):
    def __init__(self, parent=None, timecode=timecode.Frames(), scale=fractions.Fraction(1), frame_rate=fractions.Fraction(30, 1)):
        QWidget.__init__(self, parent)
        self.frame_rate = fractions.Fraction(frame_rate)
        self.set_timecode(timecode)
        self.set_scale(scale)
        self.left_frame = 0.0
        self.current_frame = 0
        self.current_frame_changed = signal.Signal()

    def sizeHint(self):
        return QSize(60, 30)

    def set_left_frame(self, left_frame):
        if left_frame != self.left_frame:
            self.left_frame = left_frame
            self.update()

    def set_current_frame(self, frame):
        frame = int(frame)

        if self.current_frame != frame:
            self.current_frame = frame
            self.current_frame_changed(frame)
            self.update()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            frame = int(round(float(fractions.Fraction(event.x()) / self.scale) + self.left_frame))
            self.set_current_frame(frame)

    def mouseMoveEvent(self, event):
        frame = int(round(float(fractions.Fraction(event.x()) / self.scale) + self.left_frame))
        self.set_current_frame(frame)

    def scale(self):
        return self.scale

    def set_scale(self, scale):
        '''
        Set the scale, in pixels per frame.

        '''
        self.scale = fractions.Fraction(scale)

        if len(self.ticks) < 3:
            self.minor_tick = None
            self.medium_tick = self.ticks[0]
            self.major_tick = self.ticks[-1]
        else:
            for minor, medium, major in zip(self.ticks[0:], self.ticks[1:], self.ticks[2:]):
                if fractions.Fraction(minor) * scale > SMALL_TICK_THRESHOLD:
                    self.minor_tick, self.medium_tick, self.major_tick = minor, medium, major
                    break

        self.update()

    def set_timecode(self, timecode):
        self.timecode = timecode
        major_ticks = self.timecode.get_major_ticks()

        # Expand the major tick list with extra divisions
        last_tick = 1
        self.ticks = [1]

        for major_tick in major_ticks:
            for div in (10, 2):
                (divend, rem) = divmod(major_tick, div)

                if rem == 0 and divend > last_tick:
                    self.ticks.append(divend)

        self.update()

    def frame_to_pixel(self, frame):
        return float(int((float(int(frame)) - self.left_frame) * float(self.scale))) + 0.5

    def paintEvent(self, event):
        paint = QPainter(self)

        paint.setPen(QColor(0, 0, 0))

        major_ticks = self.timecode.get_major_ticks()

        start_frame = int(self.left_frame)
        width_frames = int(float(fractions.Fraction(self.width()) / self.scale))
        height = self.height()

        if self.minor_tick:
            for frame in range(start_frame - start_frame % self.minor_tick, start_frame + width_frames, self.minor_tick):
                x = self.frame_to_pixel(frame)
                paint.drawLine(x, height - 5, x, height)

        for frame in range(start_frame - start_frame % self.medium_tick, start_frame + width_frames, self.medium_tick):
            x = self.frame_to_pixel(frame)
            paint.drawLine(x, height - 10, x, height)

        for frame in range(start_frame - start_frame % self.major_tick, start_frame + width_frames, self.major_tick):
            x = self.frame_to_pixel(frame)
            paint.drawLine(x, height - 15, x, height)

        prev_right = None

        for frame in range(start_frame - start_frame % self.major_tick, start_frame + width_frames, self.major_tick):
            x = self.frame_to_pixel(frame)

            if prev_right is None or x > prev_right:
                text = self.timecode.format(frame)
                rect = paint.drawText(QRectF(), Qt.TextSingleLine, text)

                prev_right = x + rect.width() + 5.0
                paint.drawText(x + 2.5, 0.0, rect.width(), rect.height(), Qt.TextSingleLine, text)

        # Draw the pointer
        x = self.frame_to_pixel(self.current_frame)

        paint.setPen(Qt.NoPen)
        paint.setBrush(QColor.fromRgbF(1.0, 0.0, 0.0))
        paint.drawConvexPolygon(QPoint(x, height), QPoint(x + 5, height - 15), QPoint(x - 5, height - 15))

