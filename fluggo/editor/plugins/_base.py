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

import os, os.path, weakref, sys, traceback
import ConfigParser
from PyQt4 import QtCore

from fluggo import signal, logging

_log = logging.getLogger(__name__)

# TODO: Need redo/undo support

class _AlertTracker(object):
    __slots__ = ('trackee', 'tracker', 'alerts', '__weakref__')

    def __init__(self, trackee, tracker):
        self.trackee = weakref.ref(trackee, self.stop_tracking)
        self.tracker = tracker
        self.alerts = None
        trackee.alert_added.connect(self.item_added)
        trackee.alert_removed.connect(self.item_removed)

        for alert in trackee._alerts.itervalues():
            self.item_added(alert)

    def stop_tracking(self):
        # For this to work via weak references, the alerts themselves
        # must not have strong references to the object in question
        trackee = self.trackee()

        if trackee is not None:
            trackee.alert_added.disconnect(self.item_added)
            trackee.alert_removed.disconnect(self.item_removed)

        if self.alerts is not None:
            for alert in self.alerts.itervalues():
                self.tracker.hide_alert(alert)

        self.alerts = None

    def item_added(self, alert):
        if self.alerts is None:
            self.alerts = {}

        self.alerts[alert.key] = alert
        self.tracker.show_alert(alert)

    def item_removed(self, alert):
        del self.alerts[alert.key]
        self.tracker.hide_alert(alert)

class AlertPublisher(object):
    '''Mixin class that reports errors and give the user ways to manage them.'''
    def __init__(self):
        self.alert_added = signal.Signal()
        self.alert_removed = signal.Signal()
        self._alerts = {}
        self._tracked_publishers = None

    def show_alert(self, alert):
        '''Add an alert to the list of alerts shown to the user.'''
        self.hide_alert(alert)

        self._alerts[alert.key] = alert
        self.alert_added(alert)

    def hide_alert(self, alert):
        if alert.key in self._alerts:
            del self._alerts[alert.key]
            self.alert_removed(alert)

    @property
    def alerts(self):
        return self._alerts.values()

    def follow_alerts(self, publisher):
        '''Re-publishes alerts published by *publisher*. The publisher is tracked with
        a weak reference; if the publisher disappears, its alerts will be unpublished.'''
        if self._tracked_publishers is None:
            self._tracked_publishers = weakref.WeakKeyDictionary()

        if publisher not in self._tracked_publishers:
            self._tracked_publishers[publisher] = _AlertTracker(publisher, self)

    def unfollow_alerts(self, publisher):
        '''Stops tracking alerts from the given *publisher*.'''
        if self._tracked_publishers is None:
            return

        tracker = self._tracked_publishers.pop(publisher, None)

        if tracker is not None:
            tracker.stop_tracking()

class AlertIcon(object):
    NoIcon, Information, Warning, Error = range(4)

class Alert(object):
    '''An alert for use with the AlertPublisher.'''
    def __init__(self, key, description, icon=AlertIcon.NoIcon, source=u'', model_obj=None, actions=[], exc_info=False):
        '''Create an alert. *key* is a way to uniquely identify this alert.
        *description* is the text to show. *icon* is either one of the values from AlertIcon,
        a QIcon, or a path to an image (Qt resource paths allowed). *source* gives the user a way to sort similar alerts together;
        give a name that would be useful for that. *actions* is a list of QActions to show the user for resolving
        the issue. *model_obj* is an object in that could be found in the model for this alert.'''
        # TODO: Add exc_info like on logging to allow capturing tracebacks on exceptions

        self.key = key
        self._description = description
        self._source = source
        self._icon = icon
        self._actions = actions
        self._model_obj = model_obj
        self._exc_info = None

        if exc_info:
            _log.debug('Alert with error: {0}', description, exc_info=True)
            self._exc_info = sys.exc_info()

    @property
    def description(self):
        '''Localized description of the error or warning.'''
        return self._description

    @property
    def source(self):
        return self._source

    @property
    def icon(self):
        '''A value from AlertIcon, or a custom QIcon.'''
        return self._icon

    @property
    def actions(self):
        '''Return a list of QActions the user can choose from to resolve the alert.'''

        # TODO: Add a general hide command?
        return self._actions

    @property
    def model_object(self):
        '''Optional object in the model that is associated with this alert. Having this
        object lets the user navigate from this alert to the object.'''
        return self._model_obj

    @property
    def exc_info(self):
        '''Optional exception info captured at the time of the alert.'''
        return self._exc_info

    def __str__(self):
        result = unicode(self.description)

        if self._source:
            result = self._source + u': ' + result

        if self._exc_info:
            result = result + u'\r\n' + u''.join(traceback.format_exception(*self._exc_info))

        return result

# TODO: Create standard alerts for things like file/plugin missing;
# the UI can possibly coalesce these and treat them as a group, or
# provide special assistance to the user

# Standard alerts:
#
#   Plain warning/error messages (only option is to dismiss)
#   Plugin missing
#   File missing
#   Failure to bring online?

class Plugin(AlertPublisher):
    def __init__(self):
        # TODO: keep track of alert keys so we can clean up after ourselves
        # TODO: Or, should alerts just be a property on a plugin or other
        #   object so that we don't have to keep up with alert managers? That
        #   seems like the easier option.
        AlertPublisher.__init__(self)

    @property
    def name(self):
        '''Return the name of the plugin.'''
        raise NotImplementedError

    @property
    def description(self):
        '''Return a short (one-line) description of the plugin.'''
        raise NotImplementedError

    @property
    def plugin_urn(self):
        '''Return a URN that uniquely identifies all versions of this plugin.'''
        raise NotImplementedError

    def activate(self):
        '''Called when the user has activated the plugin. This is usually
        the time to install any hooks into the interface.'''
        pass

    def deactivate(self):
        '''Called when the user has deactivated the plugin. This is usually
        the time to remove any hooks from the interface.'''
        pass

    # TODO: Dialogs for settings/about, global vs. project settings,
    # notifications from these stored objects that settings have changed

PLUGINS_PREFIX = u'plugins/'
DECODERS_PREFIX = u'decoders/'

class PluginManager(object):
    plugin_modules = None
    plugins = None
    enabled_plugins = None
    codecs = []
    enabled_codecs = {} # urn -> (priority, codec)
    codec_priorities = {}
    codecs_by_priority = []   # Enabled codecs in preference order
    alert_manager = AlertPublisher()

    @classmethod
    def load_all(cls):
        if cls.plugin_modules is not None:
            return

        # TODO: For now, this will just load all plugins here in the plugins directory
        # In the future, we need this to search some standard paths
        cls.plugin_modules = list(cls.find_all_modules([os.path.dirname(__file__)]))
        plugin_classes = []

        for module in cls.plugin_modules:
            module.load()

            if not module.module:
                continue

            # Scan through the module's dictionary for plugins we can use
            plugin_classes.extend(plugin for (name, plugin) in module.module.__dict__.iteritems()
                if not name.startswith('_') and issubclass(type(plugin), type) and issubclass(plugin, Plugin))

        plugins = {}

        for plugin_cls in plugin_classes:
            try:
                new_plugin = plugin_cls()
                existing_plugin = plugins.setdefault(new_plugin.plugin_urn, new_plugin)

                if new_plugin is not existing_plugin:
                    _log.error('Two plugins tried to claim the URN "{0}"', new_plugin.plugin_urn)
            except Exception as ex:
                _log.error('Could not create {0} plugin class: {1}', plugin_cls.__name__, ex, exc_info=True)

        cls.plugins = plugins
        cls.enabled_plugins = {}

        # Read config file for enabled plugins
        for (key, plugin) in cls.plugins.iteritems():
            settings = QtCore.QSettings()

            settings.beginGroup(PLUGINS_PREFIX + key)
            enabled = settings.value('enabled', False, type=bool)
            settings.endGroup()

            if enabled:
                try:
                    plugin.activate()
                    cls.alert_manager.follow_alerts(plugin)
                    cls.enabled_plugins[key] = plugin
                except:
                    _log.error('Failed to activate plugin "{0}"', plugin.name, exc_info=True)

        cls.reset_codecs()

    @classmethod
    def find_plugins(cls, baseclass=Plugin, enabled_only=True):
        cls.load_all()

        plugins = cls.enabled_plugins if enabled_only else cls.plugins
        return [plugin for plugin in plugins.itervalues() if isinstance(plugin, baseclass)]

    @classmethod
    def find_plugin_by_urn(cls, urn):
        return cls.enabled_plugins.get(urn, None)

    @classmethod
    def is_plugin_enabled(cls, plugin):
        return plugin.plugin_urn in cls.enabled_plugins

    @classmethod
    def set_plugin_enabled(cls, plugin, enable):
        if plugin.plugin_urn not in cls.plugins:
            raise ValueError('Given plugin is not in the list of available plugins.')

        enabled = cls.is_plugin_enabled(plugin)
        settings = QtCore.QSettings()

        settings.beginGroup(PLUGINS_PREFIX + plugin.plugin_urn)

        if enable and not enabled:
            try:
                plugin.activate()
                cls.alert_manager.follow_alerts(plugin)
                cls.enabled_plugins[plugin.plugin_urn] = plugin
                settings.setValue('enabled', True)
                cls.reset_codecs()
            except Exception as ex:
                _log.error('Failed to activate plugin "{0}"', plugin.name, exc_info=True)
        elif not enable and enabled:
            try:
                plugin.deactivate()
                cls.alert_manager.unfollow_alerts(plugin)
                del cls.enabled_plugins[plugin.plugin_urn]
                settings.setValue('enabled', False)
                cls.reset_codecs()
            except Exception as ex:
                _log.error('Failed to deactivate plugin "{0}"', plugin.name, exc_info=True)

        settings.endGroup()

    @classmethod
    def find_all_modules(cls, paths):
        for directory in paths:
            _log.info('Searching {0} for plugins...', directory)
            for filename in os.listdir(directory):
                if not filename.endswith('.plugin'):
                    continue

                _log.info('Found {0}...', filename)

                try:
                    for plugin in PluginModule.from_file(os.path.join(directory, filename)):
                        yield plugin
                except:
                    _log.warning('Could not read the plugin {0}', filename, exc_info=True)

    @classmethod
    def reset_codecs(cls):
        '''Clear out all codecs and start over.'''

        cls.codecs = []
        cls.enabled_codecs = {}

        for plugin in cls.find_plugins(CodecPlugin):
            try:
                cls.codecs.extend(plugin.get_all_codecs())
            except:
                _log.warning('Could not get a list of codecs from a plugin', exc_info=True)

        for codec in cls.codecs:
            settings = QtCore.QSettings()

            settings.beginGroup(DECODERS_PREFIX + codec.urn)
            enabled = settings.value('enabled', True, type=bool)
            priority = settings.value('priority', codec.default_priority, type=int)
            settings.endGroup()

            codec.priority = priority

            if enabled:
                cls.enabled_codecs[codec.urn] = codec

        cls.codecs_by_priority = list(cls.enabled_codecs.values())
        cls.codecs_by_priority.sort(key=lambda i: (i.priority, i.urn), reverse=True)

    @classmethod
    def find_codec_by_urn(cls, urn):
        '''Return the codec with the given URN, or None if it isn't enabled.'''
        return cls.enabled_codecs.get(urn)

    @classmethod
    def find_decoders(cls, format_urn=None, enabled_only=True):
        '''Return a list of codecs supporting the given *format_urn* in descending
        order of preference. If *format_urn* is not given, get all codecs.'''
        # TODO: Make can_decode a method, not a property

        if enabled_only:
            return [codec for codec in cls.codecs_by_priority if
                codec.can_decode and (format_urn is None or format_urn in codec.format_urns)]
        else:
            result = [codec for codec in cls.codecs if
                codec.can_decode and (format_urn is None or format_urn in codec.format_urns)]
            result.sort(key=lambda i: (i.priority, i.urn), reverse=True)
            return result

    @classmethod
    def is_decoder_enabled(cls, codec=None, codec_urn=None):
        return (codec_urn or codec.urn) in cls.enabled_codecs

    @classmethod
    def set_decoder_enabled(cls, codec, enable):
        if codec not in cls.codecs:
            raise ValueError('Given codec is not in the list of available codecs.')

        enabled = cls.is_decoder_enabled(codec=codec)
        settings = QtCore.QSettings()

        settings.beginGroup(DECODERS_PREFIX + codec.urn)

        if enable and not enabled:
            try:
                settings.setValue('enabled', True)
                cls.reset_codecs()
            except Exception as ex:
                _log.error('Failed to enable decoder "{0}"', codec.name, exc_info=True)
        elif not enable and enabled:
            try:
                settings.setValue('enabled', False)
                cls.reset_codecs()
            except Exception as ex:
                _log.error('Failed to disable decoder "{0}"', codec.name, exc_info=True)

        settings.endGroup()

    @classmethod
    def set_decoder_priority(cls, codec, priority):
        if codec not in cls.codecs:
            raise ValueError('Given codec is not in the list of available codecs.')

        settings = QtCore.QSettings()
        settings.beginGroup(DECODERS_PREFIX + codec.urn)

        try:
            settings.setValue('priority', priority)
            cls.reset_codecs()
        except Exception as ex:
            _log.error('Failed to set priority for decoder "{0}"', codec.name, exc_info=True)

        settings.endGroup()

class PluginModule(object):
    def __init__(self, name, module_name):
        self.name = name
        self.module_name = module_name
        self.module = None
        self.load_error = None

    @classmethod
    def from_file(cls, path):
        parser = ConfigParser.RawConfigParser()
        parser.read(path)

        for section in parser.sections():
            name = parser.get(section, 'name')
            module = parser.get(section, 'module')

            yield cls(name=name or section, module_name=module)

    def load(self):
        if self.module:
            return

        module_name = self.module_name
        from_module = None

        dot = self.module_name.rfind('.')

        if dot != -1:
            module_name = self.module_name[dot + 1:]
            from_module = self.module_name[:dot]

        try:
            if from_module:
                self.module = __import__(from_module, fromlist=[module_name]).__dict__[module_name]
            else:
                self.module = __import__(module_name)

            self.load_error = None
        except Exception as ex:
            _log.warning('Plugin "{0}" failed to load: {1}', self.name, ex, exc_info=True)
            self.load_error = ex

from ._source import *
from ._codec import *


