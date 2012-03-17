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

import yaml, collections, itertools
from fluggo import ezlist, sortlist, signal

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


class Item(object):
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

    def __init__(self, x=0, y=0.0, length=1, height=1.0, type=None, anchor=None,
            anchor_target_offset=None, anchor_source_offset=None, anchor_visible=False, tags=None,
            ease_in=0, ease_out=0, ease_in_type=None, ease_out_type=None, in_motion=False):
        self._space = None
        self._x = x
        self._y = y
        self._z = 0
        self._height = height
        self._length = length
        self._type = type
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
        self.in_motion = in_motion

    def _create_repr_dict(self):
        result = {
            'x': self._x, 'y': self._y,
            'length': self._length, 'height': self._height,
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
            result['tags'] = list(self._tags)

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
    def length(self):
        return self._length

    @property
    def height(self):
        return self._height

    @property
    def space(self):
        return self._space

    def z_sort_key(self, y=None, z=None):
        '''
        Get an object that can be used to sort items in video overlay order. *y* and *z*
        alter the key.
        '''
        return _ZSortKey(self, self.overlap_items(), self._y if y is None else y, self._z if z is None else z)

    def overlaps(self, other):
        '''
        Return True if *other* overlaps this item.
        '''
        if self.x >= (other.x + other.length) or (self.x + self.length) <= other.x:
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

        if 'length' in kw:
            self._length = int(kw['length'])

        if 'y' in kw:
            self._y = float(kw['y'])

        if 'height' in kw:
            self._height = float(kw['height'])

        if 'z' in kw:
            self._z = int(kw['z'])

        if 'in_motion' in kw:
            self.in_motion = bool(kw['in_motion'])

        self.updated(**kw)

    def overlap_items(self):
        '''
        Get a list of all items that directly or indirectly overlap this one.
        '''
        return self._space.find_overlaps_recursive(self)

    def kill(self):
        self._space = None

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

    def __init__(self, type=None, offset=0, source=None, **kw):
        Item.__init__(self, **kw)
        self._type = type
        self._source = source
        self._offset = offset

    def _create_repr_dict(self):
        dict = Item._create_repr_dict(self)
        dict['offset'] = self._offset

        if self._source:
            dict['source'] = self._source

        return dict

    def update(self, **kw):
        '''
        Update the attributes of this item.
        '''
        if 'offset' in kw:
            self._offset = int(kw['offset'])

        if 'source' in kw:
            self._source = kw['source']

        Item.update(self, **kw)

    @property
    def source(self):
        return self._source

    @property
    def offset(self):
        return self._offset

class PlaceholderItem(Item):
    def __init__(self, copy):
        Item.__init__(self,
            x=copy.x,
            y=copy.y,
            length=copy.length,
            height=copy.height,
            type=copy.type())

    def _create_repr_dict(self):
        raise NotImplementedError

class Sequence(Item, ezlist.EZList):
    yaml_tag = u'!CanvasSequence'

    def __init__(self, type=None, items=None, expanded=False, **kw):
        Item.__init__(self, **kw)
        ezlist.EZList.__init__(self)
        self._type = type
        self._items = items if items is not None else []
        self._expanded = expanded

        # Signal with signature signal(item)
        self.item_added = signal.Signal()

        # Signal with signature signal(start, stop)
        self.items_removed = signal.Signal()

        #: Signal with signature item_updated(item, **kw)
        self.item_updated = signal.Signal()

        if items:
            self.fixup()

    def _create_repr_dict(self):
        dict_ = Item._create_repr_dict(self)
        dict_['type'] = self._type
        dict_['items'] = list(self._items)
        dict_['expanded'] = self._expanded
        del dict_['length']

        return dict_

    def type(self):
        return self._type

    @property
    def expanded(self):
        return self._expanded

    def __getitem__(self, index):
        return self._items[index]

    def __len__(self):
        return len(self._items)

    def __iter__(self):
        return self._items.__iter__()

    def _replace_range(self, start, stop, items):
        old_item_set = frozenset(self._items[start:stop])
        new_item_set = frozenset(items)

        for item in sorted(old_item_set - new_item_set, key=lambda a: -a.index):
            self._length -= item.length - item.transition_length

            if item.index == 0:
                self._length -= item.transition_length

            item.kill()

        if stop > start:
            self._items[start:stop] = []
            self._update_marks(start, stop, 0)

            # Reset the x values once
            x = 0

            if start > 0:
                prev_item = self._items[start - 1]
                x = prev_item._x + prev_item.length

            for i, item in enumerate(self._items[start:], start):
                item._sequence = self
                item._x = x - item.transition_length
                x += item.length - item.transition_length

            self.items_removed(start, stop)

        self._items[start:start] = items
        self._update_marks(start, start, len(items))

        # Reset the x values again
        x = 0

        if start > 0:
            prev_item = self._items[start - 1]
            x = prev_item._x + prev_item.length

        for i, item in enumerate(self._items[start:], start):
            item._sequence = self
            item._x = x - item.transition_length
            x += item.length - item.transition_length

        # Send item_added notifications
        for item in (new_item_set - old_item_set):
            self._length += item.length - item.transition_length

            if item.index == 0:
                self._length += item.transition_length

            self.item_added(item)

        # Send x updates
        for item in self._items[start:]:
            self.item_updated(item, x=item._x)

        Item.update(self, length=self._length)

    def _move_items(self, start_index, xdiff, lendiff):
        if xdiff:
            item = self._items[start_index]
            item._x += xdiff
            self.item_updated(item, x=item._x)

        for item in self._items[start_index + 1:]:
            item._x += xdiff + lendiff
            self.item_updated(item, x=item._x)

        self.update(length=self.length + xdiff + lendiff)

    def fixup(self):
        Item.fixup(self)

        self._items = sortlist.AutoIndexList(self._items, index_attr='_index')

        # Count up the proper length and set it on the item
        total_length = len(self) and self[0].transition_length or 0

        for item in self._items:
            item._sequence = self
            item._type = self._type
            item._x = total_length - item.transition_length
            total_length += item.length - item.transition_length

        Item.update(self, length=total_length)

class SequenceItem(object):
    yaml_tag = u'!CanvasSequenceItem'

    def __init__(self, source=None, offset=0, length=1, transition=None, transition_length=0, type=None, in_motion=False):
        if length < 1:
            raise ValueError('length cannot be less than 1 ({0} was given)'.format(length))

        self._source = source
        self._offset = offset
        self._length = length
        self._transition = transition
        self._transition_length = transition_length
        self._sequence = None
        self._index = None
        self._type = type
        self._x = 0
        self.in_motion = in_motion

    def update(self, **kw):
        '''
        Update the attributes of this item.
        '''
        xdiff = 0
        lendiff = 0

        if 'source' in kw:
            self._source = kw['source']

        if 'offset' in kw:
            self._offset = int(kw['offset'])

        if 'length' in kw:
            new_length = int(kw['length'])

            if new_length < 1:
                raise ValueError('length cannot be less than 1 ({0} was given)'.format(new_length))

            lendiff += new_length - self._length
            self._length = new_length

        if 'in_motion' in kw:
            self.in_motion = bool(kw['in_motion'])

        if 'transition' in kw:
            self._transition = kw['transition']

        if 'transition_length' in kw:
            new_length = int(kw['transition_length'])

            xdiff -= new_length - self._transition_length
            self._transition_length = new_length

        if self._sequence:
            if xdiff or lendiff:
                self._sequence._move_items(self._index, xdiff, lendiff)

            self._sequence.item_updated(self, **kw)

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
        '''The length of the transition preceding this clip, if any. Zero means a cut, and a
        positive number gives the length of the transition. A negative number indicates a gap
        between the previous clip and this one. The first clip in a sequence should have a
        transition_length of zero.'''
        return self._transition_length

    @property
    def index(self):
        return self._index

    @property
    def sequence(self):
        return self._sequence

    @property
    def x(self):
        return self._x

    def type(self):
        return self._type

    def previous_item(self, skip_in_motion=False):
        '''Gets the previous item, or None if there isn't one.

        If skip_in_motion is True, skips over in_motion items.'''
        item = self

        while item.index > 0:
            item = item.sequence[item.index - 1]

            if skip_in_motion and item.in_motion:
                continue

            return item

    def next_item(self, skip_in_motion=False):
        '''Gets the next item, or None if there isn't one.

        If skip_in_motion is True, skips over in_motion items.'''
        item = self

        while item.index < len(item.sequence) - 1:
            item = item.sequence[item.index + 1]

            if skip_in_motion and item.in_motion:
                continue

            return item

    @classmethod
    def to_yaml(cls, dumper, data):
        mapping = {'source': data._source,
            'offset': data._offset, 'length': data._length}

        if data._transition_length:
            mapping['transition_length'] = data._transition_length

            if data._transition:
                mapping['transition'] = data._transition

        return dumper.represent_mapping(cls.yaml_tag, mapping)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

    def kill(self):
        self._sequence = None
        self._index = None

    def __str__(self):
        return yaml.dump(self)


def _yamlreg(cls):
    yaml.add_representer(cls, cls.to_yaml)
    yaml.add_constructor(cls.yaml_tag, cls.from_yaml)

_yamlreg(Item)
_yamlreg(Clip)
_yamlreg(Sequence)
_yamlreg(SequenceItem)

