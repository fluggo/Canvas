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

from logging import *

class _DeferredFormat(object):
    __slots__ = ('format', 'args', 'kw')

    def __init__(self, format, *args, **kw):
        self.format = format
        self.args = args
        self.kw = kw

    def __str__(self):
        return self.format.format(*self.args, **self.kw)

class _mylogger(getLoggerClass()):
    def warnonerror(self, msg='Error while executing method'):
        '''Logs a warning when an exception happens, and then eats the exception.'''
        import functools

        if callable(msg):
            # We were called with the @warnonerror syntax; that's fine
            func = msg
            msg = 'Error while executing method'

            @functools.wraps(func)
            def wrapper_func(*args, **kw):
                try:
                    return func(*args, **kw)
                except:
                    self.warning(msg, exc_info=True)

            return wrapper_func
        else:
            # We were called with the @warnonerror(msg) syntax
            def wrapper(func):
                @functools.wraps(func)
                def wrapper_func(*args, **kw):
                    try:
                        return func(*args, **kw)
                    except:
                        self.warning(msg, exc_info=True)

                return wrapper_func

            return wrapper

setLoggerClass(_mylogger)

# Works for Python 3.2 and later
_baseFactory = getLogRecordFactory()

def _factory(name, lvl, fn, lno, msg, args, exc_info, func=None, sinfo=None, **kwargs):
    return _baseFactory(name, lvl, fn, lno, _DeferredFormat(msg, *args, **kwargs), [], exc_info, func=func, sinfo=sinfo)

setLogRecordFactory(_factory)

