from fluggo.editor import plugins

class TestSourcePlugin(plugins.SourcePlugin):
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

