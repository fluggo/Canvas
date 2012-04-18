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
from PyQt4 import QtGui

_log = logging.getLogger(__name__)

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

    def get_default_stream_formats(self):
        # For now, first video stream and first audio stream
        streams = self.get_stream_formats()

        video_streams = [stream for stream in streams if stream[1].format_type == u'video']
        audio_streams = [stream for stream in streams if stream[1].format_type == u'audio']

        return video_streams[0:1] + audio_streams[0:1]

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data.get_definition())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(name=u'', **loader.construct_mapping(node))

class PluginSource(Source):
    yaml_tag = '!PluginSource'

    def _handle_offline_changed(self, source):
        self.offline = self._source.offline

    def __init__(self, name, plugin_urn, definition, **kw):
        Source.__init__(self, name, **kw)
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
            self._plugin = plugins.PluginManager.find_by_urn(self.plugin_urn)

            if self._plugin is None:
                _log.debug('Couldn\'t find plugin {0} for source {1}', self.plugin_urn, self.name)
                self._load_alert = plugins.Alert(id(self),
                    u'Plugin ' + self.plugin_urn + u' unavailable or disabled',
                    icon=plugins.AlertIcon.Error,
                    source=self.name,
                    model_obj=self,
                    actions=[
                        QtGui.QAction(u'Retry', None, statusTip=u'Try bringing the source online again', triggered=self._retry_load)])

                self.show_alert(self._load_alert)

                # TODO: Maybe listen for plugins to come online
                # and automatically nab our plugin when it appears
                return

        if not self._source:
            # TODO: Try to create source from plugin
            try:
                self._source = self._plugin.create_source(self.name, self.definition)
                self._source.offline_changed.connect(self._handle_offline_changed)
                self.follow_alerts(self._source)
            except Exception as ex:
                if self._source:
                    self._source.offline_changed.disconnect(self._handle_offline_changed)

                self._source = None

                _log.debug('Error while creating source {0} from plugin', self.name, exc_info=True)
                self._load_alert = plugins.Alert(id(self),
                    u'Unexpected ' + ex.__class__.__name__ + u' while creating source from plugin: ' + unicode(ex),
                    icon=plugins.AlertIcon.Error,
                    source=self.name,
                    model_obj=self,
                    actions=[
                        QtGui.QAction(u'Retry', None, statusTip=u'Try bringing the source online again', triggered=self._retry_load)],
                    exc_info=True)

                self.show_alert(self._load_alert)
                return

        if self._source.offline:
            try:
                self._source.bring_online()
            except Exception as ex:
                _log.debug('Error while bringing source {0} online', self.name, exc_info=True)
                self._load_alert = plugins.Alert(id(self),
                    u'Unexpected ' + ex.__class__.__name__ + u' while bringing source online: ' + unicode(ex),
                    icon=plugins.AlertIcon.Error,
                    source=self.name,
                    model_obj=self,
                    actions=[
                        QtGui.QAction(u'Retry', None, statusTip=u'Try bringing the source online again', triggered=self._retry_load)],
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

        root['plugin_urn'] = plugin_urn

        if self._source:
            root['definition'] = self._source.get_definition()
        else:
            root['definition'] = self.definition

        return root

    def get_stream_formats(self):
        if not self.offline and self._source:
            return self._source.get_stream_formats()

        # Come back when we're online
        return []

    def get_stream(self, name):
        if not self.offline and self._source:
            return self._source.get_stream(name)

        raise plugins.SourceOfflineError

class RuntimeSource(Source):
    '''
    A runtime source is a source with a list of already-generated and ready-to-go
    streams. It can't be saved in a file-- its main purpose is to support testing.
    '''
    def __init__(self, name, streams, keywords=[]):
        '''Create a runtime source. *streams* is a dictionary of streams.'''
        Source.__init__(self, name, keywords)
        self._streams = streams

    def get_stream_formats(self):
        return [(stream.name, stream.format) for stream in self._streams]

    def get_stream(self, name):
        if self.offline:
            raise plugins.SourceOfflineError

        return self._streams[name]

    def get_definition(self):
        raise RuntimeError("Runtime sources can't be written to a file.")

class StreamSourceRef(object):
    '''
    References a stream from a video or audio file.
    '''
    yaml_tag = u'!StreamSourceRef'
    __slots__ = ('_source_name', '_stream')

    def __init__(self, source_name=None, stream=None, **kw):
        self._source_name = source_name
        self._stream = stream

    @property
    def source_name(self):
        return self._source_name

    @property
    def stream(self):
        return self._stream

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {u'source_name': data._source_name,
            u'stream': data._stream}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

    def __eq__(self, other):
        return (isinstance(other, StreamSourceRef) and
            other._source_name == self._source_name and
            other._stream == self._stream)

class SourceList(collections.MutableMapping):
    def __init__(self, muxers, sources=None):
        self.muxers = muxers
        self.sources = sources or {}
        self.added = signal.Signal()
        self.renamed = signal.Signal()
        self.removed = signal.Signal()

    def __getitem__(self, name):
        return self.sources[name]

    def __setitem__(self, name, value):
        old = self.sources.get(name)

        if old:
            self.removed(name)
            old._source_list = None
            old.name = None

        self.sources[name] = value
        value._source_list = self
        value.name = name

        self.added(name)

    def __delitem__(self, name):
        old = self.sources[name]

        self.removed(name)
        old._source_list = None
        old.name = None

        del self.sources[name]

    def __len__(self):
        return len(self.sources)

    def __iter__(self):
        return self.sources.__iter__()

    def detect_container(self, path):
        for muxer in self.muxers:
            try:
                container = muxer.detect_container(path)

                if container:
                    return (muxer, container)
            except Exception as ex:
                _log.warning('Muxer {0} returned error "{1}" for path {2}', muxer, ex, path)
                pass

        return (None, None)

    def get_source_list(self):
        return self.sources

    def fixup(self):
        # Give each object its name and source_list
        for (name, source) in self.sources.iteritems():
            source.name = name
            source._source_list = self

        for source in self.sources.itervalues():
            source.fixup()

class Project(object):
    yaml_tag = '!Project'

    def __init__(self, known_formats=None, sources=None, project_settings=None):
        self._known_formats = known_formats if known_formats is not None else {}
        self._sources = sources if sources is not None else {}
        self._project_settings = project_settings if project_settings is not None else {}

    def fixup(self, muxers):
        if isinstance(self._sources, dict):
            self._sources = SourceList(muxers, sources=self._sources)

        self._sources.fixup()

    @property
    def sources(self):
        return self._sources

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {'known_formats': data._known_formats,
            'sources': data._sources.get_source_list(),
            'project_settings': data._project_settings}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class MappingProperty:
    FRAME_CONVERSION = 'frame_conversion'
    FRAME_RATE_CONVERSION = 'frame_rate_conversion'
    # COLOR_CONVERSION
    # PIXEL_ASPECT_RATIO_CONVERSION

class FrameConversionType:
    FILL = 'fill'
    BOX = 'box'
    NONE = 'none'

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

_yamlreg(StreamSourceRef)
_yamlreg(PluginSource)
_yamlreg(Project)


