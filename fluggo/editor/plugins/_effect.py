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

from fluggo.media.basetypes import *
from fluggo.media import process
from fluggo import signal
from ._base import *
from ._source import *

class classproperty:
    def __init__(self, fget):
        self.fget = fget
        self.__doc__ = fget.__doc__

    def __get__(self, obj, klass=None):
        if klass is None:
            if obj is None:
                return self

            klass = type(obj)

        return self.fget(klass)

class EffectPlugin(Plugin):
    '''Provides video and audio effects.'''

    @classmethod
    def get_all_effects(self):
        '''Return a list of all effects supported by this plugin.'''
        return []

class _MetaEffect(type):
    @property
    def localized_name(self):
        return self.get_localized_name() or self.name

class Effect(AlertPublisher, metaclass=_MetaEffect):
    '''A reference to the plugin that provided this effect.'''
    plugin = None

    '''The name of this effect.'''
    default_name = None

    '''A URN that uniquely identifies this effect.'''
    urn = None

    '''The stream type of the effect (video/audio/subtitle/etc.).'''
    stream_type = None

    def __init__(self, name=None, bypass=False):
        self._name = name
        self._bypass = bypass

        self.name_changed = signal.Signal()
        self.bypass_changed = signal.Signal()

        # Used by the model.EffectStack class
        self._index = 0

    @property
    def index(self):
        '''The index of the effect in the EffectStack.'''
        return self._index

    @property
    def name(self):
        return self._name or self.default_name

    @name.setter
    def name(self, value):
        '''Set the name of the effect. Set to None to get the default back.'''
        if self._name != value:
            self._name = value
            self.name_changed(self)

    @property
    def bypass(self):
        return self._bypass

    @bypass.setter
    def bypass(self, value):
        value = bool(value)

        if self._bypass != value:
            self._bypass = value
            self.bypass_changed(self)

    @classmethod
    def get_localized_name(self):
        '''Return the localized name of this effect, or None if the effect doesn't
        support the current locale.'''
        return None

    def get_definition(self):
        '''Return a dictionary of parameters that can be passed to the effect's
        constructor to re-create the object.'''
        map_ = {}

        if self._name:
            map_['name'] = self._name

        if self._bypass:
            map_['bypass'] = True

        return map_

    def get_parameters(self):
        '''Return a list of effect parameters, in the order they should be shown.'''
        return []

    def create_filter(self, stream):
        '''Return a VideoStream or AudioStream which implements the effect for
        the given stream. The returned filter should track updates to the effect
        object.'''
        return self.create_static_filter(stream)

    def create_static_filter(self, stream):
        '''Return the same kind of filter create_filter does, but representing the
        current state of the effect and not tracking any future changes to it.'''
        raise NotImplementedError

class PlaceholderEffect(Effect):
    default_name = 'Unknown effect'

    @property
    def name(self):
        return self._real_effect.name if self._real_effect else Effect.name.fget(self)

    @name.setter
    def name(self, value):
        if self._real_effect:
            self._real_effect.name = value

    @property
    def bypass(self):
        if self._real_effect:
            return self._real_effect.bypass

        return True

    @bypass.setter
    def name(self, value):
        if self._real_effect:
            self._real_effect.name = value

    @property
    def urn(self):
        return self._urn

    @property
    def stream_type(self):
        return self._stream_type

    def __init__(self, name, bypass, urn, stream_type, **kw):
        Effect.__init__(self, name=name, bypass=bypass)
        self.definition = kw
        self._urn = urn
        self._stream_type = stream_type
        self._real_effect = None
        self._error = None

        self.resolve()

    def resolve(self):
        if self._real_effect:
            return

        try:
            if self._error:
                self.hide_alert(self._error)
                self._error = None

            # We're out to find a specific effect
            effect_cls = PluginManager.get_effect_by_urn(self._urn)

            if not effect_cls:
                self._clear()
                self._error = Alert('Could not find effect "' + self._urn + '". Check to see that it is installed.',
                    model_obj=self.model_obj, icon=AlertIcon.Error)
                self.show_alert(self._error)
                return

            if effect_cls.stream_type != self._stream_type:
                self._clear()
                self._error = Alert(
                    'Effect "{0.localized_name}" ("{0.urn}") is of type "{0.stream_type}," but the context expects an effect of type "{1._stream_type}."'.format(
                        effect_cls, self),
                    model_obj=self.model_obj, icon=AlertIcon.Error)
                self.show_alert(self._error)
                return

            try:
                self._real_effect = effect_cls(name=self._name, bypass=self._bypass, **self.definition)
            except:
                self._clear()
                self._error = Alert('Error while creating effect object',
                    model_obj=self.model_obj, icon=AlertIcon.Error, exc_info=True)
                self.show_alert(self._error)
                return

            # We're the real thing now, so let people know
            if not self._bypass:
                self.bypass_changed(self)

            self.name_changed(self)
        except:
            _log.warning('Error while creating effect for URN "' + self._urn + '"', exc_info=True)
            self._clear()
            self._error = Alert('Error while creating effect for URN "' + self._effect_urn + '"',
                model_obj=self.model_obj, icon=AlertIcon.Error, exc_info=True)
            self.show_alert(self._error)

    def get_definition(self):
        if self._real_effect:
            return self._real_effect.get_definition()

        kw = Effect.get_definition(self)
        kw.update(self.definition)
        return kw

    def create_static_filter(self, stream):
        '''Return the same kind of filter create_filter does, but representing the
        current state of the effect and not tracking any future changes to it.'''
        if self._real_effect:
            return self._real_effect.create_static_filter(stream)

        # We don't have an effect, and we're not going to, so
        # make a dumb pass-through
        if stream_type == 'video':
            return VideoStream(stream, stream.format, stream.defined_range)
        elif stream_type == 'audio':
            return AudioStream(stream, stream.format, stream.defined_range)

        raise NotImplementedError

    def create_filter(self, stream):
        if self._real_effect:
            return self._real_effect.create_filter(stream)

        # We don't have an effect, but should we get one, this stream needs to
        # produce it. Give back a placeholder.
        if stream_type == 'video':
            return _VideoEffectPlaceholderFilter(stream, self)
        elif stream_type == 'audio':
            return _AudioEffectPlaceholderFilter(stream, self)

        raise NotImplementedError

class _EffectPlaceholderFilter:
    '''Stands in for the effect's real filter when we haven't found it yet.'''
    def __init__(self, stream, placeholder_effect):
        self._basestream = stream
        self._placeholder_effect = placeholder_effect
        self._realstream = None

        self._connect()
        self._placeholder_effect.bypass_changed.connect(self._connect)

    def _connect(self, filter_=None):
        if self._realstream:
            return

        if self._placeholder_effect._real_effect:
            self._realstream = self._placeholder_effect.real_effect.create_filter(
                self.basestream)
            self.set_format(self._realstream.format)
            self.set_base_filter(self._realstream, new_range=self._realstream.defined_range)

class _VideoEffectPlaceholderFilter(VideoStream, _EffectPlaceholderFilter):
    def __init__(self, stream, placeholder_effect):
        VideoStream.__init__(self, stream, stream.format, stream.defined_range)
        _EffectPlaceholderFilter.__init__(self, stream, placeholder_effect)

class _AudioEffectPlaceholderFilter(AudioStream, _EffectPlaceholderFilter):
    def __init__(self, stream, placeholder_effect):
        AudioStream.__init__(self, stream, stream.format, stream.defined_range)
        _EffectPlaceholderFilter.__init__(self, stream, placeholder_effect)


# Axes:
#   Type: bool, int, float, v2i, v2f, box2i, box2f, rgba
#   Control type: text box, spinner, slider, etc.
#   Animatable
class Continuity:
    NONE = None
    CONTINUOUS = 'continuous'
    C0 = 'continuous'
    C1 = 'C1'

class AnimationPoint(process.AnimationPoint):
    '''Base wrapper for the C AnimationPoint class.'''

    def __init__(self, type, frame, value):
        process.AnimationPoint(self, type=type, frame=frame, value=value)
        self._value = value

    # Override the base value property to return the same type we got
    @property
    def value(self):
        return self._value

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data.get_definition())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(name='', **loader.construct_mapping(node))

    def get_definition(self):
        return {'frame': self.frame, 'value': self.value}

class HoldAnimationPoint(AnimationPoint):
    yaml_tag = '!HoldPt'

    def __init__(self, frame, value):
        AnimationPoint.__init__(self, process.POINT_HOLD, frame, value)

class LinearAnimationPoint(AnimationPoint):
    yaml_tag = '!LinearPt'

    def __init__(self, frame, value):
        AnimationPoint.__init__(self, process.POINT_LINEAR, frame, value)

class AnimationCurve(process.FrameFuncPassThroughFilter):
    #   Type: bool, int, float, v2i, v2f, box2i, box2f, rgba
    yaml_tag = '!AnimCurve'

    '''Wrapper for AnimationFunc which provides continuity, update events, and
    serializability.'''
    def __init__(self, base_type=None, points=None):
        self._func = process.AnimationFunc()
        process.FrameFuncPassThroughFilter(self, self._func)

        if base_type not in (bool, int, float, v2i, v2f, box2i, box2f, rgba):
            raise RuntimeError(str(base_type) + ' is not a supported animation type.')

        self._base_type = base_type
        self._points = points or []

    def get_definition(self):
        raise NotImplementedError

    @property
    def base_type(self):
        return self._base_type

    def fixup(self):
        pass

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data.get_definition())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(name='', **loader.construct_mapping(node))


# EffectParameter and its subclasses will represent the parameters themselves.
# They can be of just about any type. When displaying the UI, the effect will be
# asked for a set of widgets tied to those parameters. If no widgets are available,
# I expect we'll generate some based on the parameters.

# TODO: Separate this out into boolean, integer, float/vector/box, color, and enum
class EffectParameter:
    def __init__(self, name, value, localized_name=None, animatable=False):
        '''
        Create an effect parameter.

        name: Programmatic name of the parameter.
        localized_name: Name to present to the user.
        value: Default value. The type of this value determines the type of the
            parameter.
        animatable: True if the parameter can be animated. If so, the value property
            can hold either a constant or an AnimationCurve.
        
        '''
        if isinstance(value, AnimationCurve):
            if not animatable:
                raise ValueError('AnimationCurve was specified for an effect parameter that could not be animated.')

            self._base_type = value.base_type
        else:
            self._base_type = value.__class__

        # TODO: This probably isn't needed, I don't think we instantiate this type directly
        if self._base_type not in (bool, int, float, v2i, v2f, box2i, box2f, rgba):
            raise RuntimeError(str(self._base_type) + ' is not a supported animation type.')

        self._name = name
        self._localized_name = localized_name
        self._animatable = animatable
        self._value = value

        '''value_changed(parameter): Raised when the direct value changes. The
        value property can hold a constant or an AnimationCurve, so this signal
        can be used to detect those changes and supply the new value to the
        underlying filter.

        Signal subscribers could also use the signal to validate the parameter and
        reset it to something proper.'''
        self.value_changed = signal.Signal()

        '''curve_changed(parameter, min_frame, max_frame), where min_frame and
        max_frame can be None if the range extends indefinitely. Raised when the
        value or curve changes, and gives which frames are affected by the
        change.'''
        self.curve_changed = signal.Signal()

        # TODO: Subscribe to AnimationCurve's change events!

    @property
    def base_type(self):
        return self._base_type

    @property
    def name(self):
        return self._name

    def _validate_value(self, value):
        '''Return a valid value, or raise an error if the value is completely invalid.'''
        if value is None:
            raise ValueError('Value is None.')

        if isinstance(value, self._base_type):
            # Valid instance of our base type
            return value

        raise ValueError('Value is not of type ' + str(self._base_type) + '.')

    @property
    def animatable(self):
        return self._animatable

    @property
    def value(self):
        return self._value

    @property
    def localized_name(self):
        return self._localized_name or self._name

    @value.setter
    def value(self, value):
        if isinstance(value, AnimationCurve):
            if not self._animatable:
                raise ValueError('AnimationCurve was specified for an effect parameter that could not be animated.')

            if not issubclass(value.base_type, self._base_type):
                raise ValueError('AnimationCurve of the wrong base type was specified for an effect parameter.')

            # TODO: Validate the points in the curve!
            # TODO: Subscribe to AnimationCurve's change events!
            self._value = value
        else:
            # TODO: Unsubscribe from previous AnimationCurve's change events!
            self._value = self._validate_value(value)

        self.value_changed(self)
        self.curve_changed(self, None, None)

    def clone(self):
        value = self._value

        if hasattr(value, 'clone'):
            value = value.clone()

        return self.__class__(self.name, self.value,
            localized_name=self._localized_name,
            animatable=self._animatable)

class BoolEffectParameter(EffectParameter):
    def __init__(self, name, value, animatable=False, **kw):
        if animatable:
            raise RuntimeError('Boolean effect parameters can\'t be animated.')

        value = bool(value)

        EffectParameter.__init__(self, name, value, animatable=animatable, **kw)

class ScalarEffectParameter(EffectParameter):
    def __init__(self, name, value, hard_min=None, hard_max=None, **kw):
        '''
        Create an effect parameter.

        name: Programmatic name of the parameter.
        localized_name: Name to present to the user.
        value: Default value. The type of this value determines the type of the
            parameter.
        animatable: True if the parameter can be animated. If so, the value property
            can hold either a constant or an AnimationCurve.
        hard_min: The absolute minimum value of the parameter.
            Values outside this range are clamped to this range.
            If not given, the soft and hard limits are set to default values.
        hard_max: The absolute maximum value of the parameter.
        
        '''
        EffectParameter.__init__(self, name, value, **kw)

        if self._base_type not in (int, float):
            raise RuntimeError(str(self._base_type) + ' is not a supported scalar type.')

        if self._base_type is int:
            self._hard_min = int(hard_min or min(-50000, value))
            self._hard_max = int(hard_max or min(50000, value))
        elif self._base_type is float:
            self._hard_min = float(hard_min or min(-50000.0, value))
            self._hard_max = float(hard_max or min(50000.0, value))

    @property
    def hard_min(self):
        return self._hard_min

    @property
    def hard_max(self):
        return self._hard_max

    def _validate_value(self, value):
        value = EffectParameter._validate_value(self, value)

        # Clamp to the specified range
        value = min(max(value, self._hard_min), self._hard_max)
        return value

    def clone(self):
        result = EffectParameter.clone(self)
        result._hard_min = self._hard_min
        result._hard_max = self._hard_max
        return result

class ColorEffectParameter(EffectParameter):
    def __init__(self, name, value, localized_name=None):
        EffectParameter.__init__(self, name, value, localized_name=localized_name,
            animatable=False)

        if self._base_type is not rgba:
            raise RuntimeError(str(self._base_type) + ' is not a supported color type.')

def _yamlreg(cls):
    yaml.add_representer(cls, cls.to_yaml)
    yaml.add_constructor(cls.yaml_tag, cls.from_yaml)

_yamlreg(HoldAnimationPoint)
_yamlreg(LinearAnimationPoint)

