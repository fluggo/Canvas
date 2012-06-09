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
from fluggo import logging
from PyQt4.QtCore import *
from PyQt4.QtGui import *

logger = logging.getLogger(__name__)

_Placement = collections.namedtuple('_Placement', 'min max index')

def _split_sequence_items_by_overlap(items):
    '''Splits the *items* (which should all belong to the same sequence and be
    sorted by index) into a list of lists of items, where items in each list overlap,
    but do not overlap with items in the other lists (and thus each list can be
    moved independently).'''
    if not items:
        return []

    next_list = [items[0]]
    result = [next_list]

    for item in items[1:]:
        if item.index != next_list[-1].index + 1 or next_list[-1].transition_length >= 0:
            next_list = [item]
            result.append(next_list)
        else:
            next_list.append(item)

    return result

def _split_sequence_items_by_adjacency(items):
    '''Splits the *items* (which should all belong to the same sequence and be
    sorted by index) into a list of lists of items, where items in each list are
    adjacent (have indexes that differ by one)'''
    if not items:
        return []

    next_list = [items[0]]
    result = [next_list]
    start_offset = items[0].x

    for item in items[1:]:
        if item.index != next_list[-1].index + 1:
            next_list = [item]
            result.append(next_list)
        else:
            next_list.append(item)

    return result

class SequenceItemsMover:
    '''Class for moving any group of sequence items.

    The items should either all belong to the same sequence or not belong to a
    sequence at all. If they don't belong to a sequence, they need to already be
    in the right order.'''
    def __init__(self, items):
        if items[0].sequence:
            # Sort and set positions by index and x
            items = sorted(items, key=lambda a: a.index)
            base_x = items[0].x

            self.overlap_movers = list(
                SequenceOverlapItemsMover(group, group[0].x - base_x) for group in
                _split_sequence_items_by_overlap(items))
        else:
            # Update _x attributes before we go into this
            x = 0
            index = 0

            for item in items:
                if index != 0:
                    x -= item.transition_length

                item._x = x
                item._index = index

                x += item.length
                index += 1

            self.overlap_movers = list(
                SequenceOverlapItemsMover(group, group[0].x) for group in
                _split_sequence_items_by_overlap(items))

    def to_item(self, height=10.0, x=0, y=0):
        '''Return a space Item for containing the items from this SequenceItemsMover.
        If there is one item, this will be a Clip. Otherwise, it will be a Sequence.

        The items should already be detached from any sequence. After this method,
        the items will all belong to the new sequence, if any.'''
        if len(self.overlap_movers) == 1 and len(self.overlap_movers[0].items) == 1:
            # Make a clip
            item = self.overlap_movers[0].items[0]

            return Clip(
                x=x, y=y,
                length=item.length,
                height=height,
                type=item.type(),
                source=item.source,
                offset=item.offset,
                in_motion=item.in_motion)

        seq_items = []
        last_x = 0

        for group in self.overlap_movers:
            items = group.clone_items()
            items[0].update(transition_length=-(group.offset - last_x))
            seq_items.extend(items)
            last_x = group.offset + group.length

        return Sequence(x=x, y=y, type=seq_items[0].type(), items=seq_items,
            height=height, in_motion=self.overlap_movers[0].items[0].in_motion)

class SequenceOverlapItemsMover:
    '''Mover for overlapping items belonging to the same sequence.'''
    def __init__(self, items, offset=None):
        '''Creates an overlapping items mover with the given *items*.
        *offset* can be used to store the group's offset from other groups.'''
        self.items = items
        self.offset = offset

        # The items may not have x values, or they may not be accurate
        # Calculate the long way
        self.length = sum(items[i].length - (items[i].transition_length if i > 0 else 0)
            for i in range(len(items)))

        # max_fadeout_length: The maximum value for the next item's
        # transition_length, which is the distance from the end of our last item
        # to max(start of item, end of item's transition); since these items are
        # overlapping, the last item must have a positive transition_length
        self.max_fadeout_length = items[-1].length

        # Ditto, but at the beginning of the items
        self.max_fadein_length = items[0].length

        if len(items) > 1:
            self.max_fadeout_length -= items[-1].transition_length
            self.max_fadein_length -= items[1].transition_length

    def clone_items(self):
        '''Clones all of this mover's items. The cloned items will be homeless.'''
        return [item.clone() for item in self.items]

    def clone(self):
        '''Clones this mover and all of its items. The cloned items will be homeless.'''
        return SequenceOverlapItemsMover(self.clone_items(), offset=self.offset)

    @classmethod
    def from_clip(cls, clip):
        seq_item = SequenceItem(source=clip.source,
            length=clip.length,
            offset=clip.offset,
            transition_length=0,
            type=clip.type(),
            in_motion=clip.in_motion)

        return cls([seq_item])

class NoRoomError(Exception):
    def __init__(self, message='There is no room for the item.', *args, **kw):
        Exception.__init__(self, message, *args, **kw)

class AddOverlapItemsToSequenceCommand(QUndoCommand):
    def __init__(self, sequence, mover, x, parent=None):
        '''This is where the real add work is done. To add a clip to a sequence,
        convert it to a sequence item and add it to a SequenceOverlapItemsMover.
        *x* is space-relative. The clips should not belong to a sequence already.

        If the given mover can be placed at two indexes, it it is put at the
        lower index.'''
        QUndoCommand.__init__(self, 'Add overlapping items to sequence', parent)
        self.sequence = sequence
        self.mover = mover
        self.x = x

        if self.sequence.type() != self.mover.items[0].type():
            raise NoRoomError('The item type is incompatible with the sequence type.')

        # We do a check here, but we'll be doing it again at the actual insert
        if self.where_can_fit(x) is None:
            raise NoRoomError

        # BJC: Note that we don't calculate the new transition_lengths or keep
        # the next item here; this is because the nearby items are allowed to
        # change (in certain ways) between the creation of this command and its
        # execution
        self.orig_transition_length = self.mover.items[0].transition_length

    def redo(self):
        index = self.where_can_fit(self.x)

        if index is None:
            raise NoRoomError

        self.index = index

        x = self.x - self.sequence.x
        self.orig_sequence_x = self.sequence.x

        # at_index - Item at the insertion index
        at_index = self.sequence[index] if index < len(self.sequence) else None
        at_start = at_index and not at_index.previous_item()
        prev_item = at_index.previous_item() if at_index else self.sequence[-1]
        removed_x = 0

        old_x = at_index.x if at_index else self.sequence.length
        self.orig_next_item = index < len(self.sequence) and self.sequence[index] or None
        self.orig_next_item_trans_length = self.orig_next_item and self.orig_next_item.transition_length
        logger.debug('placing at {0}, x is {1}, old_x is {2}', index, x, old_x)

        # Hit the mark "x"
        self.mover.items[0].update(transition_length=
            0 if at_start else old_x - x + (at_index.transition_length if at_index else 0))
        self.sequence[index:index] = self.mover.items

        if self.orig_next_item:
            logger.debug('next trans len {0} - ({1} - {2}) == {3}', self.mover.length, old_x, x, self.mover.length - (old_x - x))
            # Retain this item's position in spite of any removals/insertions in self.item.insert
            self.orig_next_item.update(transition_length=self.mover.length - (old_x - x) - removed_x)

        if at_start:
            # Move the sequence to compensate for insertions at the beginning
            logger.debug('moving to {0} - ({1} - {2}) == {3}', self.sequence.x, old_x, x, self.sequence.x - (old_x - x))
            self.sequence.update(x=self.sequence.x - (old_x - x) - removed_x)

    def undo(self):
        # Pop the items out of the sequence (they will be homeless)
        del self.sequence[self.index:self.index + len(self.mover.items)]

        if self.sequence.x != self.orig_sequence_x:
            self.sequence.update(x=self.orig_sequence_x)

        # Reset the before-and-after
        self.mover.items[0].update(transition_length=self.orig_transition_length)

        if self.orig_next_item:
            self.orig_next_item.update(transition_length=self.orig_next_item_trans_length)

        del self.index
        del self.orig_next_item
        del self.orig_next_item_trans_length

    def determine_range(self, index):
        '''Determine the range where a clip will fit. These are tuples of (min, max, mark).
        *min* and *max* are offsets from the beginning of the scene. *mark* is a
        left-gravity mark in the sequence at the index. If the
        item can't fit at all at an index, None might be returned.'''
        if index < 0 or index > len(self.sequence):
            raise IndexError('index out of range')

        if index < len(self.sequence):
            seq_item = self.sequence[index]

            if seq_item.transition_length > 0 and seq_item.index > 0:
                # Previous item is locked in a transition with us and is here to stay
                return None

            # If the item before that is in motion, we have to ignore prev_item's
            # transition_length (which would otherwise be zero or less)
            prev_item = seq_item.previous_item()
            prev_prev_item = prev_item and prev_item.previous_item()

            # Find next_item so we can get its transition_length
            next_item = seq_item.next_item()

            _min = max(
                # Previous item's position plus any transition_length it has
                (prev_item.x +
                    (max(0, prev_item.transition_length) if prev_prev_item else 0))
                    # Or the space before the sequence if this item is first
                    # (but really, it could go as far back as it wants)
                    if prev_item else -self.mover.length,
                # The beginning of this clip (or the gap before it)
                seq_item.x + min(0, seq_item.transition_length)
                    - (self.mover.max_fadein_length if prev_item else self.mover.length))

            _max = (
                # At the item's start
                seq_item.x

                # But back to the beginning of the mover, so they don't overlap
                - self.mover.length

                # How much they can overlap
                + min(self.mover.max_fadeout_length,
                    seq_item.length - (next_item.transition_length if next_item else 0)))

            _min += self.sequence.x
            _max += self.sequence.x

            if not prev_item:
                _min = None
            elif _max < _min:
                return None

            return _Placement(_min, _max, index)
        else:
            # Final index
            prev_item = self.sequence[-1]

            # If the item before that is in motion, we have to ignore prev_item's
            # transition_length (which would otherwise be zero or less)
            prev_prev_item = prev_item and prev_item.previous_item()

            _min = max(
                # Previous item's position plus any transition_length it has
                prev_item.x +
                    (max(0, prev_item.transition_length) if prev_prev_item else 0),
                # End of the sequence minus how much fadein we can give it
                prev_item.x + prev_item.length - self.mover.max_fadein_length)

            _min += self.sequence.x

            return _Placement(_min, None, index)

    def where_can_fit(self, x):
        '''Returns index where the item would be inserted if it can fit, None if it won't.
        "x" is space-relative.'''
        # TODO: This would be faster as a binary search
        # Or a simple optimization would be to skip indexes where X is too low
        for _range in (self.determine_range(i) for i in range(len(self.sequence) + 1)):
            if not _range:
                continue

            if (_range.min is None or x >= _range.min) and (_range.max is None or x <= _range.max):
                return _range.index

        return None


class AddSequenceToSequenceCommand(QUndoCommand):
    def __init__(self, sequence, mover, x, parent=None):
        QUndoCommand.__init__(self, 'Add sequence to sequence', parent)
        '''Adds a given SequenceItemsMover to a *sequence* at the given scene-relative *x*.
        The mover's items are added directly, and therefore should not belong to
        a sequence; if you don't want this, you should produce a copy first.

        If the constructor raises a NoRoomError, the addition isn't possible.'''
        for group in mover.overlap_groups:
            AddOverlapItemsToSequenceCommand(sequence, group, x + group.offset)

class MoveSequenceOverlapItemsInPlaceCommand(QUndoCommand):
    def __init__(self, mover, offset, parent=None):
        '''Moves the given SequenceOverlapItemsMover back and forth in a sequence.
        This command does not change the index of the items, just their distance
        to the previous and next items. As such, you'll get a NoRoomError if you
        try to move them too far. The NoRoomError does not occur until redo(),
        but you can call check_room() early if you want.

        This command can be merged with another MoveOverlapItemsInPlaceCommand, provided
        they refer to the same *mover*.
        '''
        QUndoCommand.__init__(self, 'Move overlapping sequence items in place', parent)
        self.mover = mover
        self.offset = offset
        self.sequence = self.mover.items[0].sequence

        if not self.sequence:
            raise ValueError('The given items are not in a sequence.')

    def id(self):
        return id(MoveSequenceOverlapItemsInPlaceCommand)

    def mergeWith(self, command):
        if not isinstance(command, MoveSequenceOverlapItemsInPlaceCommand):
            return False

        if self.mover is not command.mover:
            return False

        # For future reference-- not that it matters here-- the order of events
        # is *this* command followed by the command given as a parameter.
        self.offset += command.offset

    def check_room(self):
        # TODO: We do not consider whether the items around us are in motion,
        # leading to an inefficiency that we don't know if all the items *can*
        # be moved until all the items are moved; this can be improved
        next_item = self.mover.items[-1].next_item()
        previous_item = self.mover.items[0].previous_item()

        if self.offset > 0 and next_item:
            next_next_item = next_item.next_item()

            max_offset = min(
                # How much room is left in the next item
                next_item.length
                    - max(next_next_item.transition_length if next_next_item else 0, 0)
                    - next_item.transition_length,
                # How much room is left in the max_fadeout_length
                self.mover.max_fadeout_length - next_item.transition_length)

            if self.offset > max_offset:
                raise NoRoomError

        if self.offset < 0 and previous_item:
            min_offset = -min(
                # How much room is left in the previous item
                previous_item.length
                    - self.mover.items[0].transition_length
                    - max(previous_item.transition_length, 0),
                # How much room is left in the max_fadein_length
                self.mover.max_fadein_length - self.mover.items[0].transition_length)

            if self.offset < min_offset:
                raise NoRoomError

    def redo(self):
        self.check_room()

        next_item = self.mover.items[-1].next_item()

        if next_item:
            next_item.update(transition_length=next_item.transition_length + self.offset)

        if self.mover.items[0].index == 0:
            # First index-- move the sequence
            self.sequence.update(x=self.sequence.x + self.offset)
        else:
            # Update our own transition_length
            self.mover.items[0].update(
                transition_length=self.mover.items[0].transition_length - self.offset)

    def undo(self):
        next_item = self.mover.items[-1].next_item()

        if next_item:
            next_item.update(transition_length=next_item.transition_length - self.offset)

        if self.mover.items[0].index == 0:
            # First index-- move the sequence
            self.sequence.update(x=self.sequence.x - self.offset)
        else:
            # Update our own transition_length
            self.mover.items[0].update(
                transition_length=self.mover.items[0].transition_length + self.offset)


class _Sequenceable(object):
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

class _SequenceAddMap(object):
    def __init__(self, sequence, item):
        '''Creates an instance of _SequenceAddMap.

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
            seq_item = self.sequence[index]

            # In motion: it'll be gone, consider a different index instead
            # Has a transition: Can't fit here regardless
            if seq_item.in_motion:
                return None

            # Find the previous original item
            prev_item = seq_item.previous_item(skip_in_motion=True)

            if seq_item.transition_length > 0 and prev_item and prev_item.index == seq_item.index - 1:
                # Previous item is locked in a transition with us and is here to stay
                return None

            # If the item before that is in motion, we have to ignore prev_item's
            # transition_length (which would otherwise be zero or less)
            prev_prev_item = prev_item and prev_item.previous_item()

            # Find next_item so we can get its transition_length (which we ignore if the next item is in_motion)
            next_item = seq_item.next_item()

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

            if (_range[0] is None or x >= _range[0]) and (_range[1] is None or x <= _range[1]):
                return _range[2]

        return None

    def can_set(self, x):
        '''Checks if the clip can be placed at x, which is space-relative.'''
        return self.where_can_fit(x) is not None

    def set(self, x):
        index = self.where_can_fit(x)

        if index is None:
            return False

        if self.seq_index is not None:
            # Adjust index to what it would be if the placed items weren't there
            if index > self.seq_index:
                index -= (self.orig_next_item.index if self.orig_next_item else len(self.sequence)) - self.seq_index

            if self.seq_index == index:
                # It's already in place, just adjust the transition_lengths
                x -= self.sequence.x
                current_x = self.sequence[self.seq_index].x

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
            logger.debug('moved back to {0}, removed is {1}', at_index.x, removed_x)

        old_x = at_index.x if at_index else self.sequence.length
        self.seq_index = index
        self.orig_next_item = index < len(self.sequence) and self.sequence[index] or None
        self.orig_next_item_trans_length = self.orig_next_item and self.orig_next_item.transition_length
        logger.debug('placing at {0}, x is {1}, old_x is {2}', index, x, old_x)

        # Hit the mark "x"
        self.item.insert(self.sequence, index, 0 if at_start else old_x - x + (at_index.transition_length if at_index else 0))

        if self.orig_next_item:
            logger.debug('next trans len {0} - ({1} - {2}) == {3}', self.item.length, old_x, x, self.item.length - (old_x - x))
            # Retain this item's position in spite of any removals/insertions in self.item.insert
            self.orig_next_item.update(transition_length=self.item.length - (old_x - x) - removed_x)

        if at_start:
            # Move the sequence to compensate for insertions at the beginning
            logger.debug('moving to {0} - ({1} - {2}) == {3}', self.sequence.x, old_x, x, self.sequence.x - (old_x - x))
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

class _ClipManipulator:
    '''Manipulates a lone clip.'''

    def __init__(self, item, grab_x, grab_y):
        self.item = item

        self.original_x = item.x
        self.original_y = item.y
        self.original_space = item.space
        self.offset_x = item.x - grab_x
        self.offset_y = item.y - grab_y

        self.item.update(in_motion=True)

        self.space_move_op = None
        self.seq_mover = None
        self.seq_item = None
        self.space_remove_op = None
        self.seq_add_op = None
        self.seq_move_op = None

    def set_space_item(self, space, x, y):
        self._undo_sequence()

        space_move_op = UpdateItemPropertiesCommand(self.item, x=x + self.offset_x, y=y + self.offset_y)
        space_move_op.redo()

        if self.space_move_op:
            self.space_move_op.mergeWith(space_move_op)
        else:
            self.space_move_op = space_move_op

    def set_sequence_item(self, sequence, x, operation):
        # TODO: I've realized there's a difference in model here;
        # the old way expected that if we failed to move the item, it would be
        # where we last successfully put it. Here, we back out of changes we made
        # previously. That's not really a bad thing: if we fail to place it, the
        # user should be told it's not going to work out they way they want.
        if self.seq_mover is None:
            self.seq_mover = SequenceOverlapItemsMover.from_clip(self.item)
            self.seq_item = self.seq_mover.items[0]

        if operation == 'add':
            if self.seq_item.sequence == sequence:
                # Try moving it in place
                offset = x - (sequence.x + self.seq_item.x) + self.offset_x
                command = None

                try:
                    command = MoveSequenceOverlapItemsInPlaceCommand(self.seq_mover, offset)
                    command.redo()

                    if self.seq_move_op:
                        self.seq_move_op.mergeWith(command)
                    else:
                        self.seq_move_op = command

                    return
                except NoRoomError:
                    # No room here; back out and try as a clip
                    pass

            if self.seq_item.sequence:
                # Back out so we can add it again
                self._undo_sequence(undo_remove=False)

            space_remove_op = None

            if self.item.space:
                space_remove_op = RemoveItemCommand(self.item.space, self.item)
                space_remove_op.redo()

            # TODO: If this next line raises a NoRoomError, meaning we haven't
            # placed the item anywhere, finish() needs to fail loudly, and the
            # caller needs to know it will fail
            self.seq_add_op = AddOverlapItemsToSequenceCommand(sequence, self.seq_mover, x + self.offset_x)
            self.seq_add_op.redo()
            self.seq_move_op = None
            self.space_remove_op = space_remove_op or self.space_remove_op
            return

        raise ValueError('Unsupported operation "{0}"'.format(operation))

    def _undo_sequence(self, undo_remove=True):
        if self.seq_move_op:
            self.seq_move_op.undo()
            self.seq_move_op = None

        if self.seq_add_op:
            self.seq_add_op.undo()
            self.seq_add_op = None

        if undo_remove and self.space_remove_op:
            self.space_remove_op.undo()
            self.space_remove_op = None

    def reset(self):
        self._undo_sequence()

        if self.space_move_op:
            self.space_move_op.undo()
            self.space_move_op = None

        self.item.update(in_motion=False)

    def finish(self):
        if self.space_remove_op and not self.seq_add_op:
            # Oops, this wasn't a complete action
            return None

        self.item.update(in_motion=False)

        if self.seq_item:
            self.seq_item.update(in_motion=False)

        return True

class RemoveAdjacentItemsFromSequenceCommand(QUndoCommand):
    '''Removes adjacent (or single) items from a sequence, trying not to disturb
    the timing in the sequence.

    This command may move the sequence or adjust the transition lengths of items
    to retain the sequence's timing.'''

    def __init__(self, items, parent=None):
        # Items supplied to this command need to be adjacent in the same sequence
        # TODO: How does this kind of command, which hangs onto old clips,
        # interact with asset name changes?
        #   If the user does change an asset name as it is, at the very least when
        #   they undo over this step, the graph manager will look for an asset
        #   that's not there (or worse, a different asset with the same name!).
        #   (1) Perhaps this can be solved with a kind of "global" command, one that
        #       appears on all stacks and undoes the global action when traversed.
        #   (2) Or we can reach into the undo stack and commit name changes there,
        #       too? Certain commands will listen to the asset list and modify items
        #       they hold?
        #   (3) Maybe there is only one undo stack to begin with? That kind of
        #       stack could undo name changes. -- IXNAY, users won't like that.
        #   (4) Or, as above, accept that we can't track all asset name changes,
        #       and leave it up to the user to do something smart. This at least
        #       can hold until I get a better idea on what to do.

        QUndoCommand.__init__(self, 'Delete adjacent item(s) from sequence', parent)

        for i in range(0, len(items) - 1):
            if items[i].index != items[i+1].index - 1:
                raise ValueError('This operation is only supported on adjacent items.')

        self.items = items
        self.original_sequence = items[0].sequence

        # Original position X in scene
        self.original_x = items[0].x + self.original_sequence.x

        self.length = items[-1].x + items[-1].length - items[0].x
        self.original_sequence = items[0].sequence
        self.original_sequence_index = items[0].index
        self.original_next = items[-1].next_item()
        self.original_next_trans_length = self.original_next and self.original_next.transition_length
        self.orig_trans_length = items[0].transition_length

    def redo(self):
        del self.original_sequence[self.original_sequence_index:self.original_sequence_index + len(self.items)]

        if self.original_sequence_index == 0:
            self.original_sequence.update(x=self.original_sequence.x + self.length
                - self.original_next.transition_length if self.original_next else 0)

        if self.original_next:
            self.original_next.update(transition_length=0 if self.original_sequence_index == 0 else (self.original_next_trans_length - self.length + self.orig_trans_length))

    def undo(self):
        self.original_sequence[self.original_sequence_index:self.original_sequence_index] = self.items
        self.items[0].update(transition_length=self.orig_trans_length)

        if self.original_sequence_index == 0:
            self.original_sequence.update(x=self.original_x)

        if self.original_next:
            self.original_next.update(transition_length=self.original_next_trans_length)

class RemoveItemCommand(QUndoCommand):
    '''Removes an item from its container.

    This really works for any item in any mutable list, so long as the list's
    index method can find it. But this means it can also work for spaces.

    Sequences have special requirements as far as keeping items where they are.
    Use the RemoveItemsFromSequenceCommand to handle those.'''
    def __init__(self, list_, item, parent=None):
        QUndoCommand.__init__(self, 'Delete item', parent)
        self.list = list_
        self.item = item

    def redo(self):
        self.index = self.list.index(self.item)
        del self.list[self.index]

    def undo(self):
        self.list.insert(self.index, self.item)

class RemoveItemsFromSequenceCommand(QUndoCommand):
    '''Removes any set of items from a sequence. Note that each item needs to
    belong to the same sequence and must be specified only once.

    If all the items of a sequence are specified, the whole sequence is removed.'''
    def __init__(self, items, parent=None):
        QUndoCommand.__init__(self, 'Delete item(s) from sequence', parent)

        if len(items) == len(items[0].sequence):
            # Just remove the whole sequence
            RemoveItemCommand(items[0].sequence.space, items[0].sequence, self)
        else:
            items = sorted(items, key=lambda a: a.index)

            for group in _split_sequence_items_by_adjacency(items):
                RemoveAdjacentItemsFromSequenceCommand(group, parent=self)

class _SequenceItemGroupManipulator(object):
    class SequenceItemGroupSequenceable(_Sequenceable):
        def __init__(self, manip):
            self.items = manip.items
            self.length = manip.length

            self.min_fadeout_point = manip.items[-1].x + min(0, manip.items[-1].transition_length) - manip.items[0].x

            next_item = manip.items[1] if len(manip.items) > 1 else None
            self.max_fadein_point = next_item.x if next_item else self.length

        def insert(self, seq, index, transition_length):
            self.items[0].update(transition_length=transition_length)
            logger.debug('about to insert at {0}', index)
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

    '''Manipulates a set of sequence items.'''
    def __init__(self, items, grab_x, grab_y):
        self.items = items
        self.mover = SequenceItemsMover(items)
        self.original_sequence = items[0].sequence
        self.original_x = items[0].x + self.original_sequence.x
        self.offset_x = self.original_x - grab_x
        self.offset_y = self.original_sequence.y - grab_y
        self.seq_item_op = None
        self.space_item = None

        self.length = items[-1].x + items[-1].length - items[0].x
        self.remove_command = None

        for item in self.items:
            item.update(in_motion=True)

    def _remove_from_home(self):
        if self.remove_command:
            return

        self.remove_command = RemoveAdjacentItemsFromSequenceCommand(self.items)
        self.remove_command.redo()

    def _send_home(self):
        if not self.remove_command:
            return

        self.remove_command.undo()
        self.remove_command = None

    def set_space_item(self, space, x, y):
        self._undo_sequence()
        self._remove_from_home()

        if self.space_item and self.space_item.space == space:
            self.space_item.update(x=x + self.offset_x, y=y + self.offset_y)
            return

        self._undo_space()

        self.space_item = self.mover.to_item(
            x=x + self.offset_x, y=y + self.offset_y,
            height=self.original_sequence.height)

        space.insert(0, self.space_item)

    def set_sequence_item(self, sequence, x, operation):
        self._undo_space()

        if operation == 'add':
            if not self.seq_item_op or not isinstance(self.seq_item_op, _SequenceAddMap) or self.seq_item_op.sequence != sequence:
                if self.seq_item_op:
                    self.seq_item_op.reset()

                self.seq_item_op = _SequenceAddMap(sequence, self.SequenceItemGroupSequenceable(self))

            self._remove_from_home()

            if not self.seq_item_op.set(x + self.offset_x):
                raise NoRoomError

            return

        raise ValueError('Unknown operation "{0}"'.format(operation))

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

class _SequenceManipulator(object):
    '''Manipulates an entire existing sequence.'''
    def __init__(self, item, grab_x, grab_y):
        self.seq = item
        self.original_x = item.x
        self.original_y = item.y
        self.original_space = item.space
        self.offset_x = item.x - grab_x
        self.offset_y = item.y - grab_y

    def set_space_item(self, space, x, y):
        self.seq.update(x=x + self.offset_x, y=y + self.offset_y)

    def set_sequence_item(self, sequence, x, operation):
        pass

    def reset(self):
        self.seq.update(x=self.original_x, y=self.original_y)

    def finish(self):
        return True

class ItemManipulator:
    # TODO
    # Identify adjacent items in a sequence and manipulate them as a unit
    # Identify groups and manipulate them as a unit
    # Find good algorithm for turning loose items into sequences
    #  ^    Scratch: Only the item/sequence directly grabbed (listed first in self.items)
    #       is placed in a sequence, and the rest arranged around it accordingly
    '''Moves clips, sequence items, and sequences'''

    def __init__(self, items, grab_x, grab_y):
        self.items = items
        self.manips = []
        seq_items = []

        for item in items:
            if isinstance(item, Clip):
                self.manips.append(_ClipManipulator(item, grab_x, grab_y))
            elif isinstance(item, Sequence):
                self.manips.append(_SequenceManipulator(item, grab_x, grab_y))
            elif isinstance(item, SequenceItem):
                seq_items.append(item)

        # Sort and combine the sequence items
        for seq, itemlist in itertools.groupby(sorted(seq_items, key=lambda a: (a.sequence, a.index)), key=lambda a: a.sequence):
            self.manips.append(_SequenceItemGroupManipulator(list(itemlist), grab_x, grab_y))


    def set_space_item(self, space, x, y):
        # None of our set_space_item manips raise exceptions
        for manip in self.manips:
            manip.set_space_item(space, x, y)



    def set_sequence_item(self, sequence, x, operation):
        # TODO: This is wrong. Only primary should attempt to go into this
        # sequence; figure out what to do with the others
        for manip in self.manips:
            manip.set_sequence_item(sequence, x, operation)


    def reset(self):
        for manip in self.manips:
            manip.reset()

    def finish(self):
        if all(manip.finish() for manip in self.manips):
            return True

        self.reset()
        return False


