# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2010-2 Brian J. Crowell <brian@fluggo.com>
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
from fluggo.media import libav, process
from fluggo.media.basetypes import *
from fluggo.editor import plugins
from PyQt4.QtCore import *
from PyQt4.QtGui import *
import fractions

_log = logging.getLogger(__name__)

# TODO: When you think about it, the only real specialization here is the handling
# of DV video... the audio should probably be generalized

class _DVError(Exception):
    pass

class LibavDvSourcePlugin(plugins.SourcePlugin):
    plugin_urn = u'urn:fluggo.com/canvas/plugins:libav-dv'

    @property
    def name(self):
        return u'Libav DV Source'

    @property
    def description(self):
        return u'Provides special DV support from Libav'

    def create_source(self, name, definition):
        '''Return a source from the given definition.

        The *definition* will be a definition returned by Source.definition().'''
        return _DVSource.from_definition(self, name, definition)

    def create_source_from_file(self, name, path):
        '''Return a new source if one can be created from the given file. If the plugin
        can't read the file (or doesn't support files), it can raise an error or return None.

        This method is only called to attempt to read new files. Existing
        sources are re-created using create_source().'''
        source = _DVSource(self, name, self, path)
        source.bring_online()

        if not source.offline:
            return source

class _DVSource(plugins.Source):
    translation_context = u'fluggo.editor.plugins.libav._DVSource'

    def __init__(self, name, plugin, path):
        self._plugin = plugin
        self.path = path
        self._load_alert = None

        plugins.Source.__init__(self, name)

    @property
    def plugin(self):
        return self._plugin

    def bring_online(self):
        if not self.offline:
            return

        if self._load_alert:
            self.hide_alert(self._load_alert)
            self._load_alert = None

        try:
            container = libav.AVContainer(self.path)

            #if container.format_name != 'dv':
            #    raise _DVError('Not a DV file.')

            # We expect stream 0 to be video and stream 1 to be audio
            # TODO: Allow it to be reversed; split-stream containers can do that to us
            if container.streams[0].type != 'video':
                raise _DVError('Expected video on stream 0.')

            if container.streams[0].codec != 'dvvideo':
                raise _DVError('Expected dvvideo codec on stream 0.')

            if container.streams[1].type != 'audio':
                raise _DVError('Expected audio on stream 1.')

            if container.streams[1].codec != 'pcm_s16le':
                raise _DVError('Expected pcm_s16le codec on stream 1.')

            video = container.streams[0]
            video_length = video.frame_count

            if not video_length:
                # We need to give our best guess
                if video.duration:
                    video_length = int(round(fractions.Fraction(video.duration) * video.time_base * video.real_frame_rate))
                elif container.duration:
                    video_length = int(round(fractions.Fraction(container.duration, 1000000) * video.real_frame_rate))
            else:
                video_length = int(video_length)

            self._video = _DVVideo(self, video_length)

            audio = container.streams[1]
            audio_length = audio.frame_count

            if not audio_length:
                # We need to give our best guess
                if audio.duration and audio.rea:
                    audio_length = int(round(fractions.Fraction(audio.duration) * audio.time_base * audio.sample_rate))
                elif container.duration:
                    audio_length = int(round(fractions.Fraction(container.duration, 1000000) * audio.sample_rate))
            else:
                audio_length = int(audio_length)

            self._audio = _DVAudio(self, audio_length)

            self.offline = False
        except _DVError as ex:
            self._load_alert = plugins.Alert(id(self), unicode(ex), icon=plugins.AlertIcon.Error, source=self.name, actions=[
                QAction(u'Retry', None, statusTip=u'Try bringing the source online again', triggered=self._retry_load)])
            self.show_alert(self._load_alert)
        except Exception as ex:
            # TODO: This would probably be easier if we set up specific exceptions that
            # bring_online could throw, and had some other handler call it. That way,
            # we could make specific handlers for specific situations (example: file is
            # missing, so remap to another file) without requiring every source to code it
            # separately.
            self._load_alert = plugins.Alert(id(self), u'Unexpected ' + ex.__class__.__name__ + u': ' + unicode(ex), icon=plugins.AlertIcon.Error, source=self.name, actions=[
                QAction(u'Retry', None, statusTip=u'Try bringing the source online again', triggered=self._retry_load)], exc_info=True)
            self.show_alert(self._load_alert)

    def _retry_load(self, checked):
        self.bring_online()

    @classmethod
    def from_definition(cls, plugin, name, definition):
        _log.debug('Producing DV source from definition {0!r}', definition)
        return cls(name, plugin, definition['path'])

    def get_definition(self):
        return {'path': self.path}

    @property
    def file_path(self):
        return self.path

    def get_stream_formats(self):
        # TODO: Should this method return length/valid frames as well?
        if self.offline:
            raise plugins.SourceOfflineError

        return [
            (u'Video', self._video.format),
            (u'Audio', self._audio.format)
        ]

    def get_stream(self, name):
        if self.offline:
            raise plugins.SourceOfflineError

        if name == u'Video':
            return self._video

        if name == u'Audio':
            return self._audio

        raise KeyError

class _DVVideo(plugins.VideoStream):
    def __init__(self, source, length):
        # TODO: pixel_aspect_ratio, distinguish between 4:3 and 16:9 video
        # TODO: specify pulldown here, and if so, do we decode before output?
        #   I expect not; we give interlaced and let transform take care of it

        self.source = source
        base_filter = self.get_static_stream()

        video_format = plugins.VideoFormat(interlaced=True,
            full_frame=box2i(-8, -1, -8 + 720 - 1, -1 + 480 - 1),
            active_area=box2i(0, -1, 704 - 1, -1 + 480 - 1),
            pixel_aspect_ratio=fractions.Fraction(10, 11),
            white_point='D65',
            frame_rate=fractions.Fraction(30000, 1001))
        
        plugins.VideoStream.__init__(self, base_filter, video_format, (0, length - 1))

    def get_static_stream(self):
        demuxer = libav.AVDemuxer(self.source.path, 0)
        decoder = libav.AVVideoDecoder(demuxer, 'dvvideo')
        return process.DVReconstructionFilter(decoder)

class _DVAudio(plugins.AudioStream):
    def __init__(self, source, length):
        # TODO: audio, allow for quad-channel on output
        # TODO: Take sample rate from container; split-stream files may trip
        #   us up with 44.1kHz sample rates

        self.source = source
        base_filter = self.get_static_stream()

        audio_format = plugins.AudioFormat(sample_rate=48000,
            channel_assignment=('FrontLeft', 'FrontRight'))
        
        plugins.AudioStream.__init__(self, base_filter, audio_format, (0, length - 1))

    def get_static_stream(self):
        demuxer = libav.AVDemuxer(self.source.path, 1)
        return libav.AVAudioDecoder(demuxer, 'pcm_s16le', 2)

