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

class SortedList(collections.Sequence):
    def __init__(self, iterable=None, keyfunc=None):
        if iterable:
            self.list = list(iterable)
            self.list.sort(key=keyfunc)

            if keyfunc:
                self.keys = [keyfunc(item) for item in self.list]
            else:
                self.keys = self.list[:]
        else:
            self.list = []
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

        Use this to re-sort a single item after its key has changed.
        This method does the re-sort in-place, so it's faster than removing the item
        and adding it again.
        '''
        item = self.list[index]
        key = item

        if self.keyfunc:
            key = self.keyfunc(item)

        if self.keys[index] == key:
            return

        new_index = bisect.bisect_left(self.keys, key)

        # Shift everything to make room for the item's new spot
        if new_index > index:
            self.list[index:new_index] = self.list[index + 1:new_index + 1]
            self.keys[index:new_index] = self.keys[index + 1:new_index + 1]
        else:
            self.list[index + 1:new_index + 1] = self.list[index:new_index]
            self.keys[index + 1:new_index + 1] = self.keys[index:new_index]

        self.list[new_index] = item
        self.keys[new_index] = key

    def __getitem__(self, index):
        return self.list[index]

    def __delitem__(self, index):
        del self.list[index]
        del self.keys[index]

    def __len__(self):
        return len(self.list)

    def find(self, min_key=None, max_key=None):
        min_index = 0
        max_index = len(self.list)

        if min_key:
            min_index = bisect.bisect_left(self.keys, min_key)

        if max_key:
            max_index = bisect.bisect_right(self.keys, max_key)

        return self.list[min_index:max_index]

