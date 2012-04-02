from fluggo.editor import plugins

class TestSourcePlugin(plugins.SourcePlugin):
    name = u'Test Source Plugin'
    description = u'Tests the plugin system'
    plugin_urn = u'urn:fluggo.com/canvas/plugins:test'

    def __init__(self, *args, **kw):
        plugins.SourcePlugin.__init__(self, *args, **kw)
        self.alert = plugins.Alert((self, u'test'), u'Test notification', icon=plugins.AlertIcon.Information, source=self.name)
        self.show_alert(self.alert)

    def activate(self):
        pass

    def deactivate(self):
        pass

