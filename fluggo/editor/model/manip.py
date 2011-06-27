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

import collections, itertools
from .items import *

class Sequenceable(object):
    def __init__(self, length):
        self.length = length
        self.min_fadeout_point = 0
        self.max_fadein_point = length

    def insert(self, seq, index, transition_length):
        raise NotImplementedError

    def reset(self):
        raise NotImplementedError

    def finish(self):
        raise NotImplementedError

    @property
    def transition_length(self):
        pass

    def set_transition_length(self, length):
        raise NotImplementedError

class ItemManipulator(object):
    # TODO
    # Identify adjacent items in a sequence and manipulate them as a unit
    # Identify groups and manipulate them as a unit
    # Find good algorithm for turning loose items into sequences
    #  ^    Scratch: Only the item/sequence directly grabbed (listed first in self.items)
    #       is placed in a sequence, and the rest arranged around it accordingly

    class SequenceAddMap(object):
        def __init__(self, sequence, item):
            '''Creates an instance of SequenceAddMap.

            sequence - The sequence to add to.
            item - The item (of type Sequenceable) to place.'''
            self.sequence = sequence
            self.item = item

            self.seq_index = None
            self.original_x = sequence.x
            self.orig_next_item_trans_length = None
            self.orig_next_item = None

        def determine_range(self, index):
            '''Determine the range where a clip will fit. These are tuples of (min, max) which are
            frame offsets from the beginning of the sequence to the beginning of the item. If the
            item can't fit at all at an index, None might be returned.'''

            if index < len(self.sequence):
                if self.sequence[index].in_motion:
                    return None

                seq_item = self.sequence[index]

                # Find next_item so we can get its transition_length (which we ignore if the next item is in_motion)
                next_item = seq_item.next_item()

                # Find the previous original item
                prev_item = seq_item.previous_item(skip_in_motion=True)

                # If the item before that is in motion, we have to ignore prev_item's
                # transition_length (which would otherwise be zero or less)
                prev_prev_item = prev_item and prev_item.previous_item()

                _min = max(
                    (prev_item.x +
                        (prev_item.transition_length if prev_prev_item and not prev_prev_item.in_motion else 0))
                        if prev_item else -self.item.length,
                    seq_item.x + min(0, seq_item.transition_length) - (self.item.max_fadein_point if prev_item else self.item.length))

                # -min_fadeout_point is the length of the placed sequence up to the last transition, so
                # that's how far back we have to push something
                # BUT if length is longer than our first clip (less transition), we have to go back farther
                _max = seq_item.x + min(-self.item.min_fadeout_point,
                    seq_item.length - self.item.length - (next_item.transition_length if next_item and not next_item.in_motion else 0))

                _min += self.sequence.x
                _max += self.sequence.x

                #print '{2}=({0}, {1})'.format(_min, _max, index)
                if not prev_item:
                    _min = None
                elif _max < _min:
                    return None

                return (_min, _max, self.sequence.create_mark(index, True))
            else:
                # Final index
                prev_item = self.sequence[-1]

                # Find the latest previous original item
                if prev_item.in_motion:
                    prev_item = prev_item.previous_item(skip_in_motion=True)

                # If the item before that is in motion, we have to ignore prev_item's
                # transition_length (which would otherwise be zero or less)
                prev_prev_item = prev_item and prev_item.previous_item()

                _min = max(prev_item.x +
                        (prev_item.transition_length if prev_prev_item and not prev_prev_item.in_motion else 0),
                    prev_item.x + prev_item.length - self.item.length)

                _min += self.sequence.x

                return (_min, None, self.sequence.create_mark(index, True))

        def where_can_fit(self, x):
            '''Returns index where the item would be inserted if it can fit, None if it won't.
            "x" is space-relative.'''
            for _range in (self.determine_range(i) for i in range(len(self.sequence) + 1)):
                if not _range:
                    continue

                print _range
                if (_range[0] is None or x >= _range[0]) and (_range[1] is None or x <= _range[1]):
                    return _range[2]

            return None

        def can_set(self, x):
            '''Checks if the clip can be placed at x, which is space-relative.'''
            return self.where_can_fit(x) is not None

        def set(self, x):
            print x
            index = self.where_can_fit(x)

            if index is None:
                return False

            if self.seq_index is not None:
                # Adjust index to what it would be if the placed items weren't there
                if index > self.seq_index:
                    index -= (self.orig_next_item.index if self.orig_next_item else len(self.sequence)) - self.seq_index

                print index
                if self.seq_index == index:
                    # It's already in place, just adjust the transition_lengths
                    print x
                    x -= self.sequence.x
                    current_x = self.sequence[self.seq_index].x
                    print self.sequence.x, x, current_x

                    if self.orig_next_item:
                        self.orig_next_item.update(transition_length=self.orig_next_item.transition_length + (x - current_x))

                    if index == 0:
                        self.sequence.update(x=self.sequence.x + x - current_x)
                    else:
                        prev_item = self.sequence[self.seq_index - 1]
                        self.item.set_transition_length(self.item.transition_length - (x - current_x))

                    return True

                self.reset()
                index = self.where_can_fit(x)

            # The item isn't in the sequence at all, so add it
            x -= self.sequence.x

            # We have to determine how to move certain things to make this work,
            # namely the sequence, the next item's transition_length, and our new
            # item's transition length. The goal is to maintain these items'
            # current positions relative to the *space*, and with the understanding
            # that the items in the sequence marked in_motion aren't going to be
            # there. (If that last assumption turns out not to be true, I don't
            # know what to do exactly, except maybe kindly ask self.item if certain
            # of those in_motion items belong to it.)

            # at_index - Item at the insertion index
            at_index = self.sequence[index] if index < len(self.sequence) else None
            at_start = at_index and not at_index.previous_item(skip_in_motion=True)
            prev_item = at_index.previous_item() if at_index else self.sequence[-1]
            removed_x = 0

            if prev_item and prev_item.in_motion:
                # Because the previous item is in motion, we need to consider its beginning our beginning
                # We move at_index back to the first in_motion item and keep track
                # of the length, because we expect that length will be removed
                # during self.item.insert
                prev_item = prev_item.previous_item(skip_in_motion=True)
                existing_x = at_index.x if at_index else self.sequence.length

                if prev_item:
                    at_index = prev_item.next_item()
                else:
                    at_index = self.sequence[0]

                removed_x = existing_x - at_index.x
                print 'moved back to {0}, removed is {1}'.format(at_index.x, removed_x)

            old_x = at_index.x if at_index else self.sequence.length
            self.seq_index = index
            self.orig_next_item = index < len(self.sequence) and self.sequence[index] or None
            self.orig_next_item_trans_length = self.orig_next_item and self.orig_next_item.transition_length
            print index, x, old_x

            # Hit the mark "x"
            self.item.insert(self.sequence, index, 0 if at_start else old_x - x + (at_index.transition_length if at_index else 0))

            if self.orig_next_item:
                print 'next trans len {0} - ({1} - {2}) == {3}'.format(self.item.length, old_x, x, self.item.length - (old_x - x))
                # Retain this item's position in spite of any removals/insertions in self.item.insert
                self.orig_next_item.update(transition_length=self.item.length - (old_x - x) - removed_x)

            if at_start:
                # Move the sequence to compensate for insertions at the beginning
                print 'moving to {0} - ({1} - {2}) == {3}'.format(self.sequence.x, old_x, x, self.sequence.x - (old_x - x))
                self.original_x = self.sequence.x
                self.sequence.update(x=self.sequence.x - (old_x - x) - removed_x)

            return True

        def reset(self):
            if self.seq_index is None:
                return

            self.item.reset()
            self.sequence.update(x=self.original_x)

            if self.orig_next_item:
                self.orig_next_item.update(transition_length=self.orig_next_item_trans_length)
                self.orig_next_item = None
                self.orig_next_item_trans_length = None

            self.seq_item = None

        def finish(self):
            self.item.finish()

    class ClipManipulator(object):
        '''Manipulates a lone clip.'''

        class ClipSequenceable(Sequenceable):
            def __init__(self, manip):
                self.item = manip.item
                self.length = manip.item.length
                self.min_fadeout_point = 0
                self.max_fadein_point = self.length
                self.placeholder = PlaceholderItem(manip.item)
                self.seq_item = None

            def insert(self, seq, index, transition_length):
                self.seq_item = SequenceItem(source=self.item.source,
                    length=self.item.length,
                    offset=self.item.offset,
                    transition_length=transition_length,
                    type=self.item.type(),
                    in_motion=True)

                seq.insert(index, self.seq_item)

                if self.item.space:
                    self.item.space[self.item.z] = self.placeholder

            def reset(self):
                if self.seq_item:
                    del self.seq_item.sequence[self.seq_item.index]

                if not self.item.space:
                    print 'restoring item'
                    self.placeholder.space[self.placeholder.z] = self.item
                    print 'item restored at ' + str(self.item.z)

            def finish(self):
                if self.seq_item:
                    self.seq_item.update(in_motion=False)

                if self.placeholder.space:
                    self.placeholder.space.remove(self.placeholder)

            @property
            def transition_length(self):
                return self.seq_item.transition_length

            def set_transition_length(self, length):
                self.seq_item.update(transition_length=length)

        def __init__(self, item, grab_x, grab_y):
            self.item = item

            self.original_x = item.x
            self.original_y = item.y
            self.original_space = item.space
            self.offset_x = item.x - grab_x
            self.offset_y = item.y - grab_y
            self.seq_item_op = None

        def can_set_space_item(self, space, x, y):
            return True

        def set_space_item(self, space, x, y):
            print 'clip.set_space_item'
            self._undo_sequence()

            self.item.update(x=x + self.offset_x, y=y + self.offset_y, in_motion=True)
            return True

        def can_set_sequence_item(self, sequence, x, operation):
            return self.set_sequence_item(sequence, x, operation, do_it=False)

        def set_sequence_item(self, sequence, x, operation, do_it=True):
            if operation == 'add':
                if not self.seq_item_op or not isinstance(self.seq_item_op, ItemManipulator.SequenceAddMap) or self.seq_item_op.sequence != sequence:
                    if self.seq_item_op:
                        self.seq_item_op.reset()

                    self.seq_item_op = ItemManipulator.SequenceAddMap(sequence, self.ClipSequenceable(self))

                if do_it:
                    return self.seq_item_op.set(x + self.offset_x)
                else:
                    return self.seq_item_op.can_set(x + self.offset_x)

            return False

        def _undo_sequence(self):
            if self.seq_item_op:
                self.seq_item_op.reset()
                self.seq_item_op = None

        def reset(self):
            self.set_space_item(None, self.original_x - self.offset_x, self.original_y - self.offset_y)
            self.item.update(in_motion=False)

        def finish(self):
            if self.seq_item_op:
                self.seq_item_op.finish()
                self.seq_item_op = None

            self.item.update(in_motion=False)
            return True

    class SequenceItemGroupManipulator(object):
        class SequenceItemGroupSequenceable(Sequenceable):
            def __init__(self, manip):
                self.items = manip.items
                self.length = manip.length

                self.min_fadeout_point = manip.items[-1].x + min(0, manip.items[-1].transition_length) - manip.items[0].x

                next_item = manip.items[1] if len(manip.items) > 1 else None
                self.max_fadein_point = next_item.x if next_item else self.length

                print 'min_fadeout = {0}, max_fadein = {1}, length = {2}'.format(self.min_fadeout_point, self.max_fadein_point, self.length)

            def insert(self, seq, index, transition_length):
                self.items[0].update(transition_length=transition_length)
                print 'about to insert at {0}'.format(index)
                seq[index:index] = self.items

            def reset(self):
                if self.items[0].sequence:
                    del self.items[0].sequence[self.items[0].index:self.items[0].index + len(self.items)]

            def finish(self):
                pass

            @property
            def transition_length(self):
                return self.items[0].transition_length

            def set_transition_length(self, length):
                self.items[0].update(transition_length=length)

        '''Manipulates a set of adjacent sequence items.'''
        def __init__(self, items, grab_x, grab_y):
            self.items = items
            self.original_sequence = items[0].sequence
            self.original_x = items[0].x + self.original_sequence.x
            self.offset_x = self.original_x - grab_x
            self.offset_y = self.original_sequence.y - grab_y
            self.seq_item_op = None
            self.space_item = None

            self.length = items[-1].x + items[-1].length - items[0].x
            self.original_sequence = items[0].sequence
            self.original_sequence_index = items[0].index
            self.original_next = items[-1].next_item()
            self.original_next_trans_length = self.original_next and self.original_next.transition_length
            self.orig_trans_length = items[0].transition_length
            self.home = True

        def _remove_from_home(self):
            if not self.home:
                return

            del self.original_sequence[self.original_sequence_index:self.original_sequence_index + len(self.items)]

            if self.original_sequence_index == 0:
                self.original_sequence.update(x=self.original_sequence.x + self.length
                    - self.original_next.transition_length if self.original_next else 0)

            if self.original_next:
                self.original_next.update(transition_length=0 if self.original_sequence_index == 0 else (self.original_next_trans_length - self.length))

            self.home = False

        def _send_home(self):
            if self.home:
                return

            self.original_sequence[self.original_sequence_index:self.original_sequence_index] = self.items
            self.items[0].update(transition_length=self.orig_trans_length)

            if self.original_sequence_index == 0:
                self.original_sequence.update(x=self.original_x)

            if self.original_next:
                self.original_next.update(transition_length=self.original_next_trans_length)

            self.home = True

        def can_set_space_item(self, space, x, y):
            # It's always possible
            return True

        def set_space_item(self, space, x, y):
            self._undo_sequence()
            self._remove_from_home()

            if self.space_item and self.space_item.space == space:
                self.space_item.update(x=x + self.offset_x, y=y + self.offset_y)
                return True

            self._undo_space()
            self.items[0].update(transition_length=0)

            if len(self.items) == 1:
                # Manifest as clip
                self.space_item = Clip(x=x + self.offset_x, y=y + self.offset_y,
                    height=self.original_sequence.height, length=self.items[0].length,
                    source=self.items[0].source, type=self.items[0].type(),
                    offset=self.items[0].offset, in_motion=True)
            else:
                # Manifest as new sequence
                self.space_item = Sequence(x=x + self.offset_x, y=y + self.offset_y,
                    height=self.original_sequence.height, items=self.items,
                    type=self.items[0].type())

            space.insert(0, self.space_item)
            return True

        def can_set_sequence_item(self, sequence, x, operation):
            return self.set_sequence_item(sequence, x, operation, do_it=False)

        def set_sequence_item(self, sequence, x, operation, do_it=True):
            self._undo_space()

            if not self.items[0].in_motion:
                for item in self.items:
                    item.update(in_motion=True)

            if operation == 'add':
                if not self.seq_item_op or not isinstance(self.seq_item_op, ItemManipulator.SequenceAddMap) or self.seq_item_op.sequence != sequence:
                    if self.seq_item_op:
                        self.seq_item_op.reset()

                    self.seq_item_op = ItemManipulator.SequenceAddMap(sequence, self.SequenceItemGroupSequenceable(self))

                if do_it:
                    self._remove_from_home()
                    return self.seq_item_op.set(x + self.offset_x)
                else:
                    return self.seq_item_op.can_set(x + self.offset_x)

            return False

        def _undo_sequence(self):
            if self.seq_item_op:
                self.seq_item_op.reset()
                self.seq_item_op = None

        def _undo_space(self):
            if self.space_item:
                self.space_item.space.remove(self.space_item)
                self.space_item = None

        def reset(self):
            self._undo_space()
            self._undo_sequence()
            self._send_home()

            for item in self.items:
                item.update(in_motion=False)

        def finish(self):
            for item in self.items:
                item.update(in_motion=False)

            if self.space_item:
                self.space_item.update(in_motion=False)

            return True

    class SequenceManipulator(object):
        '''Manipulates an entire existing sequence.'''
        def __init__(self, item, grab_x, grab_y):
            self.seq = item
            self.original_x = item.x
            self.original_y = item.y
            self.original_space = item.space
            self.offset_x = item.x - grab_x
            self.offset_y = item.y - grab_y

        def can_set_space_item(self, space, x, y):
            return True

        def set_space_item(self, space, x, y):
            self.seq.update(x=x + self.offset_x, y=y + self.offset_y)
            return True

        def can_set_sequence_item(self, sequence, x, operation):
            return False

        def set_sequence_item(self, sequence, x, operation):
            pass

        def reset(self):
            self.seq.update(x=self.original_x, y=self.original_y)

        def finish(self):
            return True

    def __init__(self, items, grab_x, grab_y):
        self.items = items
        self.manips = []
        seq_items = []

        for item in items:
            if isinstance(item, Clip):
                self.manips.append(self.ClipManipulator(item, grab_x, grab_y))
            elif isinstance(item, Sequence):
                self.manips.append(self.SequenceManipulator(item, grab_x, grab_y))
            elif isinstance(item, SequenceItem):
                seq_items.append(item)

        # Sort and combine the sequence items
        for seq, itemlist in itertools.groupby(sorted(seq_items, cmp=lambda a, b: cmp(a.sequence, b.sequence) or cmp(a.index, b.index)), key=lambda a: a.sequence):
            self.manips.append(self.SequenceItemGroupManipulator(list(itemlist), grab_x, grab_y))

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


