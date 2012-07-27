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
from fluggo.editor import model, plugins
from fluggo.media import process

class SpaceAudioManager(plugins.AudioStream):
    class ItemWatcher(object):
        def __init__(self, owner, canvas_item, workspace_item, stream):
            self.owner = owner
            self.canvas_item = canvas_item
            self.workspace_item = workspace_item
            self.canvas_item.updated.connect(self.handle_updated)
            self.stream = stream

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

        def unwatch(self):
            self.canvas_item.updated.disconnect(self.handle_updated)

    def __init__(self, canvas_space, source_list):
        self.workspace = process.AudioWorkspace()
        format = canvas_space.audio_format
        plugins.AudioStream.__init__(self, self.workspace, format)

        self.canvas_space = canvas_space
        self.canvas_space.item_added.connect(self.handle_item_added)
        self.canvas_space.item_removed.connect(self.handle_item_removed)
        self.source_list = source_list
        self.watchers = {}

        for item in canvas_space:
            if item.type() == 'audio':
                self.handle_item_added(item)

    def handle_item_added(self, item):
        if not isinstance(item, model.Item):
            return

        if item.type() != 'audio':
            return

        stream = None
        offset = 0

        if isinstance(item, model.Sequence):
            raise NotImplementedError('Need a SequenceAudioManager here')
        elif hasattr(item, 'source'):
            stream = model.AudioSourceRefConnector(self.source_list, item.source, model_obj=item)
            offset = item.offset

        self.follow_alerts(stream)
        workspace_item = self.workspace.add(x=item.x, length=item.length, offset=offset, source=stream)

        watcher = self.ItemWatcher(self, item, workspace_item, stream)
        self.watchers[id(item)] = watcher

    def handle_item_removed(self, item):
        if item.type() != 'audio':
            return

        watcher = self.watchers.pop(id(item))
        watcher.unwatch()
        self.unfollow_alerts(watcher.stream)
        self.workspace.remove(watcher.workspace_item)

