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

# k
# kr
# kri
# kris
# krist
# kristi
# kristin
# kristina!!!!!

from fluggo import sortlist, signal
from fluggo.editor import model
from fluggo.media import process, sources

class SpaceVideoManager(sources.VideoSource):
    class ItemWatcher(object):
        def __init__(self, owner, canvas_item, workspace_item):
            self.owner = owner
            self.canvas_item = canvas_item
            self.workspace_item = workspace_item
            self.canvas_item.updated.connect(self.handle_updated)
            self._z_order = 0

        def handle_updated(self, **kw):
            # Raise the frames_updated signal if the content of frames changed
            if 'x' in kw or 'length' in kw or 'offset' in kw:
                old_x, old_length, old_offset = self.workspace_item.x, self.workspace_item.length, self.workspace_item.offset
                new_x, new_length, new_offset = kw.get('x', old_x), kw.get('length', old_length), kw.get('offset', old_offset)
                old_right, new_right = old_x + old_length, new_x + new_length

                self.workspace_item.update(
                    x=kw.get('x', old_x),
                    length=kw.get('length', old_length),
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
                self.owner.frames_updated(self.workspace_item.x, self.workspace_item.x + self.workspace_item.length - 1)

        def unwatch(self):
            self.canvas_item.updated.disconnect(self.handle_updated)

    def __init__(self, canvas_space, source_list, format):
        self.workspace = process.VideoWorkspace()
        sources.VideoSource.__init__(self, self.workspace, format)

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
        if not isinstance(item, model.Item):
            return

        if item.type() != 'video':
            return

        source = None
        offset = 0

        if isinstance(item, model.Sequence):
            source = SequenceVideoManager(item, self.source_list, self.format)
        elif hasattr(item, 'source') and isinstance(item.source, model.StreamSourceRef):
            source = self.source_list.get_stream(item.source.source_name, item.source.stream_index)
            offset = item.offset

        workspace_item = self.workspace.add(x=item.x, length=item.length, z=item.z, offset=offset, source=source)

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

class SequenceVideoManager(sources.VideoSource):
    class ItemWatcher(process.VideoPassThroughFilter):
        def __init__(self, owner, seq, seq_item):
            self.owner = owner
            self.seq = seq
            self.seq_item = seq_item
            self.source_a = process.VideoPassThroughFilter(None)
            self.source_b = process.VideoPassThroughFilter(None)

            self.mix_b = process.AnimationFunc()
            self.mix_b.add(process.POINT_HOLD, 0.0, 0.0)
            self.fade_point = self.mix_b.add(process.POINT_LINEAR, 0.0, 0.0)
            self.out_point = self.mix_b.add(process.POINT_HOLD, 0.0, 1.0)

            self.mix_filter = process.VideoMixFilter(self.source_a, self.source_b, self.mix_b)
            process.VideoPassThroughFilter.__init__(self, self.mix_filter)

    def __init__(self, sequence, source_list, format):
        self.seqfilter = process.VideoSequence()
        sources.VideoSource.__init__(self, self.seqfilter, format)

        self.sequence = sequence
        self.source_list = source_list
        self.frames_updated = signal.Signal()

        self.sequence.item_added.connect(self._handle_item_added)
        self.sequence.items_removed.connect(self._handle_items_removed)
        self.sequence.item_updated.connect(self._handle_item_updated)

        self.watchers = []

        for item in self.sequence:
            self._handle_item_added(item)

    def unwatch(self):
        self.sequence.item_added.disconnect(self._handle_item_added)
        self.sequence.items_removed.disconnect(self._handle_items_removed)
        self.sequence.item_updated.disconnect(self._handle_item_updated)

    def _handle_item_added(self, item):
        # Insert a watcher at the same index
        watcher = self.ItemWatcher(self, self.sequence, item)
        self.watchers.insert(item.index, watcher)
        self.seqfilter.insert(item.index, (watcher, 0, item.length))

        self._handle_item_updated(item, offset=item.offset, source=item.source,
            length=item.length, transition_length=item.transition_length)

        # We don't have the source for the next clip, so grab it
        watcher = self.watchers[item.index]
        next_watcher = item.index + 1 < len(self.watchers) and self.watchers[item.index + 1]

        if next_watcher:
            watcher.source_b.set_source(next_watcher.source_a.source())

    def _handle_items_removed(self, start, stop):
        # Get enough info to send an update
        start_frame = self.watchers[start].seq_item.x
        end_frame = self.seqfilter.get_start_frame(len(self.seqfilter) - 1) + self.seqfilter[-1][2] - 1

        # Remove the watchers in this range
        del self.watchers[start:stop]

        for i in range(stop - 1, start - 1, -1):
            del self.seqfilter[i]

        # Redo the item afterward (now at start)
        if start < len(self.watchers):
            item = self.watchers[start].seq_item

            self._handle_item_updated(item, source=item.source,
                transition_length=item.transition_length)
        elif len(self.watchers):
            # There is no item afterward; clear source_b and reset its transition_point
            watcher = self.watchers[-1]
            item = watcher.seq_item

            watcher.source_b.set_source(None)
            watcher.fade_point.frame = item.length - item.transition_length

        self.frames_updated(start_frame, end_frame)

    def _handle_item_updated(self, item, **kw):
        if frozenset(('offset', 'source', 'transition_length', 'length')) .isdisjoint(frozenset(kw.keys())):
            return

        watcher = self.watchers[item.index]
        prev_watcher = item.index > 0 and self.watchers[item.index - 1]
        prev_item = prev_watcher and prev_watcher.seq_item
        next_watcher = item.index + 1 < len(self.watchers) and self.watchers[item.index + 1]
        next_item = next_watcher and next_watcher.seq_item

        start_frame = item.x + item.transition_length
        length = item.length - item.transition_length
        mid_width = length

        # Update source offsets regardless; this is cheap
        if next_item:
            mid_width -= next_item.transition_length
            watcher.source_b.offset = next_item.offset - mid_width

        if prev_item:
            prev_length = prev_item.length - prev_item.transition_length
            prev_watcher.source_b.offset = item.offset - (prev_length - item.transition_length)

        watcher.source_a.offset = item.offset + item.transition_length

        if 'offset' in kw:
            self.frames_updated(start_frame - item.transition_length, start_frame + length - 1)

        if 'source' in kw:
            source = None

            if isinstance(item.source, model.StreamSourceRef):
                source = self.source_list.get_stream(item.source.source_name, item.source.stream_index)

            watcher.source_a.set_source(source)

            if prev_watcher:
                prev_watcher.source_b.set_source(source)

            self.frames_updated(start_frame - item.transition_length, start_frame + length - 1)

        if 'transition_length' in kw or 'length' in kw:
            # Altering length messes with this item's length and transition points;
            # Altering transition length also messes with this item's offset
            # and the previous item's transition points
            old_fade_point = int(round(watcher.fade_point.frame))
            old_length = int(round(watcher.out_point.frame))

            watcher.out_point.frame = length
            watcher.fade_point.frame = mid_width

            if 'transition_length' in kw and prev_item:
                old_trans_length = int(round(prev_watcher.out_point.frame - prev_watcher.fade_point.frame))

                prev_watcher.out_point.frame = prev_length
                prev_watcher.fade_point.frame = prev_length - item.transition_length
                self.frames_updated(start_frame - item.transition_length - max(old_trans_length - item.transition_length, 0),
                    self.sequence.length + max(0, old_length - length) - 1)
            else:
                self.frames_updated(start_frame + min(old_fade_point, mid_width),
                    self.sequence.length + max(0, old_length - length) - 1)

            self.seqfilter[item.index] = (watcher, 0, length)

        if 'transition' in kw:
            pass


