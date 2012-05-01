# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009-10 Brian J. Crowell <brian@fluggo.com>
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

import collections, bisect
from fluggo import ezlist

class AutoIndexList(ezlist.EZList):
    '''
    List that optionally stores an item's index on the item in *index_attr*.
    If given, *iterable* determines the list's initial contents.

    '''
    def __init__(self, iterable=None, index_attr=None):
        self.index_attr = index_attr

        if iterable:
            self.list = list(iterable)

            if index_attr:
                for i in range(len(self.list)):
                    setattr(self.list[i], index_attr, i)
        else:
            self.list = []

    def _replace_range(self, start, stop, items):
        if self.index_attr:
            for i, item in enumerate(items, start):
                setattr(item, self.index_attr, i)

        self.list[start:stop] = items

        if self.index_attr and stop - start != len(items):
            for i, item in enumerate(self.list[start + len(items):], start + len(items)):
                setattr(item, self.index_attr, i)

    def index(self, item):
        if self.index_attr:
            return getattr(item, self.index_attr)

        return self.list.index(item)

    def __getitem__(self, index):
        return self.list[index]

    def __len__(self):
        return len(self.list)

class SortedList(collections.Sequence):
    def __init__(self, iterable=None, keyfunc=None, index_attr=None):
        '''
        Create a sorted list.

        iterable - Optional initial items to sort.
        keyfunc - Optional function that produces sortable keys. The function
            should accept one argument, a list item, and produce an object that
            can be compared with other keys. Keys must be immutable; if SortedList
            suspects that the item has changed, it will call keyfunc to request
            a new key.
        index_attr - If a string, store the item's index on the item as an attribute
            with this name.
        '''
        if iterable:
            self.list = list(iterable)
            self.list.sort(key=keyfunc)

            self.list = AutoIndexList(self.list, index_attr)

            if keyfunc:
                self.keys = [keyfunc(item) for item in self.list]
            else:
                self.keys = self.list[:]
        else:
            self.list = AutoIndexList(index_attr=index_attr)
            self.keys = []

        self.keyfunc = keyfunc

    def add(self, item):
        key = item

        if self.keyfunc:
            key = self.keyfunc(item)

        index = bisect.bisect_left(self.keys, key)
        self.list.insert(index, item)
        self.keys.insert(index, key)

    def index(self, item):
        if self.list.index_attr:
            return self.list.index(item)

        key = item

        if self.keyfunc:
            key = self.keyfunc(item)

        index = bisect.bisect_left(self.keys, key)

        while True:
            if index >= len(self.list):
                raise ValueError

            if self.keys[index] != key:
                raise ValueError

            if self.list[index] == item:
                return index

            index += 1

    def remove(self, item):
        del self[self.index(item)]

    def move(self, index):
        '''
        Check the key on the item at index and move it to its correct sort position.
        '''
        # BJC: The original algorithm was incredibly error-prone, so until the need arises to write a new one,
        # this is what we've got.
        item = self.list[index]
        del self[index]
        self.add(item)

    def __getitem__(self, index):
        return self.list[index]

    def __delitem__(self, index):
        del self.list[index]
        del self.keys[index]

    def __len__(self):
        return len(self.list)

    def __str__(self):
        return '[' + ', '.join(str(item) for item in self) + ']'

    def __repr__(self):
        return '[' + ', '.join(repr(item) for item in self) + ']'

    def find(self, min_key=None, max_key=None):
        min_index = 0
        max_index = len(self.list)

        if min_key:
            min_index = bisect.bisect_left(self.keys, min_key)

        if max_key:
            max_index = bisect.bisect_right(self.keys, max_key)

        return self.list[min_index:max_index]

