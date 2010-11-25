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

class VideoSequence(VideoItem):
    def __init__(self, sequence):
        VideoItem.__init__(self, sequence, None)
        self.manager = None

    @property
    def stream(self):
        if not self.manager:
            self.manager = graph.SequenceVideoManager(self.item, self.scene().source_list, self.scene().space.video_format)

        return self.manager

