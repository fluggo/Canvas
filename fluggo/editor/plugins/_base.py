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

import os, os.path
import ConfigParser
from PyQt4 import QtCore

from fluggo import signal, logging

_log = logging.getLogger(__name__)

class _AlertManager(object):
    '''Not a plugin. Lets plugins report errors and give the user ways to manage them.'''
    def __init__(self):
        self.added = signal.Signal()
        self.removed = signal.Signal()
        self._alerts = {}

    def add(self, alert):
        '''Add an alert to the list of alerts shown to the user.'''
        self.remove(alert)

        self._alerts[alert.key] = alert
        self.added(alert)

    def remove(self, alert):
        if alert.key in self._alerts:
            del self._alerts[alert.key]
            self.removed(alert)

    def __iter__(self):
        return self._alerts.itervalues()

class AlertIcon(object):
    NoIcon, Information, Warning, Error = range(4)

class Alert(object):
    '''An alert for use with the AlertManager.'''
    def __init__(self, key, description, icon=AlertIcon.NoIcon, source='', actions=[]):
        '''Create an alert. *key* is a way to uniquely identify this alert.
        *description* is the text to show. *icon* is either one of the values from AlertIcon
        or a custom QIcon. *source* gives the user a way to sort similar alerts together;
        give a name that would be useful for that. *actions* is a list of QActions to show the user for resolving
        the issue.'''

        self.key = key
        self._description = description
        self._source = source
        self._icon = icon
        self._actions = actions

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

        # TODO: Add a general hide command
        return self._actions

class Plugin(object):
    def __init__(self, alert_manager):
        self._alert_manager = alert_manager

    def add_alert(self, alert):
        self._alert_manager.add(alert)

    def remove_alert(self, alert):
        self._alert_manager.remove(alert)

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

class PluginManager(object):
    plugin_modules = None
    plugins = None
    enabled_plugins = None
    alert_manager = _AlertManager()

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
                new_plugin = plugin_cls(cls.alert_manager)
                existing_plugin = plugins.setdefault(new_plugin.plugin_urn, new_plugin)

                if new_plugin is not existing_plugin:
                    _log.warning('Two plugins tried to claim the URN "{0}"', new_plugin.plugin_urn)
            except Exception as ex:
                _log.warning('Could not create {0} plugin class: {1}', plugin_cls.__name__, ex)

        cls.plugins = plugins
        cls.enabled_plugins = {}

        # Read config file for enabled plugins
        for (key, plugin) in cls.plugins.iteritems():
            settings = QtCore.QSettings()

            settings.beginGroup('plugins/' + key)
            enabled = settings.value('enabled', False).toBool()
            settings.endGroup()

            if enabled:
                try:
                    plugin.activate()
                    cls.enabled_plugins[key] = plugin
                except Exception as ex:
                    _log.warning('Failed to activate plugin "{0}"', plugin.name)

    @classmethod
    def find(cls, baseclass=Plugin, enabled_only=True):
        cls.load_all()

        plugins = cls.enabled_plugins if enabled_only else cls.plugins
        return [plugin for plugin in plugins.itervalues() if isinstance(plugin, baseclass)]

    @classmethod
    def is_enabled(cls, plugin):
        return plugin.plugin_urn in cls.enabled_plugins

    @classmethod
    def set_enabled(cls, plugin, enable):
        if plugin.plugin_urn not in cls.plugins:
            raise ValueError('Given plugin is not in the list of available plugins.')

        enabled = cls.is_enabled(plugin)
        settings = QtCore.QSettings()

        settings.beginGroup('plugins/' + plugin.plugin_urn)

        if enable and not enabled:
            try:
                plugin.activate()
                cls.enabled_plugins[plugin.plugin_urn] = plugin
                settings.setValue('enabled', True)
            except Exception as ex:
                _log.warning('Failed to activate plugin "{0}"', plugin.name)
        elif not enable and enabled:
            try:
                plugin.deactivate()
                del cls.enabled_plugins[plugin.plugin_urn]
                settings.setValue('enabled', False)
            except Exception as ex:
                _log.warning('Failed to deactivate plugin "{0}"', plugin.name)

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
                except Exception as ex:
                    _log.warning('Could not read the plugin {0}: {1}', filename, ex)

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
            _log.warning('Plugin "{0}" failed to load: {1}', self.name, ex)
            self.load_error = ex

from .codec import *
from .source import *

