from fluggo.editor import plugins

class TestSourcePlugin(plugins.SourcePlugin):
    name = 'Test Source Plugin'
    description = 'Tests the plugin system'
    plugin_urn = 'urn:fluggo.com/canvas/plugins:test'

    def __init__(self, *args, **kw):
        plugins.SourcePlugin.__init__(self, *args, **kw)
        self.alert = plugins.Alert('test', 'Test notification', icon=plugins.AlertIcon.Information, source=self.name)

    def activate(self):
        self.add_alert(self.alert)

    def deactivate(self):
        self.remove_alert(self.alert)

