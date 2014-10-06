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

from PyQt5.QtCore import *
from PyQt5.QtGui import *
from fluggo import signal, sortlist
from fluggo.media import process
from fluggo.media.basetypes import *
from datetime import datetime

from fluggo import logging

_log = logging.getLogger(__name__)
_queue = process.VideoPullQueue()

_cache_by_time = sortlist.SortedList(keyfunc=lambda i: i.last_accessed, index_attr='time_index')
_cache_by_frame = {}

# Default 32M
_max_cache_size = 32 * 1024 * 1024
_current_cache_size = 0

class _CacheEntry:
    def __init__(self, stream_key, frame, image):
        self.frame_key = (stream_key, frame)
        self._image = None
        self.image = image
        self.time_index = None
        self.frame_index = None
        self.touch()

    def touch(self):
        self.last_accessed = datetime.utcnow()

    @property
    def image(self):
        return self._image

    @image.setter
    def image(self, value):
        global _current_cache_size

        if isinstance(self._image, QImage):
            _current_cache_size -= self._image.byteCount()

        self._image = value

        if isinstance(self._image, QImage):
            _current_cache_size += self._image.byteCount()

        trim_thumbnail_cache()

def trim_thumbnail_cache():
    while _current_cache_size > _max_cache_size:
        entry = _cache_by_time[0]
        del _cache_by_frame[entry.frame_key]
        del _cache_by_time[0]

        if isinstance(entry.image, QImage):
            current_cache_size -= entry.image.byteCount()

def _get_cached(stream_key, frame):
    entry = _cache_by_frame.get((stream_key, frame))

    if entry:
        entry.touch()
        _cache_by_time.move(entry.time_index)

    return entry

def _cache_frame(stream_key, frame, image):
    global _current_cache_size
    entry = _get_cached(stream_key, frame)

    if not entry:
        entry = _CacheEntry(stream_key, frame, image)
        _cache_by_frame[entry.frame_key] = entry
        _cache_by_time.add(entry)
    else:
        # Replace the image
        entry.image = image

    # Fix the size of the cache
    trim_thumbnail_cache()

    return entry

class ThumbnailPainter(object):
    def __init__(self):
        self._thumbnails = []
        self._thumbnail_indexes = []
        self._thumbnail_width = 1.0
        self._thumbnail_count = 0
        self._stream = None
        self._stream_key = None
        self.updated = signal.Signal()
        self._rect = None
        self._length = 1
        self._offset = 0

    def set_stream(self, stream_key, stream):
        self.clear()
        self._stream = stream
        self._stream_key = stream_key
        self.updated(QRectF())

    def set_length(self, length):
        # TODO: Really, we should work to preserve as many
        # thumbnails as we can
        self.clear()
        self._length = length
        self.updated(QRectF())

    def set_offset(self, offset):
        # TODO: Really, we should work to preserve as many
        # thumbnails as we can
        self.clear()
        self._offset = offset
        self.updated(QRectF())

    def set_rect(self, rect):
        if self._rect != rect or self._thumbnail_count == 0:
            self._rect = rect

            if self._stream and self._stream.format:
                box = self._stream.format.thumbnail_box
                aspect = self._stream.format.pixel_aspect_ratio
                frame_count = self._length

                self._thumbnail_width = (rect.height() * float(box.width) * float(aspect)) / float(box.height)
                self._thumbnail_count = min(max(int(rect.width() / self._thumbnail_width), 1), frame_count)

                self.clear()
            else:
                self._thumbnail_count = 0
                self.clear()

    def clear(self):
        self._thumbnail_indexes = []

        # Calculate how many thumbnails fit
        frame_count = self._length
        count = self._thumbnail_count

        if len(self._thumbnail_indexes) == count:
            return

        if count == 1:
            self._thumbnail_indexes = [self._offset]
        else:
            self._thumbnail_indexes = [self._offset + int(float(a) * (frame_count - 1) / (count - 1)) for a in range(count)]

    def get_thumbnail_rect(self, index):
        # "index" is relative to the start of the clip (not the stream)
        rect = self._rect

        if self._thumbnail_count == 1:
            return QRect(rect.x() + (index * (rect.width() - self._thumbnail_width)),
                         rect.y(),
                         self._thumbnail_width, rect.height())
        else:
            return QRect(rect.x() + (index * (rect.width() - self._thumbnail_width) / (self._thumbnail_count - 1)),
                         rect.y(),
                         self._thumbnail_width, rect.height())

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
                (self._thumbnail_count - 1) / (rect.width() - self._thumbnail_width))
            right_nail = int((clip_rect.x() + clip_rect.width() - rect.x()) *
                (self._thumbnail_count - 1) / (rect.width() - self._thumbnail_width)) + 1
            left_nail = max(0, left_nail)
            right_nail = min(self._thumbnail_count, right_nail)

            scale = process.VideoScaler(stream,
                target_point=v2f(0, 0), source_point=box.min,
                scale_factors=v2f(rect.height() * float(stream.format.pixel_aspect_ratio) / box.height,
                    rect.height() / box.height),
                source_rect=box)

            def callback(frame_index, frame, user_data):
                try:
                    (entry, i) = user_data

                    size = frame.current_window.size()
                    img_str = frame.to_argb32_bytes()

                    entry.image = QImage(img_str, size.x, size.y, QImage.Format_ARGB32_Premultiplied).copy()

                    self.updated(inverted_transform.mapRect(QRectF(self.get_thumbnail_rect(i))))
                except:
                    _log.warning('Error in thumbnail callback', exc_info=True)

            for i in range(left_nail, right_nail):
                # TODO: If two people go after the same thumbnail, it might not
                # paint right (one will get notification the thumbnail has arrived,
                # the other won't)
                frame_index = self._thumbnail_indexes[i]
                entry = _get_cached(self._stream_key, frame_index)

                if not entry:
                    entry = _cache_frame(self._stream_key, frame_index, None)

                    entry.image = _queue.enqueue(source=scale, frame_index=frame_index,
                        window=stream.format.thumbnail_box,
                        callback=callback, user_data=(entry, i))

                # TODO: Scale existing thumbnails to fit
                if isinstance(entry.image, QImage):
                    thumbnail_rect = self.get_thumbnail_rect(i)
                    painter.drawImage(thumbnail_rect, entry.image)
        else:
            _log.debug('Thumbnail painter has no stream')
            # TODO: Show a slug or something?
            pass

