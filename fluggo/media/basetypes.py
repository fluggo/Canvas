# -*- coding: utf-8 -*-
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

import collections

_v2i = collections.namedtuple('_v2i', 'x y')

class v2i(_v2i):
    def __new__(class_, x=0, y=0):
        if isinstance(x, tuple):
            (x, y) = x

        return _v2i.__new__(class_, int(x), int(y))

    def __add__(self, other):
        return v2i(self[0] + other[0], self[1] + other[1])

    def __sub__(self, other):
        return v2i(self[0] - other[0], self[1] - other[1])

    def __repr__(self):
        return 'v2i({0.x}, {0.y})'.format(self)

_box2i = collections.namedtuple('_box2i', 'min max')

class box2i(_box2i):
    def __new__(class_, min=v2i(0, 0), max=v2i(-1, -1), max_x=None, max_y=None):
        if max_x is not None and max_y is not None:
            min = v2i(min, max)
            max = v2i(max_x, max_y)
        elif isinstance(min, box2i):
            (min, max) = min

        return _box2i.__new__(class_, v2i(min), v2i(max))

    def width(self):
        return max(0, self.max.x - self.min.x + 1)

    def height(self):
        return max(0, self.max.y - self.min.y + 1)

    def size(self):
        if self.empty():
            return v2i()

        return self.max - self.min + v2f(1, 1)

    def empty(self):
        return not self.__nonzero__()

    def __nonzero__(self):
        return self.max.x >= self.min.x and self.max.y >= self.min.y

    def __repr__(self):
        return 'box2i({0.min}, {0.max})'.format(self)

_v2f = collections.namedtuple('_v2f', 'x y')

class v2f(_v2f):
    def __new__(class_, x=0, y=0):
        if isinstance(x, tuple):
            (x, y) = x

        return _v2f.__new__(class_, float(x), float(y))

    def __add__(self, other):
        return v2f(self[0] + other[0], self[1] + other[1])

    def __sub__(self, other):
        return v2f(self[0] - other[0], self[1] - other[1])

    def __repr__(self):
        return _v2f.__repr__(self)[1:]

_box2f = collections.namedtuple('_box2f', 'min max')

class box2f(_box2f):
    def __new__(class_, min=v2f(0, 0), max=v2f(-1, -1)):
        return _box2i.__new__(class_, v2f(min), v2f(max))

    def width(self):
        return max(0, self.max.x - self.min.x)

    def height(self):
        return max(0, self.max.y - self.min.y)

    def size(self):
        if self.empty():
            return v2f()

        return self.max - self.min

    def empty(self):
        return not self.__nonzero__()

    def __nonzero__(self):
        return self.max.x >= self.min.x and self.max.y >= self.min.y

    def __repr__(self):
        return _box2f.__repr__(self)[1:]

_rgba = collections.namedtuple('_rgba', 'r g b a')

class rgba(_rgba):
    def __new__(class_, r=0.0, g=0.0, b=0.0, a=1.0):
        return _rgba.__new__(class_, float(r), float(g), float(b), float(a))

    def __repr__(self):
        return _rgba.__repr__(self)[1:]

