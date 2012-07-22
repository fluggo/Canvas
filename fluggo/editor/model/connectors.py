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

    def __init__(self, asset_list, ref, model_obj=None):
        plugins.VideoStream.__init__(self)

        self.asset_list = asset_list
        self.ref = ref
        self.model_obj = model_obj
        self.asset = None
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
            if self.asset:
                self.asset = None

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
            if not isinstance(self.ref, sources.AssetStreamRef):
                self._clear()
                return

            # Handle missing sources, failure to bring online, and missing streams
            try:
                self.asset = self.asset_list[self.ref.asset_path]
            except KeyError:
                self._clear()
                self._error = plugins.Alert('Reference refers to asset "' + self.ref.asset_path + '", which doesn\'t exist.',
                    model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                self.show_alert(self._error)
                return

            if not self.asset.is_source:
                self._clear()
                self._error = plugins.Alert('Reference refers to asset "' + self.ref.asset_path + '" which is not a video source.',
                    model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                self.show_alert(self._error)

            try:
                self.source = self.asset.get_source()
            except:
                self._clear()
                self._error = plugins.Alert('Error while getting source from asset',
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
                    self._error = plugins.Alert('Unable to bring source "' + self.ref.asset_path + '" online.',
                        model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                    self.show_alert(self._error)

                return

            try:
                self.stream = self.source.get_stream(self.ref.stream)
            except KeyError:
                self._clear()
                self._error = plugins.Alert('Can\'t find stream "' + self.ref.stream + '" in source "' + self.ref.asset_path + '".',
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


class AudioSourceRefConnector(plugins.AudioStream):
    # Really, this has almost the exact same behavior as the above; maybe
    # combine the two
    '''Resolves a reference into an audio stream.

    This class publishes alerts for any error that happens when finding the
    stream.'''

    def __init__(self, asset_list, ref, model_obj=None):
        plugins.AudioStream.__init__(self)

        self.asset_list = asset_list
        self.ref = ref
        self.model_obj = model_obj
        self.asset = None
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
            if self.asset:
                self.asset = None

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
            if not isinstance(self.ref, sources.AssetStreamRef):
                self._clear()
                return

            # Handle missing sources, failure to bring online, and missing streams
            try:
                self.asset = self.asset_list[self.ref.asset_path]
            except KeyError:
                self._clear()
                self._error = plugins.Alert('Reference refers to asset "' + self.ref.asset_path + '", which doesn\'t exist.',
                    model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                self.show_alert(self._error)
                return

            if not self.asset.is_source:
                self._clear()
                self._error = plugins.Alert('Reference refers to asset "' + self.ref.asset_path + '" which is not an audio source.',
                    model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                self.show_alert(self._error)

            try:
                self.source = self.asset.get_source()
            except:
                self._clear()
                self._error = plugins.Alert('Error while getting source from asset',
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
                    self._error = plugins.Alert('Unable to bring source "' + self.ref.asset_path + '" online.',
                        model_obj=self.model_obj, icon=plugins.AlertIcon.Error)
                    self.show_alert(self._error)

                return

            try:
                self.stream = self.source.get_stream(self.ref.stream)
            except KeyError:
                self._clear()
                self._error = plugins.Alert('Can\'t find stream "' + self.ref.stream + '" in source "' + self.ref.asset_path + '".',
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

