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

from ..canvas import *
from .markers import *
from fluggo.editor import model
from fluggo.media import sources
from fluggo.editor import graph
from PyQt4 import QtCore, QtGui
from PyQt4.QtCore import Qt
from .thumbnails import ThumbnailPainter

class _ItemLeftController(Controller1D):
    def __init__(self, owner):
        self.owner = owner

class _ItemRightController(Controller1D):
    def __init__(self, owner):
        self.owner = owner

class _SequenceItem(VideoItem):
    def __init__(self, item, parent):
        VideoItem.__init__(self, item, None)
        self.setParentItem(parent)
        self.item = item
        self.top_handle.setVisible(False)
        self.bottom_handle.setVisible(False)
        self._stream = None

        # Let hover events pass through (hover-transparent) when collapsed
        self.setAcceptHoverEvents(False)

    def view_scale_changed(self, view):
        # BJC I tried to keep it view-independent, but the handles need to have different sizes
        # depending on the level of zoom in the view (not to mention separate sets of thumbnails)
        hx = view.handle_width / float(view.scale_x)

        self.left_handle.setRect(QtCore.QRectF(0.0, 0.0, hx, self.parentItem().item_display_height))
        self.right_handle.setRect(QtCore.QRectF(-hx, 0.0, hx, self.parentItem().item_display_height))

    def _added_to_scene(self):
        # Set the things we couldn't without self.units_per_second
        self.setPos(self.item.x / self.units_per_second, 0.0)
        self.right_handle.setPos(self.item.length / self.units_per_second, 0.0)

    @property
    def stream(self):
        if not self._stream:
            self._stream = sources.VideoSource(self.scene().source_list.get_stream(self.item.source.source_name, self.item.source.stream_index))
            self._stream.offset = self.item.offset

        return self._stream

    @property
    def height(self):
        return self.parentItem().height

    def paint(self, painter, option, widget):
        pass

class VideoSequence(VideoItem):
    def __init__(self, sequence):
        VideoItem.__init__(self, sequence, None)
        self.manager = None

        self.left_handle.hide()
        self.right_handle.hide()
        self.top_handle.setZValue(1)
        self.bottom_handle.setZValue(1)

        self.seq_items = [_SequenceItem(item, self) for item in sequence]
        x = 0

        for seq_item in self.seq_items:
            seq_item.x = x
            x += seq_item.item.length - seq_item.item.transition_length

    @property
    def item_display_height(self):
        return self.item.height

    @property
    def stream(self):
        if not self.manager:
            self.manager = graph.SequenceVideoManager(self.item, self.scene().source_list, self.scene().space.video_format)

        return self.manager

    def view_scale_changed(self, view):
        VideoItem.view_scale_changed(self, view)

        for item in self.seq_items:
            item.view_scale_changed(view)

    def _handle_item_added(self, item):
        pass

    def _handle_items_removed(self, start, stop):
        pass

    def _handle_item_updated(self, item):
        pass


