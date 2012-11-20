from fluggo.editor import plugins
from fluggo.media import process

class TestSourcePlugin(plugins.SourcePlugin): #, plugins.EffectPlugin):
    alerts = plugins.AlertPublisher()
    name = 'Test Source Plugin'
    description = 'Tests the plugin system'
    plugin_urn = 'urn:fluggo.com/canvas/plugins:test'

    alert = plugins.Alert('Test notification', icon=plugins.AlertIcon.Information, source=name)
    alerts.show_alert(alert)

    @classmethod
    def activate(self):
        pass

    @classmethod
    def deactivate(self):
        pass

    @classmethod
    def get_all_effects(self):
        return [TestEffect]

class TestEffect(plugins.Effect):
    default_name = 'Gain/Offset'
    urn = 'urn:fluggo.com/canvas/effects:gain-offset'
    stream_type = 'video'
    plugin = TestSourcePlugin

    def __init__(self, gain=1.0, offset=0.0, **kw):
        plugins.Effect.__init__(self, **kw)

        self._gain = plugins.ScalarEffectParameter('Gain', gain)
        #self._gain.value_changed.connect(lambda p: self.gain = p.value)
        self._offset = plugins.ScalarEffectParameter('Offset', offset)
        #self._offset.value_changed.connect(lambda p: self.offset = p.value)

    def get_definition(self):
        map_ = plugins.Effect.get_definition(self)

        if self._gain != 1.0:
            map_['gain'] = self._gain

        if self._offset != 0.0:
            map_['offset'] = self._offset

        return map_

    def get_parameters(self):
        return [self._gain, self._offset]

    def create_static_filter(self, stream):
        return _GainOffsetEffectFilter(stream, self)

class _GainOffsetEffectFilter(plugins.VideoStream):
    def __init__(self, stream, effect):
        self._filter = process.VideoGainOffsetFilter(stream)

        plugins.VideoStream.__init__(self, self._filter, stream.format, stream.defined_range)

        self._filter.gain = effect._gain.value
        self._filter.offset = effect._offset.value

        # TODO: Update format/defined_range when underlying changes
        # TODO: Update effect params when params change
        # TODO: Find way to do static effect

