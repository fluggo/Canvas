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
_log = logging.getLogger(__name__)

from ._base import *
from ._source import *

class CodecPlugin(Plugin):
    '''Provides encoders and decoders for one or more stream formats.

    Encoders and decoders transform from CodecPacketSource to AudioStream or VideoStream and back.
    They do not expose any intermediate steps, such as CodedImageSources, except
    where options for reconstructing images are exposed in the codec's settings.'''

    @classmethod
    def get_all_codecs(cls):
        '''Return a list of all codecs supported by this plugin.'''
        return []

class Codec(object):
    '''Sets this codec's initial place among other codecs. If a codec thinks
    it might be particularly good at decoding/encoding a format, it might bump
    this higher.'''
    default_priority = 0

    '''A reference to the plugin that provided this codec.'''
    plugin = None

    name = None

    '''A frozenset of format URNs this codec supports.'''
    format_urns = frozenset()

    '''A URN that uniquely identifies this codec.'''
    urn = None

    # Video/audio/subtitle/etc.
    stream_type = None

    '''True if the codec can decode streams.'''
    can_decode = False

    '''True if the codec can encode streams.'''
    can_encode = False

    @classmethod
    def get_localized_name(self):
        '''Return the localized name of this codec, or None if the codec doesn't
        support the current locale.'''
        return None

    def get_definition(self):
        '''Return a dictionary of parameters that can be passed to the codec's
        constructor to re-create the object.'''
        return {}

    def create_encoder(self, stream, offset, length):
        '''Return a CodecPacketSource for the given stream and definition (which can be None if a source is trying to discover).

        *defined_range* identifies which part of the stream should be encoded.

        The returned object needs to have a get_definition() method, which sources
        can pass to this method to re-create the encoder, and a codec attribute which
        could be used to identify this codec.'''
        raise NotImplementedError

    def create_decoder(self, packet_stream, offset, length):
        '''Return a stream object (VideoStream, AudioStream, etc.) to decode the given packet stream and definition (which can be None if a source is trying to discover).

        *defined_range* is supplied by the source, and should indicate where and how long the
        stream is.

        The returned object needs to have a get_definition() method, which sources
        can pass to this method to re-create the decoder, and a codec attribute which
        could be used to identify this codec.'''
        raise NotImplementedError

class NotConnectedError(Exception):
    pass

class _DecoderConnector(object):
    '''Finds a video codec to decode the given stream.

    This class publishes alerts for any error that happens when finding the
    codec.'''

    def __init__(self, packet_stream, format_urn, offset, length, model_obj=None, codec_urn=None, definition=None):
        '''Creates a connector for the given *packet_stream*.

        If *codec_urn* is given, the connector tries to find the exact decoder
        and create it with the given *definition*. Otherwise, it tries to find
        a codec that can decode *format_urn* and creates it with no settings.'''
        if not packet_stream:
            raise ValueError('packet_stream cannot be None')

        self._pktstream = packet_stream
        self._offset = offset
        self._length = length
        self._start_definition = definition or {}
        self._format_urn = format_urn
        self._codec_urn = codec_urn
        self.model_obj = model_obj

        self.codec = None
        self.decoder = None
        self._error = None

        self.connect()

        # TODO: Handle codecs appearing (and disappearing?)

    def _clear(self):
        self.set_base_filter(None, new_range=(None, None))
        self.set_format(None)

    def get_definition(self):
        if not self.decoder:
            return self._start_definition

        return self.decoder.get_definition()

    def connect(self):
        try:
            if self.decoder:
                self.unfollow_alerts(self.decoder)
                self.decoder = None

            self.codec = None

            if self._error:
                self.hide_alert(self._error)
                self._error = None

            if self._codec_urn:
                # We're out to find a specific codec
                codec_class = PluginManager.get_codec_by_urn(self._codec_urn)
                decoder = None
                codec = None

                if not codec_class:
                    self._clear()
                    self._error = Alert('Could not find codec "' + self._codec_urn + '". Check to see that it is installed and enabled.',
                        model_obj=self.model_obj, icon=AlertIcon.Error)
                    self.show_alert(self._error)
                    return

                try:
                    codec = codec_class(**self._start_definition)
                except:
                    self._clear()
                    self._error = Alert('Error while creating codec instance',
                        model_obj=self.model_obj, icon=AlertIcon.Error, exc_info=True)
                    self.show_alert(self._error)
                    return

                try:
                    self.decoder = codec.create_decoder(self._pktstream, self._offset, self._length)
                    self.codec = codec
                except:
                    self._clear()
                    self._error = Alert('Error while creating decoder',
                        model_obj=self.model_obj, icon=AlertIcon.Error, exc_info=True)
                    self.show_alert(self._error)
                    return
            else:
                # Try to find one that handles the format
                codecs = PluginManager.find_decoders(self._format_urn)

                if not len(codecs):
                    self._clear()
                    self._error = Alert('No codecs found to handle format "' + self._format_urn + '".',
                        model_obj=self.model_obj, icon=AlertIcon.Error)
                    self.show_alert(self._error)
                    return

                for codec_class in codecs:
                    codec = None

                    try:
                        codec = codec_class()
                    except:
                        _log.warning('Error while creating instance of codec {0}', codec.__name__, exc_info=True)
                        continue

                    try:
                        self.decoder = codec.create_decoder(self._pktstream, self._offset, self._length)
                        self.codec = codec
                    except:
                        _log.warning('Error while trying codec {0}', codec.urn, exc_info=True)

                if not self.decoder:
                    self._clear()
                    self._error = Alert('No codecs found to handle format "' + self._format_urn + '". All codecs that were tried failed. See log for details.',
                        model_obj=self.model_obj, icon=AlertIcon.Error)
                    self.show_alert(self._error)
                    return

            self.follow_alerts(self.decoder)

            # TODO: If we set the defined_range here, can we skip giving it to the codec? (Answer: probably)
            # What do offset and length mean for codecs that don't start at zero? (Answer: codecs probably shouldn't start at anything but zero)
            self.set_format(None)
            self.set_base_filter(self.decoder, new_range=self.decoder.defined_range)
            self.set_format(self.decoder.format)
        except:
            _log.warning('Error while finding codec for format "' + self._format_urn + '"', exc_info=True)
            self._clear()
            self._error = Alert('Error while finding codec for format "' + self._format_urn + '"', model_obj=self.model_obj, icon=AlertIcon.Error, exc_info=True)
            self.show_alert(self._error)

class VideoDecoderConnector(_DecoderConnector, VideoStream):
    def __init__(self, *args, **kw):
        VideoStream.__init__(self)
        _DecoderConnector.__init__(self, *args, **kw)

class AudioDecoderConnector(_DecoderConnector, AudioStream):
    def __init__(self, *args, **kw):
        AudioStream.__init__(self)
        _DecoderConnector.__init__(self, *args, **kw)

