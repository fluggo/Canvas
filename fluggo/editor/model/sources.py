# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009-12 Brian J. Crowell <brian@fluggo.com>
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

import collections
import fractions
from fluggo.media import process
import yaml
from fluggo import signal, logging
from fluggo.media.formats import *
from fluggo.editor import plugins
from PyQt5 import QtGui, QtWidgets

_log = logging.getLogger(__name__)

class Asset:
    yaml_tag = '!Asset'

    '''is_source: True if the asset can be used as a source.'''
    is_source = False

    '''is_composition: True if the editor can create a composition editor for this asset.'''
    is_composition = False

    '''contains_assets: True if this asset can contain other assets.'''
    contains_assets = False

    def __init__(self, name, keywords=[]):
        self.name = name
        self._keywords = frozenset(keywords)
        self.keywords_updated = signal.Signal()
        self._asset_list = None

    def get_source(self):
        '''Return the plugins.Source that the asset represents, if any. May return
        None if the asset doesn't represent a source.'''
        return None

    def create_composition_editor(self):
        '''Return a new composition editor for this composition.'''
        raise NotImplementedError

    def get_definition(self):
        return {'keywords': list(self._keywords)}

    @property
    def asset_list(self):
        return self._asset_list

    @property
    def path(self):
        # TODO: Return full path of this asset
        return self.name

    @property
    def keywords(self):
        return self._keywords

    def fixup(self):
        # Prepares temporary data, such as checking up on
        # source and proxy files; sources should expect this can run more
        # than once
        pass

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data.get_definition())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(name='', **loader.construct_mapping(node))

class _SpaceSource(plugins.Source):
    def __init__(self, space, asset_list):
        plugins.Source.__init__(self, space.name)
        self._space = space
        self._asset_list = asset_list
        self._video = None
        self._audio = None
        self._load_alert = None

    def bring_online(self):
        if self._load_alert:
            self.hide_alert(self._load_alert)
            self._load_alert = None

        try:
            import fluggo.editor.graph

            self._video = fluggo.editor.graph.SpaceVideoManager(self._space, self._asset_list)
            self._video.name = 'Video'
            self.follow_alerts(self._video)

            self._audio = fluggo.editor.graph.SpaceAudioManager(self._space, self._asset_list)
            self._video.name = 'Audio'
            self.follow_alerts(self._audio)

            plugins.Source.bring_online(self)
        except Exception as ex:
            _log.debug('Error while creating source for space "{0}"', self.name, exc_info=True)
            self.take_offline()

            self._load_alert = plugins.Alert(
                'Unexpected ' + ex.__class__.__name__ + ' while creating source from space: ' + str(ex),
                icon=plugins.AlertIcon.Error,
                source=self.name,
                model_obj=self._space,
                actions=[],
                exc_info=True)

            self.show_alert(self._load_alert)

    def take_offline(self):
        if self._load_alert:
            self.hide_alert(self._load_alert)
            self._load_alert = None

        if self._video:
            self.unfollow_alerts(self._video)
            self._video = None

        if self._audio:
            self.unfollow_alerts(self._audio)
            self._audio = None

        plugins.Source.take_offline(self)

    def get_streams(self):
        if self.offline:
            raise plugins.SourceOfflineError

        return [self._video, self._audio]



class SpaceAsset(Asset):
    yaml_tag = '!SpaceAsset'
    is_source = True
    is_composition = True

    def __init__(self, space, **kw):
        Asset.__init__(self, **kw)
        self._space = space
        self._source = None

    @property
    def space(self):
        return self._space

    def get_definition(self):
        d = Asset.get_definition(self)
        d['space'] = self._space

        return d

    def get_source(self):
        if not self._asset_list:
            raise RuntimeError('Asset list not set on asset')

        if not self._source:
            self._source = _SpaceSource(self._space, self._asset_list)

        return self._source

    def fixup(self):
        Asset.fixup(self)
        self._space.fixup()

if False:
    class Source(plugins.Source):
        # Base source class:
        #   Keywords
        #   Authorship metadata
        #   Proxies

        # Notes: This extends the plugin Source class with more attributes that
        # the editor itself keeps track of, such as proxy and keyword info.
        # There may be *some* more convergence of these types in the future,
        # but the nice thing about this class living here is that we can safely
        # add features to it, and it can be our way of looking up true plugin
        # sources.

        def __init__(self, name, keywords=[]):
            plugins.Source.__init__(self, name)
            self._keywords = set(keywords)
            self.updated = signal.Signal()
            self.keywords_updated = signal.Signal()
            self._source_list = None

        def get_definition(self):
            return {'keywords': list(self.keywords)}

        @property
        def source_list(self):
            return self._source_list

        @property
        def keywords(self):
            return self._keywords

        def fixup(self):
            # Prepares temporary data, such as checking up on
            # source and proxy files; sources should expect this can run more
            # than once
            pass

        def visit(self, visitfunc):
            pass

        @classmethod
        def to_yaml(cls, dumper, data):
            return dumper.represent_mapping(cls.yaml_tag, data.get_definition())

        @classmethod
        def from_yaml(cls, loader, node):
            return cls(name='', **loader.construct_mapping(node))

class PluginSource(plugins.Source):
    def _handle_offline_changed(self, source):
        self.offline = self._source.offline

    def __init__(self, name, plugin_urn, definition, **kw):
        plugins.Source.__init__(self, name, **kw)
        self.definition = definition
        self.plugin_urn = plugin_urn
        self._plugin = None
        self._source = None
        self._load_alert = None

    def bring_online(self):
        if not self.offline:
            return

        if self._load_alert:
            self.hide_alert(self._load_alert)
            self._load_alert = None

        if not self._plugin:
            self._plugin = plugins.PluginManager.find_plugin_by_urn(self.plugin_urn)

            if self._plugin is None:
                _log.debug('Couldn\'t find plugin {0} for source {1}', self.plugin_urn, self.name)
                self._load_alert = plugins.Alert(
                    'Plugin ' + self.plugin_urn + ' unavailable or disabled',
                    icon=plugins.AlertIcon.Error,
                    source=self.name,
                    model_obj=self,
                    actions=[
                        QtWidgets.QAction('Retry', None, statusTip='Try bringing the source online again', triggered=self._retry_load)])

                self.show_alert(self._load_alert)

                # TODO: Maybe listen for plugins to come online
                # and automatically nab our plugin when it appears
                return

        if not self._source:
            # Try to create source from plugin
            try:
                self._source = self._plugin.create_source(self.name, self.definition)
                self._source.offline_changed.connect(self._handle_offline_changed)
                self.follow_alerts(self._source)
            except Exception as ex:
                if self._source:
                    self._source.offline_changed.disconnect(self._handle_offline_changed)

                self._source = None

                _log.debug('Error while creating source {0} from plugin', self.name, exc_info=True)
                self._load_alert = plugins.Alert(
                    'Unexpected ' + ex.__class__.__name__ + ' while creating source from plugin: ' + str(ex),
                    icon=plugins.AlertIcon.Error,
                    source=self.name,
                    model_obj=self,
                    actions=[
                        QtWidgets.QAction('Retry', None, statusTip='Try bringing the source online again', triggered=self._retry_load)],
                    exc_info=True)

                self.show_alert(self._load_alert)
                return

        if self._source.offline:
            try:
                self._source.bring_online()
            except Exception as ex:
                _log.debug('Error while bringing source {0} online', self.name, exc_info=True)
                self._load_alert = plugins.Alert(
                    'Unexpected ' + ex.__class__.__name__ + ' while bringing source online: ' + str(ex),
                    icon=plugins.AlertIcon.Error,
                    source=self.name,
                    model_obj=self,
                    actions=[
                        QtWidgets.QAction('Retry', None, statusTip='Try bringing the source online again', triggered=self._retry_load)],
                    exc_info=True)

                self.show_alert(self._load_alert)
                return

        if not self._source.offline:
            self.offline = False

    def _retry_load(self, checked):
        self.bring_online()

    def take_offline(self):
        if self.offline or not self._source:
            return

        try:
            self._source.take_offline()
        except:
            # TODO: Use generic error alert I'm going to create someday
            pass

        self.offline = True

    @property
    def file_path(self):
        if self.source:
            return self.source.file_path

        return None

    def get_definition(self):
        root = Source.get_definition(self)

        root['plugin_urn'] = self.plugin_urn

        if self._source:
            root['definition'] = self._source.get_definition()
        else:
            root['definition'] = self.definition

        return root

    @classmethod
    def from_plugin_source(cls, source):
        # BJC: We could save a *little* work and set the source and plugin
        # attributes of the new object, but I'm worried that could make this
        # harder to maintain
        return cls(source.name, source.plugin.plugin_urn, source.get_definition())

    def get_default_streams(self):
        if not self.offline and self._source:
            return self._source.get_default_streams()

        raise plugins.SourceOfflineError

    def get_stream(self, name):
        if not self.offline and self._source:
            return self._source.get_stream(name)

        raise plugins.SourceOfflineError

class PluginSourceAsset(Asset):
    yaml_tag = '!PluginSourceAsset'
    is_source = True
    is_composition = False

    def __init__(self, name, plugin_urn, definition, **kw):
        Asset.__init__(self, name=name, **kw)
        self._source = PluginSource(name, plugin_urn, definition)

    def get_definition(self):
        d = Asset.get_definition(self)
        d['plugin_urn'] = self._source.plugin_urn
        d['definition'] = self._source.get_definition()

        return d

    def get_source(self):
        return self._source

class RuntimeSource(plugins.Source):
    '''
    A runtime source is a source with a list of already-generated and ready-to-go
    streams. It can't be saved in a file-- its main purpose is to support testing.
    '''
    def __init__(self, name, streams):
        '''Create a runtime source. *streams* is a dictionary of streams.'''
        plugins.Source.__init__(self, name)
        self._streams = streams

    def get_stream_formats(self):
        return [(stream.name, stream.format) for stream in self._streams]

    def get_stream(self, name):
        if self.offline:
            raise plugins.SourceOfflineError

        return self._streams[name]

    def get_definition(self):
        raise RuntimeError("Runtime sources can't be written to a file.")

class RuntimeSourceAsset(Asset):
    is_source = True

    def __init__(self, source):
        Asset.__init__(self, source.name)
        self._source = source

    def get_source(self):
        return self._source

class AssetStreamRef:
    '''
    References a stream from a video or audio file.
    '''
    yaml_tag = '!AssetStreamRef'

    def __init__(self, asset_path=None, stream=None, **kw):
        self._asset_path = asset_path
        self._stream = stream

    @property
    def asset_path(self):
        return self._asset_path

    @property
    def stream(self):
        return self._stream

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {'asset_path': data._asset_path,
            'stream': data._stream}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

    def __eq__(self, other):
        return (isinstance(other, AssetStreamRef) and
            other._asset_path == self._asset_path and
            other._stream == self._stream)

class AssetList(collections.MutableMapping):
    def __init__(self, assets=None):
        self.assets = assets or {}
        self.added = signal.Signal()
        self.renamed = signal.Signal()
        self.removed = signal.Signal()

    def __getitem__(self, name):
        return self.assets[name]

    def __setitem__(self, name, value):
        old = self.assets.get(name)

        if old:
            self.removed(name)
            old._asset_list = None
            old.name = None

        self.assets[name] = value
        value._asset_list = self
        value.name = name

        self.added(name)

    def __delitem__(self, name):
        old = self.assets[name]

        self.removed(name)
        old._asset_list = None
        old.name = None

        del self.assets[name]

    def __len__(self):
        return len(self.assets)

    def __iter__(self):
        return self.assets.__iter__()

    def get_asset_list(self):
        return self.assets

    def fixup(self):
        # Give each object its name and asset_list
        for (name, asset) in self.assets.items():
            asset.name = name
            asset._asset_list = self

        for asset in self.assets.values():
            asset.fixup()

class Project(object):
    yaml_tag = '!Project'

    def __init__(self, known_formats=None, assets=None, project_settings=None):
        self._known_formats = known_formats if known_formats is not None else {}
        self._assets = assets if assets is not None else {}
        self._project_settings = project_settings if project_settings is not None else {}

    def fixup(self):
        if isinstance(self._assets, dict):
            self._assets = AssetList(assets=self._assets)

        self._assets.fixup()

    @property
    def assets(self):
        return self._assets

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {'known_formats': data._known_formats,
            'assets': data._assets.get_asset_list(),
            'project_settings': data._project_settings}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class FrameRateConversionType:
    DISCARD_FIELD = 'discard_field'
    BOB_DEINTERLACE = 'bob_deinterlace'
    BOB_INTERLACE = 'bob_interlace'
    ADD_PULLDOWN = 'add_pulldown'
    REMOVE_PULLDOWN = 'remove_pulldown'
    NONE = 'none'
    

def _yamlreg(cls):
    yaml.add_representer(cls, cls.to_yaml)
    yaml.add_constructor(cls.yaml_tag, cls.from_yaml)

_yamlreg(AssetStreamRef)
_yamlreg(SpaceAsset)
_yamlreg(PluginSourceAsset)
_yamlreg(Project)


