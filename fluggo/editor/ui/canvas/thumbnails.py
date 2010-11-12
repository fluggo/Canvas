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
from fluggo import signal
from fluggo.media import process
from fluggo.media.basetypes import *

_queue = process.VideoPullQueue()

class ThumbnailPainter(object):
    def __init__(self):
        self._thumbnails = []
        self._thumbnail_indexes = []
        self._thumbnail_width = 1.0
        self._stream = None
        self.updated = signal.Signal()
        self._length = 1

    def set_stream(self, stream):
        self.clear()
        self._stream = stream

    def set_width(self, length):
        # TODO: Really, we should work to preserve as many
        # thumbnails as we can
        self.clear()
        self._length = length

    def clear(self):
        for frame in self._thumbnails:
            if hasattr(frame, 'cancel'):
                frame.cancel()

        self._thumbnails = []

    def _create_thumbnails(self, total_width, height):
        # Calculate how many thumbnails fit
        box = self._stream.format.thumbnail_box
        aspect = self._stream.format.pixel_aspect_ratio
        frame_count = self._length

        self._thumbnail_width = (height * float(box.width) * float(aspect)) / float(box.height)
        count = min(max(int(total_width / self._thumbnail_width), 1), frame_count)

        if len(self._thumbnails) == count:
            return

        self.clear()
        self._thumbnails = [None for a in range(count)]

        if count == 1:
            self._thumbnail_indexes = [0]
        else:
            self._thumbnail_indexes = [0 + int(float(a) * frame_count / (count - 1)) for a in range(count)]

    def paint(self, painter, rect, clip_rect):
        # Figure out which thumbnails belong here and paint them
        # The thumbnail lefts are at (i * (rect.width - thumbnail_width) / (len(thumbnails) - 1)) + rect.x()
        # Rights are at left + thumbnail_width
        stream = self._stream

        if stream:
            self._create_thumbnails(rect.width(), rect.height())
            box = stream.format.thumbnail_box

            left_nail = int((clip_rect.x() - self._thumbnail_width - rect.x()) *
                (len(self._thumbnails) - 1) / (rect.width() - self._thumbnail_width))
            right_nail = int((clip_rect.x() + clip_rect.width() - rect.x()) *
                (len(self._thumbnails) - 1) / (rect.width() - self._thumbnail_width)) + 1
            left_nail = max(0, left_nail)
            right_nail = min(len(self._thumbnails), right_nail)

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
                self.updated()

            for i in range(left_nail, right_nail):
                if not self._thumbnails[i]:
                    self._thumbnails[i] = _queue.enqueue(source=scale, frame_index=self._thumbnail_indexes[i],
                        window=stream.format.thumbnail_box,
                        callback=callback, user_data=(self._thumbnails, i))

                # TODO: Scale existing thumbnails to fit
                if isinstance(self._thumbnails[i], QImage):
                    if len(self._thumbnails) == 1:
                        painter.drawImage(rect.x() + (i * (rect.width() - self._thumbnail_width)),
                            rect.y(), self._thumbnails[i])
                    else:
                        painter.drawImage(rect.x() + (i * (rect.width() - self._thumbnail_width) / (len(self._thumbnails) - 1)),
                            rect.y(), self._thumbnails[i])
        else:
            # TODO: Show a slug or something?
            pass

