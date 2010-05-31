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

from fluggo import ezlist, signal

class Space(ezlist.EZList):
    def __init__(self):
        self.item_added = signal.Signal()
        self.item_removed = signal.Signal()
        self._items = []

    def __len__(self):
        return len(self._items)

    def __getitem__(self, key):
        return self._items[key]

    def _replace_range(self, start, stop, items):
        old_items = self._items[start:stop]
        self._items[start:stop] = []

        for item in old_items:
            self.item_removed(item)

        for i in range(len(items)):
            items[i].update(z=start + i)

        for i in range(start, len(self._items)):
            self._items[i].update(z=i + len(items))

        self._items[start:start] = items

        for item in items:
            self.item_added(item)

class Item(object):
    def __init__(self):
        self._scene = None
        self._offset = 0
        self._x = 0
        self._y = 0.0
        self._height = 1.0
        self._width = 1
        self._z = 0
        self.updated = signal.Signal()

    @property
    def x(self):
        return self._x

    @property
    def y(self):
        return self._y

    @property
    def z(self):
        return self._z

    @property
    def width(self):
        return self._width

    @property
    def height(self):
        return self._height

    @property
    def offset(self):
        return self._offset

    def update(self, **kw):
        if 'x' in kw:
            self._x = int(kw['x'])

        if 'width' in kw:
            self._width = int(kw['width'])

        if 'y' in kw:
            self._y = float(kw['y'])

        if 'height' in kw:
            self._height = float(kw['height'])

        if 'offset' in kw:
            self._offset = int(kw['offset'])

        if 'z' in kw:
            self._z = int(kw['z'])

        self.updated(**kw)

    def type(self):
        raise NotImplementedError

class Clip(Item):
    '''
    A freestanding video or audio clip.
    '''
    def __init__(self, type_):
        Item.__init__(self)
        self._type = type_
        self._source_name = None
        self._source_stream_id = None

    @property
    def source_name(self):
        return self._source_name

    @property
    def source_stream_id(self):
        return self._source_stream_id

    def type(self):
        return self._type


