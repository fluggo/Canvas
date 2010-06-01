# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009 Brian J. Crowell <brian@fluggo.com>
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
import process
import yaml
from formats import *

class SourceList(object):
    def __init__(self, muxers, sources=None):
        self.muxers = muxers
        self.sources = sources or {}
        # TODO: Add signals for list changes, renames, etc.

    def get_stream(self, name, stream_id):
        container = self.sources.get(name)

        for muxer in self.muxers:
            if container.muxer in muxer.supported_muxers:
                return muxer.get_stream(container, stream_id)

        return None

class VideoSource(process.VideoPassThroughFilter):
    def __init__(self, source, format):
        self.format = format
        self.length = self.format.length

        if self.pulldown_type == PULLDOWN_23:
            source = process.Pulldown23RemovalFilter(source, self.pulldown_phase);
            self.length = source.get_new_length(self.length)

        process.VideoPassThroughFilter.__init__(self, source)

    @property
    def pixel_aspect_ratio(self):
        return (
            self.format.override.get(VideoAttribute.SAMPLE_ASPECT_RATIO) or
            self.format.detected.get(VideoAttribute.SAMPLE_ASPECT_RATIO) or '')

    @property
    def pulldown_type(self):
        return (
            self.format.override.get(VideoAttribute.PULLDOWN_TYPE) or
            self.format.detected.get(VideoAttribute.PULLDOWN_TYPE) or '')

    @property
    def pulldown_phase(self):
        return (
            self.format.override.get(VideoAttribute.PULLDOWN_PHASE) or
            self.format.detected.get(VideoAttribute.PULLDOWN_PHASE) or 0)

    @property
    def thumbnail_box(self):
        return (
            self.format.override.get(VideoAttribute.THUMBNAIL_WINDOW) or
            self.format.override.get(VideoAttribute.MAX_DATA_WINDOW) or
            self.format.detected.get(VideoAttribute.THUMBNAIL_WINDOW) or
            self.format.detected.get(VideoAttribute.MAX_DATA_WINDOW))

