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

import collections

class EZList(collections.MutableSequence):
    '''
    Allows implementing __setitem__ and __delitem__ on a list as
    replace operations. Implement _replace_range in your code.
    '''
    def _replace_range(self, start, stop, items):
        '''
        Replace indexes start through stop with items.

        my_list._replace_range(start, stop, items)

        start - Index in the current list to start replacement.
        stop - Index to stop replacement at; replace range(start, stop).
        items - List of items to put in place. This list is not at
            all guaranteed to match the number of items being replaced.

        This is an abstract method. Implement it in your own class.
        A minimal implementation might be:

        def _replace_range(self, start, stop, items):
            self._internal_list[start:stop] = items
        '''
        raise NotImplementedError

    def insert(self, index, value):
        self[index:index] = [value]

    def __setitem__(self, key, value):
        start, stop, step = None, None, None
        is_slice = isinstance(key, slice)
        items = None

        if is_slice:
            start, stop, step = key.indices(len(self))
            items = value
        else:
            start, stop, step = key, key + 1, 1
            items = [value]

        if step == 1:
            self._replace_range(start, stop, items)
        else:
            # Reduce it to solid ranges
            i = 0

            for j in range(start, stop, step):
                if i < len(clips):
                    self._replace_range(j, j + 1, [clips[i]])
                else:
                    self._replace_range(j, j + 1, [])

    def __delitem__(self, key):
        # Just a special case of __setitem__ above
        if isinstance(key, slice):
            self[key] = []
        else:
            self[key:key + 1] = []

