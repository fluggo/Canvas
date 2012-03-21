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

class SourcePlugin(Plugin):
    '''Base class for a plugin that handles certain source types.

    It's this plugin's responsibility to notify the user of any source errors
    to the user through a NotificationManager.'''

    def create_source(self, definition):
        '''Return a source from the given definition.

        The *definition* will be a definition returned by Source.get_definition().'''
        raise NotImplementedError

    def create_source_from_file(self, path):
        '''Return a new source if one can be created from the given file. If the plugin
        can't read the file (or doesn't support files), it can raise an error or return None.'''
        return False

    # TODO:
    #   Ways to represent/create ad-hocable sources
    #       Thumbnail

#    def get_source_templates(self):
#        '''Return a list of sources that can be used as templates.
#
#        This is usually used for ad-hoc sources that can create their own
#        content, such as a solid color source, and would represent base versions
#        that the user could drag into a composition and alter to their needs.'''
#        # TODO: How to know the list is updated? How to separate into categories?
#        #   * Perhaps this isn't a function of the plugin, but part of some kind
#        #     of installation procedure. The templates could be .yaml files in
#        #     some program directory.
#        return []

class Source(object):
    def get_plugin(self):
        '''Return a reference to the plugin that created this source.'''
        raise NotImplementedError

    def get_definition(self):
        '''Return an object that the source plugin can use to recreate this
        source in the future. The returned object is considered opaque (the
        caller promises not to alter it), but it should be representable in
        YAML.'''
        raise NotImplementedError

    def get_file_path(self):
        '''Return the path to the file this source represents.

        If the source is not file-based, return None. Otherwise, this needs to be
        a valid path that the source plugin can use to find the source again.
        This is important if the user has moved the source files to a new
        directory and needs to remap them.'''
        return None

    def get_stream_formats(self):
        '''Return a list of descriptions for streams in this source.'''
        # This is where stream formats will be described
        raise NotImplementedError

    def get_stream(self, name):
        '''Return the stream with the given name.

        If the stream is temporarily unavailable, such as when a file is missing,
        this method must still return a stream, but the stream can be empty
        (preferred for audio) or produce a warning image (preferred for video).
        Once the error is resolved, the existing stream should produce the
        correct data; this can be done with a pass-through source.

        This is a VideoStream or AudioStream (or some similar stream). Codec decoding
        takes place in the source.
        '''
        raise NotImplementedError

    def get_source_metadata(self):
        '''Return the set of user metadata defined in the source itself, if any.

        This is used to establish a baseline for metadata. It might be called only
        once and then never called again if the caller takes over handling metadata
        for this source.'''
        return None

    def get_stream_metadata(self, name):
        '''Return the base set of user metadata for the stream, if any.

        This is used to establish a baseline for metadata. It might be called only
        once and then never called again if the caller takes over handling metadata
        for this source.'''
        return None

    def get_thumbnail(self, size):
        '''Return an RgbaFrameF16 representative of this source that fits in the given
        *size* (a v2i).'''
        raise NotImplementedError

    # TODO: Property sheets, tool boxes, menu items, 3D
    # What about sources that can produce to any specification?
    #   * Probably have the specification set by the user before transform
    # What about overrides?
    #   a) Could be on the transform input side <-- NO, too many
    #   b) Could be created and maintained by the calling app
    #   c) Could be maintained by the source itself <-- I think so
    # What about *codec* overrides?
    #   a) Could be maintained by the source itself, where the source plugin
    #      can ask the plugin architecture for codec plugins <-- DING DING

