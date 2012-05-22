# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2012 Brian J. Crowell <brian@fluggo.com>
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

from fluggo import logging
from fluggo.editor import plugins
from fluggo.editor.model import sources

_log = logging.getLogger(__name__)

class VideoSourceRefConnector(plugins.VideoStream):
    '''Resolves a reference into a video stream.

    This class publishes alerts for any error that happens when finding the
    stream.'''

    def __init__(self, source_list, ref, model_obj=None):
        plugins.VideoStream.__init__(self)

        self.source_list = source_list
        self.ref = ref
        self.model_obj = model_obj
        self.source = None
        self.stream = None
        self._error = None

        self.connect()

        # TODO: Handle sources appearing, disappearing, and going online/offline
        # TODO: Also potentially handle transforms

    def set_ref(self, ref):
        self.ref = ref
        self.connect()

    def _clear(self):
        self.set_base_filter(None, new_range=(None, None))
        self.set_format(None)

    def connect(self):
        try:
            if self.source:
                self.unfollow_alerts(self.source)
                self.source = None

            if self.stream:
                self.unfollow_alerts(self.stream)
                self.stream = None

            if self._error:
                self.hide_alert(self._error)
                self._error = None

            if not self.ref:
                self._clear()
                return

            # TODO: Handle ad-hoc sources
            if not isinstance(self.ref, sources.StreamSourceRef):
                self._clear()
                return

            # Handle missing sources, failure to bring online, and missing streams
            try:
                self.source = self.source_list[self.ref.source_name]
            except KeyError:
                self._clear()
                self._error = plugins.Alert('Reference refers to source "' + self.ref.source_name + '" which doesn\'t exist.',
                    model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                self.show_alert(self._error)
                return

            self.follow_alerts(self.source)

            if self.source.offline:
                try:
                    self.source.bring_online()
                except:
                    self._clear()
                    self._error = plugins.Alert('Error while bringing source online',
                        model_obj=self.model_obj, icon=plugins.AlertIcon.Error, exc_info=True)
                    self.show_alert(self._error)
                    return

            if self.source.offline:
                self._clear()

                if not len(self.source.alerts):
                    self._error = plugins.Alert('Unable to bring source "' + self.ref.source_name + '" online.',
                        model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                    self.show_alert(self._error)

                return

            try:
                self.stream = self.source.get_stream(self.ref.stream)
            except KeyError:
                self._clear()
                self._error = plugins.Alert('Can\'t find stream "' + self.ref.stream + '" in source "' + self.ref.source_name + '".',
                    model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                self.show_alert(self._error)
                return

            self.follow_alerts(self.stream)

            self.set_format(None)
            self.set_base_filter(self.stream, new_range=self.stream.defined_range)
            self.set_format(self.stream.format)
        except:
            _log.debug('Error while resolving reference', exc_info=True)
            self._clear()
            self._error = plugins.Alert('Error while resolving reference', model_obj=self.model_obj, icon=plugins.AlertIcon.Error, exc_info=True)
            self.show_alert(self._error)

