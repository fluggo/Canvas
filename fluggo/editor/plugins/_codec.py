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

from ._base import *
from ._source import *

class CodecPlugin(Plugin):
    '''Provides encoders and decoders for one or more stream formats.

    Encoders and decoders transform from CodecPacketSource to AudioStream or VideoStream and back.
    They do not expose any intermediate steps, such as CodedImageSources, except
    where options for reconstructing images are exposed in the codec's settings.'''

    def get_all_codecs(self):
        '''Return a list of all codecs supported by this plugin.'''
        return []

class Codec(object):
    '''Sets this codec's initial place among other codecs. If a codec thinks
    it might be particularly good at decoding/encoding a format, it might bump
    this higher.'''
    default_priority = 0

    @property
    def plugin(self):
        '''Return a reference to the plugin that created this source.'''
        return None

    @property
    def name(self):
        raise NotImplementedError

    @property
    def format_urns(self):
        '''Return a frozenset of format URNs this codec supports.'''
        raise NotImplementedError

    @property
    def urn(self):
        '''Return a URN that uniquely identifies this codec.'''
        raise NotImplementedError

    @property
    def stream_type(self):
        # Video/audio/subtitle/etc.
        raise NotImplementedError

    @property
    def can_decode(self):
        '''Return true if the codec can decode streams.'''
        return False

    @property
    def can_encode(self):
        '''Return true if the codec can encode streams.'''
        return False

    def create_encoder(self, stream, offset, length, definition):
        '''Return a CodecPacketSource for the given stream and definition (which can be None if a source is trying to discover).

        *defined_range* identifies which part of the stream should be encoded.

        The returned object needs to have a get_definition() method, which sources
        can pass to this method to re-create the encoder, and a codec attribute which
        could be used to identify this codec.'''
        raise NotImplementedError

    def create_decoder(self, packet_stream, offset, length, definition):
        '''Return a stream object (VideoStream, AudioStream, etc.) to decode the given packet stream and definition (which can be None if a source is trying to discover).

        *defined_range* is supplied by the source, and should indicate where and how long the
        stream is.

        The returned object needs to have a get_definition() method, which sources
        can pass to this method to re-create the decoder, and a codec attribute which
        could be used to identify this codec.'''
        raise NotImplementedError

