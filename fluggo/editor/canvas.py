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

import yaml, collections
from fluggo import ezlist, sortlist, signal

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

        for i, item in enumerate(self._items[start:], start):
            item._scene = self
            item._z = i

        if len(old_item_set) > len(new_item_set):
            # We can renumber going forwards
            for i, item in enumerate(self._items[start:], start):
                item.update(z=i)
        elif len(new_item_set) > len(old_item_set):
            # We must renumber backwards to avoid conflicts
            for i, item in reversed(list(enumerate(self._items[start:], start))):
                item.update(z=i)
        else:
            # We only have to renumber the items themselves
            for i, item in enumerate(self._items[start:stop], start):
                item.update(z=i)

        for item in (new_item_set - old_item_set):
            self.item_added(item)

    def fixup(self):
        '''
        Perform first-time initialization after being deserialized; PyYAML doesn't
        give us a chance to do this automatically.
        '''
        # Number the items as above
        for i, item in enumerate(self._items):
            item._scene = self
            item._z = i
            item.fixup()

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

class _ZSortKey():
    __slots__ = ('item', 'overlaps', 'y', 'z')

    def __init__(self, item, overlaps, y, z):
        self.item = item
        self.y = y
        self.z = z

    def __cmp__(self, other):
        if other.item in self.item.overlap_items():
            result = -cmp(self.z, other.z)

            if result:
                return result

        return -cmp(self.y, other.y)

    def __str__(self):
        return 'key(y={0.y}, z={0.z})'.format(self)

class Item(yaml.YAMLObject):
    '''
    Class for all items that can appear in the canvas.

    All of the arguments for the constructor are the YAML properties that can appear
    for this class.

    An anchor on one clip says that its position should be fixed in relation
    to another. In the Y direction, all we need is the :attr:`target` clip; each item's
    current position is enough to establish the offset.

    In the X (time) direction, if each clip is using a different time scale, the
    time offset can be different depending on where each item appears in the
    canvas. Therefore we establish a fixed offset here based on :attr:`target_offset`,
    the offset from the beginning of the target clip (not the beginning of the
    target's source) in target frames, and :attr:`source_offset`, the offset in source
    frames from the position defined by :attr:`target_offset` to the beginning of the
    source clip.

    This won't work out exactly in many situations; the scene will round to the
    nearest position in those cases.

    The attribute :attr:`visible` determines whether the anchor deserves displaying
    an explicit link between the clips. If ``False``, then the anchor acts more
    like groups found in other editors.
    '''

    yaml_tag = u'!CanvasItem'

    def __init__(self, x=0, y=0.0, width=1, height=1.0, type=None, source=None, anchor=None,
            anchor_target_offset=None, anchor_source_offset=None, anchor_visible=False, tags=None,
            ease_in=0, ease_out=0, ease_in_type=None, ease_out_type=None):
        self._scene = None
        self._x = x
        self._y = y
        self._z = 0
        self._height = height
        self._width = width
        self._type = type
        self._source = source
        self._ease_in_type = ease_in_type
        self._ease_in = ease_in
        self._ease_out_type = ease_out_type
        self._ease_out = ease_out
        self.updated = signal.Signal()
        self._anchor = anchor
        self._anchor_target_offset = anchor_target_offset
        self._anchor_source_offset = anchor_source_offset
        self._anchor_visible = anchor_visible
        self._tags = set(tags) if tags else set()

    def _create_repr_dict(self):
        result = {
            'x': self._x, 'y': self._y,
            'width': self._width, 'height': self._height,
            'type': self._type
        }

        if self._anchor:
            result['anchor'] = self._anchor

            if self._anchor_target_offset:
                result['anchor_target_offset'] = self._anchor_target_offset

            if self._anchor_source_offset:
                result['anchor_source_offset'] = self._anchor_source_offset

            if self._anchor_visible:
                result['anchor_visible'] = self._anchor_visible

        if self._ease_in:
            result['ease_in'] = self._ease_in

            if self._ease_in_type:
                result['ease_in_type'] = self._ease_in_type

        if self._ease_out:
            result['ease_out'] = self._ease_out

            if self._ease_out_type:
                result['ease_out_type'] = self._ease_out_type

        if self._tags:
            result['tags'] = list(tags)

        if self._source:
            result['source'] = self._source

        return result

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data._create_repr_dict())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

    @property
    def tags(self):
        return frozenset(self._tags)

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
    def source(self):
        return self._source

    def z_sort_key(self, y=None, z=None):
        '''
        Get an object that can be used to sort items in video overlay order. *y* and *z*
        alter the key.
        '''
        return _ZSortKey(self, self.overlap_items(), self._y if y is None else y, self._z if z is None else z)

    def __cmp__(self, other):
        if self is other:
            return 0

        return cmp(self.z_sort_key(), other.z_sort_key())

    def overlaps(self, other):
        '''
        Return True if *other* overlaps this item.
        '''
        if self.x >= (other.x + other.width) or (self.x + self.width) <= other.x:
            return False

        if self.y >= (other.y + other.height) or (self.y + self.height) <= other.y:
            return False

        return True

    def update(self, **kw):
        '''
        Update the attributes of this item.
        '''
        if 'x' in kw:
            self._x = int(kw['x'])

        if 'width' in kw:
            self._width = int(kw['width'])

        if 'y' in kw:
            self._y = float(kw['y'])

        if 'height' in kw:
            self._height = float(kw['height'])

        if 'z' in kw:
            self._z = int(kw['z'])

        if 'source' in kw:
            self._source = kw['source']

        self.updated(**kw)

    def overlap_items(self):
        '''
        Get a list of all items that directly or indirectly overlap this one.
        '''
        return self._scene.find_overlaps_recursive(self)

    def kill(self):
        self._scene = None

    def fixup(self):
        '''
        Perform initialization that has to wait until deserialization is finished.
        '''
        pass

    def type(self):
        '''
        The type of the item, such as ``'audio'`` or ``'video'``.
        '''
        return self._type

    def split(self, offset):
        '''
        Split the item *offset* frames from its start, putting two (new) items in
        its place in the scene list.
        '''
        raise NotImplementedError

    def can_join(self, other):
        return False

    def join(self, other):
        raise NotImplementedError

class Clip(Item):
    '''
    A freestanding video or audio clip.
    '''
    yaml_tag = u'!CanvasClip'

    def __init__(self, type=None, offset=0, source_name=None, source_stream_index=None, **kw):
        Item.__init__(self, **kw)
        self._type = type
        self._source_name = source_name
        self._source_stream_index = source_stream_index
        self._offset = offset

    def _create_repr_dict(self):
        dict = Item._create_repr_dict(self)
        dict['source_name'] = self._source_name
        dict['source_stream_index'] = self._source_stream_index
        dict['offset'] = self._offset

        return dict

    def update(self, **kw):
        '''
        Update the attributes of this item.
        '''
        if 'offset' in kw:
            self._offset = int(kw['offset'])

        Item.update(self, **kw)

    @property
    def source_name(self):
        return self._source_name

    @property
    def source_stream_index(self):
        return self._source_stream_index

    @property
    def offset(self):
        return self._offset

class StreamSourceRef(yaml.YAMLObject):
    '''
    References a stream from a video or audio file.
    '''
    yaml_tag = u'!StreamSourceRef'

    def __init__(self, source_name=None, stream_index=None, **kw):
        self._source_name = source_name
        self._stream_index = stream_index

    @property
    def source_name(self):
        return self._source_name

    @property
    def stream_index(self):
        return self._stream_index

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {'source_name': self._source_name,
            'stream_index': self._stream_index}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class Timeline(Item):
    yaml_tag = u'!CanvasTimeline'

    def __init__(self, type=None, items=None, expanded=False, **kw):
        Item.__init__(self, **kw)
        self._type = type
        self._items = items
        self._expanded = False

        #: Signal with signature item_updated(item, **kw)
        self.item_updated = signal.Signal()

    def type(self):
        return self._type

    def __getitem__(self, index):
        return self._items[index]

    def __len__(self):
        return len(self._items)

    def __iter__(self):
        return self._items.__iter__()

    def fixup(self):
        Item.fixup(self)

        self._items = sortlist.AutoIndexList(self._items)

        # Count up the proper width and set it on the item
        total_width = len(self) and self[0].transition_length or 0

        for item in self._items:
            item._timeline = self
            total_width += item.length - item.transition_length

        Item.update(self, width=total_width)

class TimelineItem(yaml.YAMLObject):
    yaml_tag = u'!CanvasTimelineItem'

    def __init__(self, source=None, offset=0, length=1, transition=None, transition_length=0):
        self._source = source
        self._offset = offset
        self._length = length
        self._transition = transition
        self._transition_length = transition_length
        self._timeline = None
        self._index = None

    def update(self, **kw):
        '''
        Update the attributes of this item.
        '''
        if 'source' in kw:
            self._source = kw['source']

        if 'offset' in kw:
            self._offset = int(kw['offset'])

        if 'length' in kw:
            self._length = int(kw['length'])

        if 'transition' in kw:
            self._transition = kw['transition']

        if 'transition_length' in kw:
            self._transition_length = int(kw['transition_length'])

        self._timeline.item_updated(self, **kw)

    @property
    def source(self):
        return self._source

    @property
    def offset(self):
        return self._offset

    @property
    def length(self):
        return self._length

    @property
    def transition(self):
        return self._transition

    @property
    def transition_length(self):
        return self._transition_length

    @classmethod
    def to_yaml(cls, dumper, data):
        mapping = {'source': self._source,
            'offset': self._offset, 'length': self._length}

        if self._transition_length:
            mapping['transition_length'] = self._transition_length

            if self._transition:
                mapping['transition'] = self._transition

        return dumper.represent_mapping(cls.yaml_tag, mapping)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

