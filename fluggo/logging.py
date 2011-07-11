# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2010-1 Brian J. Crowell <brian@fluggo.com>
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

# This extends ordinary Python logging to support new-style format strings

from __future__ import absolute_import
from logging import *

class _DeferredFormat(object):
    __slots__ = ('format', 'args', 'kw')

    def __init__(self, format, *args, **kw):
        self.format = format
        self.args = args
        self.kw = kw

    def __str__(self):
        return self.format.format(*args, **kw)

_baseLog = getLoggerClass()

class _logger(_baseLog):
    def log(self, lvl, msg, *args, **kwargs):
        subkw = {}

        if 'exc_info' in kwargs:
            subkw['exc_info'] = kwargs['exc_info']

        if 'extra' in kwargs:
            subkw['extra'] = kwargs['extra']

        _baseLog.log(self, lvl, DeferredFormat(msg, args, kwargs), **subkw)

setLoggerClass(_logger)

