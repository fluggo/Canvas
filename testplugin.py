
# Idea: Use file change date / hash /size to know when things need to be rebuilt
# You could even think of the whole thing as a build process


# More plugin types: File output, preview output, transitions, effects,
# timecodes, compositions, codecs, transitions, asset...

# One thing I wonder... I spent a good deal of time on the
# canvas spaces separating model from UI from rendering, so that
# a program could load just the model and the rendering and
# go straight at it. The plugins kind of mess with that.
#
# Are the plugins strictly an editor thing? No... the very first
# thing I'm implementing are source plugins. Any rendering/playback
# program will need those to read the source files. But how much
# UI do I put in the source (and other) plugins? Does each plugin
# perhaps need a separate editor UI provider?
#
# Take an Effect. I was thinking you can easily programmatically
# specify a set of EffectParameters, which can be animated and
# set independently of any UI. They could also raise alerts, but
# absent of a user, how will they tell a program what's wrong
# and what could fix it? I could raise exceptions, but that
# means the editor will have to catch them and know how to present
# alternatives to the user. Finally, I had the thought that
# some effects might have options that couldn't be tuned generically;
# the effect UI might display an "Options..." link which goes to
# a specialized dialog box. How does this tie in with programs that
# have no UI?
#
# Clearly some of these things need to have a UI, but it needs to
# be separate from the plugin *just enough* that a program without
# a UI need not deal with them, perhaps even so far as to say that
# the calling program need not have PyQt4 installed. When the editor
# wants to ask for a QAction, QIcon, or QWidget to present some
# specialized UI element, only then may the plugin attempt to load
# PyQt4.
#
# I see this causing the most trouble for alerts. Alerts are
# necessarily UI things; so far, the idea has been to allow an
# AlertPublisher to create and present alerts, and the calling
# program can optionally listen for those alerts and display them.
# But these alerts require certain UI services, namely:
#
#  * UI translation
#  * QActions for menu items
#  * Possible custom QIcons
#
# Can a plugin passively provide these things? Or do I need to
# load QtGui, even if I don't have a GUI? I can create Python-only
# analogs of QAction, but not QIcon (resource paths are possible--
# I can have the module register its resources with pyrcc4 or
# QtCore.registerResource()). Translations are in QtCore.
# So only QAction would need to be replicated.
#
# I'm thinking maybe QtGui should just be requisite. I don't think
# anybody packages QtCore without QtGui (besides with the commercial
# licensees). I don't want to duplicate QActions.

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
    def __init__(self):
        self.updated = signal.Signal()

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

    def get_effect_parameters(self):
        '''Return a list of EffectParameters that the user interface can use to animate this source.'''
        return []

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

    def has_scaler(self):
        return False

    def create_scaler(self, source, input_interlaced, input_rect, output_interlaced, output_rect):
        # What if there are aspects of the scaler that we should be able to configure?
        # Really, maybe we should just build the effects plugins, and let
        # the return value here be an effect instance, and configurable via
        # the same methods.
        raise NotImplementedError

    def has_frame_rate_conversion(self):
        return False

    def create_frame_rate_converter(self, source, interlaced, in_frames, out_frames):
        '''Create a filter that converts every *in_frames* frames to *out_frames*
        frames. *interlaced* is whether the frames are interlaced.'''
        raise NotImplementedError

class ScalingTransform(object):
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
    #
    # Better yet, transform could be just a normal built-in component, and the
    # plugins to deal with the steps. Transform could include steps for:
    #
    # (Video)
    # * Pulldown
    # * Interlacing/scaling/pixel aspect - these belong together
    #   * Letterboxing/fill <- Canvas supplies these options
    # * Frame rate conversion
    # * Color conversion (not system, because source should output XYZ; white point mainly)
    # * Active area?
    #
    # (Audio)
    # * Sample rate conversion
    # * Channels?
    # * Normalization
    #
    # Even better: When a user selects a clip in the space, let there be three regions in
    # the effects box, stacked one below the other:
    #
    #   Source: Information about the source stream, which applies to all
    #       references to that source. After the source block itself, allow
    #       the user to attach effects that are performed before the transform
    #       block.
    #   Transform: Effects that transform the source into this space, if any
    #       are necessary. The effects are created automatically, and can be
    #       re-created if the user wishes, but at all other times, the user can
    #       add, remove, or alter any effect they wish in the stack. Transform
    #       effects are applied wherever that particular source stream appears
    #       in the space, but after source-level and before clip-level plugins.
    #   Effects: Effects that apply to only this clip. These are applied last.
    #
    # I'm thinking there are no standard motion controls; these must be supplied by
    # an effect.

    def get_plugin(self):
        '''Return a reference to the plugin that created this transform.'''
        raise NotImplementedError


class VideoFormat(object):
    # interlaced = bool
    # active_rect = box2f
    # pixel_aspect_ratio = fractions.Fraction
    # white_point = (float, float, float) or wellknownwhitepoint
    # frame_rate = fractions.Fraction

    pass

class AudioFormat(object):
    # sample_rate = fractions.Fraction
    # channel_assignment = [one of ('FrontRight', 'FrontLeft', 'Center', 'Solo',
    #       'CenterLeft', 'CenterRight', 'BackLeft', 'BackRight', 'LFE')]
    # loudness = something resembling an RMS of the signal, useful toward
    #       getting a sane default loudness before adjusting +/- dB

    pass

class RenderPlugin(Plugin):
    '''Provides renderers, which send a source to some final format, usually a file.'''
    pass

class EffectPlugin(Plugin):
    def create_effect(self, name, definition=None):
        '''Return a supported VideoEffect or AudioEffect.'''
        pass

class Effect(object):
    # An effect. An effect can have whatever UI it likes, but it
    # can also expose EffectParameters which are animatable.

    # Raises the updated signal (one parameter, the effect)
    # when any of the parameters changes. (The single source doesn't count.)

    def __init__(self):
        self.updated = signal.Signal()

    def get_plugin(self):
        raise NotImplementedError

    def get_name(self):
        raise NotImplementedError

    def get_localized_name(self):
        raise NotImplementedError

    def get_definition(self):
        '''Return an object that the effect plugin can use to recreate this
        effect in the future. The returned object is considered opaque (the
        caller promises not to alter it), but it should be representable in
        YAML.'''
        raise NotImplementedError

    def get_effect_parameters(self):
        '''Return a list of EffectParameters that the user interface can use to animate this effect.'''
        return []

    def set_source(self, source):
        '''Sets the raw source for this effect.'''
        raise NotImplementedError

class VideoEffect(Effect, process.VideoPassThroughFilter):
    pass

class AudioEffect(Effect, process.AudioPassThroughFilter):
    pass

class EffectParameter(object):
    # Animatable parameter
    # type: int, float, v2i, v2f, box2i, box2f, rgba

    # Parameters are not pluggable. At the moment, I'm thinking
    # they're just a wrapper around process.AnimationFunc.

    def get_name(self):
        '''Return a programmatic name for the parameter.'''
        raise NotImplementedError

    def get_localized_name(self):
        '''Return a localized name for the parameter to display in the UI.'''
        return self.get_name()

    def get_constant(self):
        '''Return a constant value if one is set, otherwise None.'''
        raise NotImplementedError

    def set_constant(self, value):
        '''Set this effect to a constant value. Raise ValueError
        if it's the wrong type.'''
        raise NotImplementedError

    def get_type(self):
        # Get the type of the parameter, either int, float, v2i, v2f, box2i, box2f, rgba
        raise NotImplementedError

    # TODO: Need support for hold, linear-interp, and Bezier-curve points;
    # for cubic Bezier curves, store the start point and the two control points
    # at the beginning animation point, and let the next animation point be the end point




class Asset:
    # Will take some of the functionality ofPluginSource; SourceList will be AssetList

    @property
    def can_expand(self):
        # True if the asset can contain other assets
        return False

    @property
    def children(self):
        # dict-like mapping of names to child assets
        raise NotImplementedError

    # Keywords, authorship, etc. is here instead of PluginSource

    def can_accept_drop(self, drop):
        # Returns true if the asset can accept the given dropped object
        # Can be anything, but can be list of assets
        return False

    def drop(self, drop):
        # Tries to drop the given objects here; only
        # called if can_accept_drop returned True; return True if successful
        raise NotImplementedError

    def get_definition(self):
        # As always, dictionary definition to recover the asset later

class FolderAsset(Asset):
    can_expand = True

    def __init__(self, definition={}):
        self._children = {}

class PluginSourceAsset(Asset):
    # This is what contains the plugin_urn and definition
    pass

class EffectAsset(Asset):
    # Later down the line
    pass


# Things we need:

# Drag-and-drop into space fixed
# Fix moving clips around
# Save whole project (not just source list)
# Undo and redo

# Auto-save
# Narrative
# Multiple spaces
# Clip -> subclip slicing
# Effects
# * Source, transform, and clip effects
# Output codec selection
# Assets and folder assets

# Multiple spaces on one canvas

