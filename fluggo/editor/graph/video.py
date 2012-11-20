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
from fluggo.editor import model, plugins
from fluggo.media import process

from fluggo import logging
_log = logging.getLogger(__name__)

class SpaceVideoManager(plugins.VideoStream):
    class ItemWatcher(object):
        def __init__(self, owner, canvas_item, workspace_item, stream):
            self.owner = owner
            self.canvas_item = canvas_item
            self.workspace_item = workspace_item
            self.canvas_item.updated.connect(self.handle_updated)
            self.stream = stream
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

    def __init__(self, canvas_space, source_list):
        self.workspace = process.VideoWorkspace()
        format = canvas_space.video_format
        plugins.VideoStream.__init__(self, self.workspace, format)

        self.canvas_space = canvas_space
        self.canvas_space.item_added.connect(self.handle_item_added)
        self.canvas_space.item_removed.connect(self.handle_item_removed)
        self.source_list = source_list
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

        stream = None
        offset = 0

        if isinstance(item, model.Sequence):
            stream = SequenceVideoManager(item, self.source_list, self.format)
        elif hasattr(item, 'source'):
            stream = model.VideoSourceRefConnector(self.source_list, item.source, model_obj=item)
            offset = item.offset

        self.follow_alerts(stream)
        workspace_item = self.workspace.add(x=item.x, length=item.length, z=item.z, offset=offset, source=stream)

        watcher = self.ItemWatcher(self, item, workspace_item, stream)
        self.watchers[id(item)] = watcher
        self.watchers_sorted.add(watcher)

    def handle_item_removed(self, item):
        if item.type() != 'video':
            return

        watcher = self.watchers.pop(id(item))
        watcher.unwatch()
        self.unfollow_alerts(watcher.stream)
        self.watchers_sorted.remove(watcher)
        self.workspace.remove(watcher.workspace_item)

class SequenceVideoManager(plugins.VideoStream):
    class ItemWatcher(plugins.VideoStream):
        '''An ItemWatcher constructs the video for one clip in a sequence. It includes
        the "out" transition but not the "in" transition. If there is a gap before the
        clip (transition_length < 0), that's included, too.'''

        def __init__(self, owner, seq, seq_item):
            self.owner = owner
            self.seq = seq
            self.seq_item = seq_item
            self.connector = model.VideoSourceRefConnector(owner.source_list,
                ref=seq_item.source, model_obj=seq_item)

            # Source A is the current clip, B is the next one (unless there's a gap, next_item.transition_length < 0)
            self.source_a = process.VideoPassThroughFilter(self.connector, start_frame=0)
            self.gap_proxy = process.VideoPassThroughFilter(self.source_a)
            self.source_b = process.VideoPassThroughFilter(None)

            # Define a curve representing the transition (a linear one)
            # The first point stays at -1 and holds source_a (-1 because fade_point can go to zero)
            # The second point (fade_point) begins the transition to source_b (at length - next_item.transition_length)
            # The third point completes the transition and should be placed at the length of the clip
            self.mix_b = process.AnimationFunc()
            self.mix_b.add(process.POINT_HOLD, -1.0, 0.0)
            self.fade_point = self.mix_b.add(process.POINT_LINEAR, 0.0, 0.0)
            self.out_point = self.mix_b.add(process.POINT_HOLD, 0.0, 1.0)

            self.mix_filter = process.VideoMixFilter(self.gap_proxy, self.source_b, self.mix_b)
            plugins.VideoStream.__init__(self, self.mix_filter)
            self.follow_alerts(self.connector)

    def __init__(self, sequence, source_list, format):
        self.seqfilter = process.VideoSequence()
        plugins.VideoStream.__init__(self, self.seqfilter, format)

        self.sequence = sequence
        self.source_list = source_list

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
        self.follow_alerts(watcher)
        self.watchers.insert(item.index, watcher)
        self.seqfilter.insert(item.index, (watcher, 0, item.length))

        self._handle_item_updated(item, offset=item.offset, source=item.source,
            length=item.length, transition_length=item.transition_length)

        # We don't have the source for the next clip, so grab it
        watcher = self.watchers[item.index]
        next_watcher = item.index + 1 < len(self.watchers) and self.watchers[item.index + 1]

        if next_watcher:
            watcher.source_b.set_source(next_watcher.connector)

    def _handle_items_removed(self, start, stop):
        # Get enough info to send an update
        start_frame = self.watchers[start].seq_item.x
        end_frame = self.seqfilter.get_start_frame(len(self.seqfilter) - 1) + self.seqfilter[-1][2] - 1

        # Remove the watchers in this range
        for watcher in self.watchers[start:stop]:
            self.unfollow_alerts(watcher)

        del self.watchers[start:stop]

        for i in range(stop - 1, start - 1, -1):
            del self.seqfilter[i]

        # Redo the item afterward (now at start)
        if start < len(self.watchers):
            item = self.watchers[start].seq_item

            self._handle_item_updated(item,
                transition_length=item.transition_length)
        elif len(self.watchers):
            # There is no item afterward; clear source_b and reset its transition_point
            watcher = self.watchers[-1]
            item = watcher.seq_item

            watcher.source_b.set_source(None)
            watcher.fade_point.frame = item.length - item.transition_length

        self.frames_updated(start_frame, end_frame)

    def _handle_item_updated(self, item, **kw):
        if frozenset(('offset', 'source', 'transition_length', 'length')) .isdisjoint(frozenset(list(kw.keys()))):
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
            watcher.source_b.set_source(next_watcher.connector)

        if prev_item:
            prev_length = prev_item.length - prev_item.transition_length
            prev_watcher.source_b.offset = item.offset - (prev_length - item.transition_length)
            prev_watcher.source_b.set_source(watcher.connector)

        watcher.source_a.offset = item.offset + max(0, item.transition_length)

        # If there's a gap before, use the gap proxy to produce it
        watcher.gap_proxy.offset = min(0, item.transition_length)

        if 'offset' in kw:
            self.frames_updated(start_frame - item.transition_length, start_frame + length - 1)

        if 'source' in kw:
            watcher.connector.set_ref(item.source)
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
                prev_watcher.fade_point.frame = prev_length - max(item.transition_length, 0)
                self.frames_updated(start_frame - item.transition_length - max(old_trans_length - item.transition_length, 0),
                    self.sequence.length + max(0, old_length - length) - 1)
            else:
                self.frames_updated(start_frame + min(old_fade_point, mid_width),
                    self.sequence.length + max(0, old_length - length) - 1)

            self.seqfilter[item.index] = (watcher, 0, length)

        if 'transition' in kw:
            pass

class EffectVideoManager(plugins.VideoPassThroughStream):
    class ItemWatcher(plugins.VideoPassThroughStream):
        '''An ItemWatcher constructs the video for one effect in the stack. It
        responds to bypass events from the effect to enable or disable it.'''

        def __init__(self, owner, effect):
            self.owner = owner
            self.effect = effect
            self.input_stream = plugins.VideoPassThroughStream(None)

            # Lucky for us, the effect object can directly create its effect
            self.filter = effect.create_filter(self.input_stream)

            source = self.source if effect.bypass else self.filter

            plugins.VideoPassThroughStream.__init__(self, self.input_stream)

            effect.bypass_changed.connect(self._handle_bypass_changed)

            if not effect.bypass:
                self.follow_alerts(self.filter)

        def _handle_bypass_changed(self, effect):
            if effect.bypass:
                self.set_stream(self.input_stream)
                self.unfollow_alerts(self.filter)
            else:
                self.set_stream(self.filter)
                self.follow_alerts(self.filter)

    def __init__(self, input_stream, effect_stack):
        plugins.VideoPassThroughStream.__init__(self, input_stream)
        self.input_stream = input_stream

        self.effect_stack = effect_stack
        self.effect_stack.effect_added.connect(self.handle_effect_added)
        self.effect_stack.effect_removed.connect(self.handle_effect_removed)

        self.watchers = {}
        self.watchers_sorted = sortlist.SortedList(keyfunc=lambda a: a.effect._index, index_attr='index')

        for effect in effect_stack:
            self.handle_effect_added(effect)

    def handle_effect_added(self, effect):
        watcher = self.ItemWatcher(self, effect)
        self.watchers[id(effect)] = watcher
        self.watchers_sorted.add(watcher)

        _log.debug('Adding effect {0}', effect.name)

        # Wire it up with the others
        if watcher.index == 0:
            _log.debug('Feeding with initial input')
            watcher.input_stream.set_stream(self.input_stream)
        else:
            _log.debug('Feeding with [{0}] input', watcher.index - 1)
            watcher.input_stream.set_stream(self.watchers_sorted[watcher.index - 1])

        if watcher.index == len(self.watchers) - 1:
            _log.debug('Using as output')
            self.set_stream(watcher)
        else:
            _log.debug('Using as input to [{0}]', watcher.index + 1)
            self.watchers_sorted[watcher.index + 1].input_stream.set_stream(watcher)

        self.follow_alerts(watcher)

    def handle_effect_removed(self, effect):
        watcher = self.watchers.pop(id(item))
        self.unfollow_alerts(watcher)
        index = watcher.index

        if index == len(self.watchers) - 1:
            self.set_stream(self.watchers_sorted[index - 1])
        elif index == 0:
            self.watchers_sorted[index + 1].input_stream.set_stream(self.input_stream)
        else:
            self.watchers_sorted[index + 1].input_stream.set_stream(self.watchers_sorted[index - 1])

        self.watchers_sorted.remove(watcher)


