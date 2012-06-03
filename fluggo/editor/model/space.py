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
from fluggo.editor.model import sources

class Space(sources.Source, ezlist.EZList):
    def __init__(self, name, video_format, audio_format):
        sources.Source.__init__(self, name)
        ezlist.EZList.__init__(self)
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

    def index(self, item, i = None, j = None):
        if self != item._space:
            raise ValueError

        # Shortcut for common case
        if i is None and j is None:
            return item._z

        i = 0 if i is None else i
        i = i + len(self) if i < 0 else i
        i = 0 if i < 0 else i

        j = len(self) if j is None else j
        j = j + len(self) if j < 0 else j
        j = 0 if j < 0 else j

        result = item._z

        if result >= i and result < j:
            return result

    def _replace_range(self, start, stop, items):
        old_item_set = frozenset(self._items[start:stop])
        new_item_set = frozenset(items)

        for item in (old_item_set - new_item_set):
            self.item_removed(item)
            item.kill()

        self._items[start:stop] = items
        self._update_marks(start, stop, len(items))

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
    return dumper.represent_mapping('!CanvasSpace', {'items': data._items})

def _space_construct(loader, node):
    mapping = loader.construct_mapping(node)
    result = Space('', mapping['video_format'], mapping['audio_format'])
    result._items = mapping['items']
    return result

yaml.add_representer(Space, _space_represent)
yaml.add_constructor('!CanvasSpace', _space_construct)

