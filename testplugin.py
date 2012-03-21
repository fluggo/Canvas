
class NotificationManager(object):
    '''Not a plugin. Lets plugins report errors and give the user ways to manage them.'''
    def add_notification(self, notification):
        '''Add a notification to the list of notifications shown to the user.'''
        raise NotImplementedError

    def remove_notification(self, notification):
        raise NotImplementedError

class Notification(object):
    '''Base class of NotificationManager notifications.'''

    def get_plugin(self):
        '''Return a reference to the plugin that made this notification.'''
        raise NotImplementedError

    def get_object(self):
        '''Return a reference to the object affected by this notification, such as a source.

        If the notification is general, return None.'''
        return None

    def get_description(self):
        '''Return a short, localized string description of the error or warning.'''
        raise NotImplementedError

    def get_actions(self):
        '''Return a list of QActions the user can choose from to resolve the notification.'''
        return []

# More plugin types: File output, preview output, transitions, effects,
# timecodes, compositions, codecs...

class Plugin(object):
    def get_plugin_urn(self):
        '''Return a URN that uniquely identifies all versions of this plugin.'''
        raise NotImplementedError

    # TODO: Dialogs for settings/about, global vs. project settings,
    # notifications from these stored objects that settings have changed

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

class TransformPlugin(Plugin):
    '''Base class for plugins that can transform a source format to a target format.'''

    pass

class Transform(object):
    '''Base class for objects that manage the transformation of a specific source to a specific target.'''

    # Struggling to think of this class. The easiest concept is a fully
    # automatic transform: no matter what the input or output is, it constructs
    # a chain to handle it. Such a transform might have options for how to
    # handle aspect ratio differences (e.g. letterbox or full), but the user
    # isn't actually in control of what happens, and formats can be changed at will.
    #
    # Less clear is a transform that lets the user decide each step. Suppose you
    # start with an interlaced source and your target is progressive. You might
    # set a bob deinterlace step. Then what happens if you change the source later
    # to a progressive source? You could discard the deinterlace step, I suppose.
    # The transform could just be a list of ways to perform the transform should it
    # be necessary; if deinterlacing, bob, if interlacing, weave, etc.

    def get_plugin(self):
        '''Return a reference to the plugin that created this transform.'''
        raise NotImplementedError

    def set_source_format(self, format):
        raise NotImplementedError

    def set_target_format(self, format):
        raise NotImplementedError

    def build_stream(self, input_stream):
        '''Return a stream with all of the transformations applied.'''
        raise NotImplementedError


class RenderPlugin(Plugin):
    '''Provides renderers, which send a source to some final format, usually a file.'''
    pass

