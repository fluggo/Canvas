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

from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from fluggo import signal
from fluggo.media import process
from fluggo.media.basetypes import *

from fluggo import logging

_log = logging.getLogger(__name__)
_queue = process.VideoPullQueue()

class ThumbnailPainter(object):
    def __init__(self):
        self._thumbnails = []
        self._thumbnail_indexes = []
        self._thumbnail_width = 1.0
        self._thumbnail_count = 0
        self._stream = None
        self.updated = signal.Signal()
        self._length = 1
        self._offset = 0

    def set_stream(self, stream):
        self.clear()
        self._stream = stream
        self.updated(QtCore.QRectF())

    def set_length(self, length):
        # TODO: Really, we should work to preserve as many
        # thumbnails as we can
        self.clear()
        self._length = length
        self.updated(QtCore.QRectF())

    def set_offset(self, offset):
        # TODO: Really, we should work to preserve as many
        # thumbnails as we can
        self.clear()
        self._offset = offset
        self.updated(QtCore.QRectF())

    def set_rect(self, rect):
        self._rect = rect

        box = self._stream.format.thumbnail_box
        aspect = self._stream.format.pixel_aspect_ratio
        frame_count = self._length

        self._thumbnail_width = (rect.height() * float(box.width) * float(aspect)) / float(box.height)
        self._thumbnail_count = min(max(int(rect.width() / self._thumbnail_width), 1), frame_count)

        self._create_thumbnails()

    def clear(self):
        for frame in self._thumbnails:
            if hasattr(frame, 'cancel'):
                frame.cancel()

        self._thumbnails = []

    def get_thumbnail_rect(self, index):
        # "index" is relative to the start of the clip (not the stream)
        rect = self._rect

        if self._thumbnail_count == 1:
            return QtCore.QRect(rect.x() + (index * (rect.width() - self._thumbnail_width)),
                                rect.y(),
                                self._thumbnail_width, rect.height())
        else:
            return QtCore.QRect(rect.x() + (index * (rect.width() - self._thumbnail_width) / (self._thumbnail_count - 1)),
                                rect.y(),
                                self._thumbnail_width, rect.height())

    def _create_thumbnails(self):
        # Calculate how many thumbnails fit
        frame_count = self._length
        count = self._thumbnail_count

        if len(self._thumbnails) == count:
            return

        self.clear()
        self._thumbnails = [None for a in range(count)]

        if count == 1:
            self._thumbnail_indexes = [self._offset]
        else:
            self._thumbnail_indexes = [self._offset + int(float(a) * (frame_count - 1) / (count - 1)) for a in range(count)]

    def paint(self, painter, rect, clip_rect, transform):
        # Figure out which thumbnails belong here and paint them
        # The thumbnail lefts are at (i * (rect.width - thumbnail_width) / (len(thumbnails) - 1)) + rect.x()
        # Rights are at left + thumbnail_width
        self.set_rect(rect)
        stream = self._stream

        if stream:
            if not stream.format:
                _log.warning('Encountered stream with no format')
                return

            box = stream.format.thumbnail_box
            inverted_transform = transform.inverted()[0]

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
                try:
                    (thumbnails, i) = user_data

                    size = frame.current_window.size()
                    img_str = frame.to_argb32_bytes()

                    thumbnails[i] = QtGui.QImage(img_str, size.x, size.y, QtGui.QImage.Format_ARGB32_Premultiplied).copy()

                    self.updated(inverted_transform.mapRect(QtCore.QRectF(self.get_thumbnail_rect(i))))
                except:
                    _log.warning('Error in thumbnail callback', exc_info=True)

            for i in range(left_nail, right_nail):
                if not self._thumbnails[i]:
                    self._thumbnails[i] = _queue.enqueue(source=scale, frame_index=self._thumbnail_indexes[i],
                        window=stream.format.thumbnail_box,
                        callback=callback, user_data=(self._thumbnails, i))

                # TODO: Scale existing thumbnails to fit
                if isinstance(self._thumbnails[i], QtGui.QImage):
                    thumbnail_rect = self.get_thumbnail_rect(i)
                    painter.drawImage(thumbnail_rect, self._thumbnails[i])
        else:
            _log.debug('Thumbnail painter has no stream')
            # TODO: Show a slug or something?
            pass

