# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009 Brian J. Crowell <brian@fluggo.com>
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

_log = logging.getLogger(__name__)

class Source(object):
    # Base source class:
    #   Keywords
    #   Authorship metadata
    #   Proxies
    #   Format conformance

    def __init__(self, keywords=[]):
        self._keywords = set(keywords)
        self.updated = signal.Signal()
        self.keywords_updated = signal.Signal()
        self._source_list = None

    def _create_repr_dict(self):
        return {'keywords': list(self.keywords)}

    @property
    def source_list(self):
        return self._source_list

    @property
    def keywords(self):
        return self._keywords

    @property
    def stream_formats(self):
        return []

    def fixup(self):
        # Prepares temporary data, such as checking up on
        # source and proxy files; sources should expect this can run more
        # than once, such as if the muxers list changes
        pass

    def visit(self, visitfunc):
        pass

    def get_default_stream_formats(self):
        # For now, first video stream and first audio stream
        video_streams = [stream for stream in self.stream_formats if stream.type == 'video']
        audio_streams = [stream for stream in self.stream_formats if stream.type == 'audio']

        return video_streams[0:1] + audio_streams[0:1]

    def get_stream(self, stream_index):
        raise NotImplementedError

    @classmethod
    def to_yaml(cls, dumper, data):
        return dumper.represent_mapping(cls.yaml_tag, data._create_repr_dict())

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class FileSource(Source):
    yaml_tag = '!FileSource'

    def __init__(self, container, **kw):
        Source.__init__(self, **kw)
        self.main_container = container
        self.detected_container = None
        self.detected_muxer = None

    def _create_repr_dict(self):
        result = Source._create_repr_dict(self)
        result['container'] = self.main_container

        return result

    @property
    def stream_formats(self):
        return self.main_container.streams

    def fixup(self):
        Source.fixup(self)
        (self.detected_muxer, self.detected_container) = self.source_list.detect_container(self.main_container.path)

        if not self.detected_container:
            _log.warning("Couldn't open the file {0}", self.main_container.path)

    def get_stream(self, stream_index):
        if self.detected_muxer:
            return self.detected_muxer.get_stream(self.main_container, stream_index)

        # Temporary: in the future, render an appropriate "missing video" stream
        return VideoStream(process.EmptyVideoSource(), self.main_container.streams[stream_index])

class RuntimeSource(Source):
    '''
    A runtime source is a source with a list of already-generated and ready-to-go
    streams. It can't be saved in a file-- its main purpose is to support testing.
    '''
    def __init__(self, streams, keywords=[]):
        Source.__init__(self, keywords)
        self._streams = streams

        # Set the stream indexes
        for i, stream in enumerate(self._streams):
            stream.format.override[ContainerProperty.STREAM_INDEX] = i

    @property
    def stream_formats(self):
        return [stream.format for stream in self._streams]

    def get_stream(self, stream_index):
        return self._streams[stream_index]

    def _create_repr_dict(self):
        raise RuntimeError("Runtime sources can't be written to a file.")

class StreamSourceRef(object):
    '''
    References a stream from a video or audio file.
    '''
    yaml_tag = u'!StreamSourceRef'

    def __init__(self, source_name=None, stream_index=None, **kw):
        self._source_name = source_name
        self._stream_index = stream_index

    @property
    def source_name(self):
        return self._source_name

    @property
    def stream_index(self):
        return self._stream_index

    @classmethod
    def to_yaml(cls, dumper, data):
        result = {'source_name': data._source_name,
            'stream_index': data._stream_index}

        return dumper.represent_mapping(cls.yaml_tag, result)

    @classmethod
    def from_yaml(cls, loader, node):
        return cls(**loader.construct_mapping(node))

class GeneratedSource(Source):
    '''
    A generated source is a special kind of source that can stand in for a source
    reference (in which case, it's an ad-hoc generated source). Generated sources
    have only one stream and can adapt it to a StreamFormat.
    '''
    def adapt(self, format):
        raise NotImplementedError

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
            old._name = None

        self.sources[name] = value
        value._source_list = self
        value._name = name

        self.added(name)

    def __delitem__(self, name):
        old = self.sources[name]

        self.removed(name)
        old._source_list = None
        old._name = None

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
            source._name = name
            source._source_list = self

        for source in self.sources.itervalues():
            source.fixup()

class VideoStream(process.VideoPassThroughFilter):
    def __init__(self, base_stream, format=None):
        self.format = format or (base_stream.format if hasattr(base_stream, 'format') else StreamFormat(type='video'))
        self.length = self.format.adjusted_length
        self.frames_updated = signal.Signal()

        process.VideoPassThroughFilter.__init__(self, base_stream)

class AudioStream(process.AudioPassThroughFilter):
    def __init__(self, base_stream, format=None):
        self.format = format or (base_stream.format if hasattr(base_stream, 'format') else StreamFormat(type='audio'))
        self.length = self.format.adjusted_length
        self.samples_updated = signal.Signal()

        process.AudioPassThroughFilter.__init__(self, base_stream)

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
_yamlreg(FileSource)
_yamlreg(Project)


