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

import yaml, collections, itertools
from fluggo import ezlist, sortlist, signal

class Space(ezlist.EZList):
    yaml_tag = u'!CanvasSpace'

    def __init__(self, video_format, audio_format):
        self.item_added = signal.Signal()
        self.item_removed = signal.Signal()
        self._items = []
        self._video_format = video_format
        self._audio_format = audio_format

    def __len__(self):
        return len(self._items)

    def __getitem__(self, key):
        return self._items[key]

    @property
    def video_format(self):
        return self._video_format

    @property
    def audio_format(self):
        return self._audio_format

    def _replace_range(self, start, stop, items):
        old_item_set = frozenset(self._items[start:stop])
        new_item_set = frozenset(items)

        for item in (old_item_set - new_item_set):
            self.item_removed(item)
            item.kill()

        self._items[start:stop] = items

        for i, item in enumerate(self._items[start:], start):
            item._space = self
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
            item._space = self
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
    result = Space(mapping['video_format'], mapping['audio_format'])
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
            ease_in=0, ease_out=0, ease_in_type=None, ease_out_type=None):
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
        self.in_motion = False

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
            result['tags'] = list(tags)

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

class StreamSourceRef(object):
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
        result = {'source_name': data._source_name,
            'stream_index': data._stream_index}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class Sequence(Item, ezlist.EZList):
    yaml_tag = u'!CanvasSequence'

    def __init__(self, type=None, items=None, expanded=False, **kw):
        Item.__init__(self, **kw)
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

        # Reset the x values again
        x = 0

        if start > 0:
            prev_item = self._items[start - 1]
            x = prev_item._x + prev_item.length

        for i, item in enumerate(self._items[start:], start):
            item._sequence = self
            item._x = x - item.transition_length
            x += item.length - item.transition_length

        for item in (new_item_set - old_item_set):
            self._length += item.length - item.transition_length

            if item.index == 0:
                self._length += item.transition_length

            self.item_added(item)

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
            item._x = total_length - item.transition_length
            total_length += item.length - item.transition_length

        Item.update(self, length=total_length)

class SequenceItem(object):
    yaml_tag = u'!CanvasSequenceItem'

    def __init__(self, source=None, offset=0, length=1, transition=None, transition_length=0):
        if length < 1:
            raise ValueError('length cannot be less than 1 ({0} was given)'.format(length))

        if transition_length < 0:
            raise ValueError('transition_length cannot be less than 0 ({0} was given)'.format(length))

        self._source = source
        self._offset = offset
        self._length = length
        self._transition = transition
        self._transition_length = transition_length
        self._sequence = None
        self._index = None
        self._x = 0
        self.in_motion = False

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

            if new_length < 0:
                raise ValueError('transition_length cannot be less than zero ({0} was given)'.format(new_length))

            xdiff -= new_length - self._transition_length
            self._transition_length = new_length

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
_yamlreg(StreamSourceRef)
_yamlreg(Sequence)
_yamlreg(SequenceItem)

class ItemManipulator(object):
    # TODO
    # Identify adjacent items in a sequence and manipulate them as a unit
    # Identify groups and manipulate them as a unit
    # Find good algorithm for turning loose items into sequences
    #  ^    Scratch: Only the item/sequence directly grabbed (listed first in self.items)
    #       is placed in a sequence, and the rest arranged around it accordingly

    class ClipManipulator(object):
        '''Manipulates a lone clip.'''
        def __init__(self, item, grab_x, grab_y):
            self.item = item
            self.original_x = item.x
            self.original_y = item.y
            self.original_space = item.space
            self.offset_x = item.x - grab_x
            self.offset_y = item.y - grab_y
            self.seq_item = None
            self.placeholder = PlaceholderItem(item)

        def can_set_space_item(self, space, x, y):
            return True

        def set_space_item(self, space, x, y):
            print 'clip.set_space_item'
            if self.seq_item and self.seq_item.sequence:
                del self.seq_item.sequence[self.seq_item.index]
                self.seq_item = None

            if not self.item.space:
                self.placeholder.space[self.placeholder.index] = self.item

            self.item.update(x=x + self.offset_x, y=y + self.offset_y, in_motion=True)
            return True

        def can_set_sequence_item(self, sequence, x, operation):
            return self.set_sequence_item(sequence, x, operation, do_it=False)

        def set_sequence_item(self, sequence, x, operation, do_it=True):
            if operation == 'add':
                return self._set_sequence_add(sequence, x, do_it=do_it)

            return False

        def _set_sequence_add(self, sequence, x, do_it=True):
            # For the first pass at this, we'll just do inserting in place
            # Later we can come back for overwriting or proper insertion

            # Simpler first: remove the item from the sequence before putting it back
            # TODO: See if we can just move it
            if self.seq_item and self.seq_item.sequence:
                del self.seq_item.sequence[self.seq_item.index]
                self.seq_item = None

            prev_item, current_item, next_item = None, None, sequence[0]
            start_x = x + self.offset_x
            end_x = x + self.offset_x + self.item.length

            while next_item and next_item.x + next_item.length >= start_x:
                prev_item = current_item
                current_item = next_item
                next_item = sequence[current_item.index + 1] if current_item.index + 1 < len(sequence) else None

                if start_x > current_item.x:
                    continue

                # Don't let it end before this item
                if end_x < current_item.x:
                    return False

                # If we run over the end of the current one...
                if end_x > (current_item.x + current_item.length):
                    if start_x == current_item.x:
                        # This one belongs at the next position
                        continue
                    else:
                        # It dominates this one, can't do it
                        return False

                # If we already have a transition here, nothing will fit
                if current_item.transition_length:
                    return False

                # If there's a next transition (or even just a cut), don't run over it
                next_trans_start_x = (current_item.x + current_item.length - next_item.transition_length) if next_item else None

                if next_trans_start_x and end_x > next_trans_start_x:
                    return False

                if do_it:
                    self.seq_item = SequenceItem(source=self.item.source,
                        length=self.item.length,
                        offset=self.item.offset,
                        transition_length=current_item.x - start_x if prev_item else 0)

                    print self.seq_item

                    sequence.insert(current_item.index, self.seq_item)
                    current_item.update(transition_length=end_x - current_item.x)

                    if not prev_item:
                        # Move the sequence to account for the new item
                        self.sequence.update(x=self.sequence.x + (self.item.length - current_item.transition_length))

                    if item.space:
                        self.item.space[self.item.index] = self.placeholder

                return True

            return False

        def reset(self):
            self.set_space_item(None, self.original_x - self.offset_x, self.original_y - self.offset_y)
            self.item.update(in_motion=False)

        def finish(self):
            self.item.update(in_motion=False)
            return True

    class SequenceItemGroupManipulator(object):
        '''Manipulates a set of adjacent sequence items.'''
        def __init__(self, items, grab_x, grab_y):
            self.items = items

        def can_set_space_item(self, space, x, y):
            return False

        def set_space_item(self, space, x, y):
            return False

        def can_set_sequence_item(self, sequence, x):
            return False

        def set_sequence_item(self, sequence, x):
            return False

        def reset(self):
            pass

        def finish(self):
            pass

    class SequenceManipulator(object):
        '''Manipulates an entire existing sequence.'''
        def __init__(self, item, grab_x, grab_y):
            self.seq = item
            self.original_x = item.x
            self.original_y = item.y
            self.original_scene = item.scene
            self.offset_x = item.x - grab_x
            self.offset_y = item.y - grab_y

        def can_set_space_item(self, space, x, y):
            return True

        def set_space_item(self, space, x, y):
            self.item.update(x=x + self.offset_x, y=y + self.offset_y)
            return True

        def can_set_sequence_item(self, sequence, x):
            return False

        def set_sequence_item(self, sequence, x):
            pass

        def reset(self):
            self.item.update(x=self.original_x, y=self.original_y)

        def finish(self):
            pass

    def __init__(self, items, grab_x, grab_y):
        self.items = items
        self.manips = []
        seq_items = []

        for item in items:
            if isinstance(item, Clip):
                self.manips.append(self.ClipManipulator(item, grab_x, grab_y))
            elif isinstance(item, Sequence):
                self.manips.append(self.SequenceManipulator(item, grab_x))
            elif isinstance(item, SequenceItem):
                seq_items.append(item)

        # Sort and combine the sequence items
        for itemlist in itertools.groupby(sorted(seq_items, cmp=lambda a, b: cmp(a.sequence, b.sequence) or cmp(a.index, b.index)), key=lambda a: a.sequence):
            self.manips.append(self.SequenceItemGroupManipulator(itemlist, grab_x, grab_y))

    def can_set_space_item(self, space, x, y):
        return all(manip.can_set_space_item(space, x, y) for manip in self.manips)

    def set_space_item(self, space, x, y):
        if all(manip.set_space_item(space, x, y) for manip in self.manips):
            return True

        self.reset()
        return False

    def can_set_sequence_item(self, sequence, x, operation):
        return all(manip.can_set_sequence_item(sequence, x, operation) for manip in self.manips)

    def set_sequence_item(self, sequence, x, operation):
        if all(manip.set_sequence_item(sequence, x, operation) for manip in self.manips):
            return True

        self.reset()
        return False

    def reset(self):
        for manip in self.manips:
            manip.reset()

    def finish(self):
        if all(manip.finish() for manip in self.manips):
            return True

        self.reset()
        return False


