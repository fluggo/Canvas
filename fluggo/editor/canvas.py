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

import yaml
from fluggo import ezlist, signal

class Space(ezlist.EZList):
    yaml_tag = u'!CanvasSpace'

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

def _space_represent(dumper, data):
    return dumper.represent_mapping(u'!CanvasSpace', {'items': data._items})

def _space_construct(loader, node):
    mapping = loader.construct_mapping(node)
    result = Space()
    result._items = mapping['items']
    return result

yaml.add_representer(Space, _space_represent)
yaml.add_constructor(u'!CanvasSpace', _space_construct)


class _ZSortKey(object):
    __slots__ = ('z', 'y')

    def __init__(self, item):
        self.z = item.z
        self.y = item.y

    def __cmp__(self, other):
        result = cmp(self.z, other.z)

        if result:
            return result

        return -cmp(self.y, other.y)

    def __str__(self):
        return 'key(y={0.y}, z={0.z})'.format(self)

class Item(yaml.YAMLObject):
    def __init__(self, x=0, y=0.0, z=0, width=1, height=1.0, offset=0):
        self._scene = None
        self._offset = offset
        self._x = x
        self._y = y
        self._height = height
        self._width = width
        self._z = z
        self.updated = signal.Signal()

    def _create_repr_dict(self):
        return {
            'x': self._x, 'y': self._y, 'z': self._z,
            'width': self._width, 'height': self._height,
            'offset': self._offset
        }

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data._create_repr_dict())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

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

    def z_sort_key(self):
        return _ZSortKey(self)

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
    yaml_tag = u'!CanvasClip'

    def __init__(self, type=None, source_name=None, source_stream_id=None, **kw):
        Item.__init__(self, **kw)
        self._type = type
        self._source_name = source_name
        self._source_stream_id = source_stream_id

    def _create_repr_dict(self):
        dict = Item._create_repr_dict(self)
        dict['source_name'] = self._source_name
        dict['source_stream_id'] = self._source_stream_id
        dict['type'] = self._type

        return dict

    @property
    def source_name(self):
        return self._source_name

    @property
    def source_stream_id(self):
        return self._source_stream_id

    def type(self):
        return self._type


