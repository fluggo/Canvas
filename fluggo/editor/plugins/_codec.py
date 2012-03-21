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

class CodecPlugin(Plugin):
    '''Provides encoders and decoders for one or more stream formats.

    Encoders and decoders transform from CodecPacketSource to AudioSource or VideoSource and back.
    They do not expose any intermediate steps, such as CodedImageSources, except
    where options for reconstructing images are exposed in the codec's settings.'''

    def getDecoderUrn(self, formatUrn):
        '''Return a codec URN if the plugin can decode a stream with the given stream format.'''
        return None

    def createDecoder(self, stream, codecUrn, options={}):
        '''Create a decoder (class Codec).'''
        raise NotImplementedError

    def getEncoderUrn(self, formatUrn):
        '''Return a codec URN if the plugin can encode a stream with the given stream format.'''
        return None

    def createEncoder(self, stream, codecUrn, options={}):
        '''Create an encoder (class Codec).'''
        raise NotImplementedError

    # TODO: Get the list of supported codecs

class Codec(object):
    def getPlugin(self):
        '''Return a reference to the plugin that created this codec.'''
        raise NotImplementedError

    def getFormatUrn(self):
        '''Return the URN of the format this codec encodes or decodes.'''
        raise NotImplementedError

    def getUrn(self):
        '''Return the URN of this specific codec. This should be unique among codec plugins.'''
        raise NotImplementedError

    def getMediaType(self):
        # Video/audio/subtitle/etc.
        raise NotImplementedError

    def getOptions(self):
        '''Return an object with the current settings for the codec, or
        None if there are no configurable settings.'''
        return None

    def setOptions(self, options):
        raise NotImplementedError

    def isDecoder(self):
        '''Return true if this is a decoder, false if it's an encoder.'''
        raise NotImplementedError

    # TODO: Property dialogs

class Encoder(Codec):
    def getCodedStream(self, input_stream):
        raise NotImplementedError

class Decoder(Codec):
    def getDecodedStream(self, input_stream):
        raise NotImplementedError

