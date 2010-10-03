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

from fluggo import sortlist, signal
from fluggo.editor import canvas
from fluggo.media import process

class SpaceVideoManager(process.VideoPassThroughFilter):
    class ItemWatcher(object):
        def __init__(self, owner, canvas_item, workspace_item):
            self.owner = owner
            self.canvas_item = canvas_item
            self.workspace_item = workspace_item
            self.canvas_item.updated.connect(self.handle_updated)
            self._z_order = 0

        def handle_updated(self, **kw):
            # Raise the frames_updated signal if the content of frames changed
            if 'x' in kw or 'width' in kw or 'offset' in kw:
                old_x, old_width, old_offset = self.workspace_item.x, self.workspace_item.width, self.workspace_item.offset
                new_x, new_width, new_offset = kw.get('x', old_x), kw.get('width', old_width), kw.get('offset', old_offset)
                old_right, new_right = old_x + old_width, new_x + new_width

                self.workspace_item.update(
                    x=kw.get('x', old_x),
                    width=kw.get('width', old_width),
                    offset=kw.get('offset', old_offset)
                )

                # Update the currently displayed frame if it's in a changed region
                if old_x != new_x:
                    self.owner.frames_updated(min(old_x, new_x), max(old_x, new_x) - 1)

                if old_right != new_right:
                    self.owner.frames_updated(min(old_right, new_right), max(old_right, new_right) - 1)

                if old_x - old_offset != new_x - new_offset:
                    self.owner.frames_updated(max(old_x, new_x), min(old_right, new_right) - 1)

            if 'y' in kw or 'z' in kw:
                self.owner.watchers_sorted.move(self.z_order)

        @property
        def z_order(self):
            return self._z_order

        @z_order.setter
        def z_order(self, value):
            self._z_order = value

            if value != self.workspace_item.z:
                self.workspace_item.update(z=value)
                self.owner.frames_updated(self.workspace_item.x, self.workspace_item.x + self.workspace_item.width - 1)

        def unwatch(self):
            self.canvas_item.updated.disconnect(self.handle_updated)

    def __init__(self, canvas_space, source_list):
        self.workspace = process.VideoWorkspace()
        process.VideoPassThroughFilter.__init__(self, self.workspace)

        self.canvas_space = canvas_space
        self.canvas_space.item_added.connect(self.handle_item_added)
        self.canvas_space.item_removed.connect(self.handle_item_removed)
        self.source_list = source_list
        self.frames_updated = signal.Signal()
        self.watchers = {}
        self.watchers_sorted = sortlist.SortedList(keyfunc=lambda a: a.canvas_item.z_sort_key(), index_attr='z_order')

        for item in canvas_space:
            if item.type() == 'video':
                self.handle_item_added(item)

    def handle_item_added(self, item):
        if not isinstance(item, canvas.Clip):
            return

        if item.type() != 'video':
            return

        source = None

        if isinstance(item.source, canvas.StreamSourceRef):
            source = self.source_list.get_stream(item.source.source_name, item.source.stream_index)

        workspace_item = self.workspace.add(x=item.x, width=item.width, z=item.z, offset=item.offset, source=source)

        watcher = self.ItemWatcher(self, item, workspace_item)
        self.watchers[id(item)] = watcher
        self.watchers_sorted.add(watcher)

    def handle_item_removed(self, item):
        if item.type() != 'video':
            return

        watcher = self.watchers.pop(id(item))
        watcher.unwatch()
        self.watchers_sorted.remove(watcher)
        self.workspace.remove(watcher.workspace_item)

