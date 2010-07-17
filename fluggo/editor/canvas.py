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
        old_item_set = frozenset(self._items[start:stop])
        new_item_set = frozenset(items)

        for item in (old_item_set - new_item_set):
            self.item_removed(item)
            item.kill()

        self._items[start:stop] = items

        for i, item in enumerate(items, start):
            item._scene = self
            item.update(z=i)

            if item not in old_item_set:
                self.item_added(item)

    def find_overlaps(self, item):
        '''Find all items that directly overlap the given item (but not including the given item).'''
        return [other for other in self._items if item is not other and item.overlaps(other)]

    def find_overlaps_recursive(self, start_item):
        '''
        Find all items that indirectly overlap the given item.

        Indirect items are items that touch items that touch the original items (to whatever
        level needed) but only in either direction (the stack must go straight up or straight down).
        '''
        first_laps = self.find_overlaps(start_item)
        up_items = set(x for x in first_laps if x.z > start_item.z)
        down_items = set(x for x in first_laps if x.z < start_item.z)
        result = up_items | down_items

        while up_items:
            current_overlaps = set()

            for item in up_items:
                current_overlaps |= frozenset(x for x in self.find_overlaps(item) if x.z > item.z) - result
                result |= current_overlaps

            up_items = current_overlaps

        while down_items:
            current_overlaps = set()

            for item in down_items:
                current_overlaps |= frozenset(x for x in self.find_overlaps(item) if x.z < item.z) - result
                result |= current_overlaps

            down_items = current_overlaps

        return result

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
    def __init__(self, x=0, y=0.0, width=1, height=1.0, offset=0):
        self._scene = None
        self._offset = offset
        self._x = x
        self._y = y
        self._z = 0
        self._height = height
        self._width = width
        self._overlap_items = None
        self.updated = signal.Signal()

    def _create_repr_dict(self):
        return {
            'x': self._x, 'y': self._y,
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

    def overlaps(self, other):
        if self.x >= (other.x + other.width) or (self.x + self.width) <= other.x:
            return False

        if self.y >= (other.y + other.height) or (self.y + self.height) <= other.y:
            return False

        return True

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

        # Update the overlap_items list (if we have one)
        if self._overlap_items is not None:
            old_overlaps = self._overlap_items
            self._overlap_items = self._scene.find_overlaps_recursive(self)

            for old_item in old_overlaps - self._overlap_items:
                if old_item._overlap_items is not None and self in old_item._overlap_items:
                    old_item._overlap_items.remove(self)

            for new_item in self._overlap_items - old_overlaps:
                if new_item._overlap_items is not None:
                    new_item._overlap_items.add(self)
        else:
            # Start from scratch
            self._overlap_items = self._scene.find_overlaps_recursive(self)

            for overlap_item in self._overlap_items:
                if overlap_item._overlap_items is not None:
                    overlap_item._overlap_items.add(self)

        self.updated(**kw)

    def overlap_items(self):
        if self._overlap_items is None:
            self._overlap_items = self._scene.find_overlaps_recursive(self)

        return self._overlap_items

    def kill(self):
        self._scene = None

        for item in self._overlap_items:
            if item._overlap_items is not None:
                item._overlap_items.remove(self)

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


