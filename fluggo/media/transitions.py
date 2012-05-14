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

import yaml
from . import formats, process

class Transition(object):
    def create_source(self, source_a, source_b, length):
        raise NotImplementedError

class Crossfade(Transition):
    def create_source(self, source_a, source_b, length):
        return process.VideoMixFilter(src_a=source_a, src_b=source_b, mix_b=LinearFrameFunc(a=1.0/length, b=0.0))

