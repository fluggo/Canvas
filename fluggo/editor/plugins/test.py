from fluggo.editor import plugins

class TestSourcePlugin(plugins.SourcePlugin):
    def name(self):
        return 'Test Source Plugin'

    def description(self):
        return 'Tests the plugin system'

    def plugin_urn(self):
        return 'urn:fluggo.com/canvas/plugins:test'

    def activate(self):
        print 'activated!'

    def deactivate(self):
        print 'deactivated!'

