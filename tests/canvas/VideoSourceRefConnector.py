import unittest
from fluggo.media import process, formats
from fluggo.media.basetypes import *
from fluggo.editor import model, plugins

class FailedSource(plugins.Source):
    '''A source that refuses to come online.'''
    def __init__(self, name):
        plugins.Source.__init__(self, name)
        self._load_error = plugins.Alert(id(self), u'Can\'t load maaaan', source=name, icon=plugins.AlertIcon.Error, model_obj=self)

    def bring_online(self):
        self.show_alert(self._load_error)

    def get_stream(self, name):
        raise plugins.SourceOfflineError

class SilentFailedSource(plugins.Source):
    '''A source that refuses to come online AND doesn't report an error. (for shame!)'''
    def __init__(self, name):
        plugins.Source.__init__(self, name)

    def bring_online(self):
        pass

    def get_stream(self, name):
        raise plugins.SourceOfflineError

red_format = plugins.VideoFormat(active_area=box2i(-1, -1, 20, 20))
green_format = plugins.VideoFormat(active_area=box2i(12, -6, 100, 210))
blue_format = plugins.VideoFormat(active_area=box2i(0, 0, 14, 19))

slist = model.SourceList()
slist[u'red'] = model.RuntimeSource(u'red', {u'video': 
    plugins.VideoStream(
        process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (100, 0, 0, 1), 100)),
        red_format,
        (4, 50))
    })
slist[u'green'] = model.RuntimeSource(u'green', {u'video':
    plugins.VideoStream(
        process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 100, 0, 1), 100)),
        green_format,
        (1, 12))
    })
slist[u'blue'] = model.RuntimeSource(u'blue', {u'video':
    plugins.VideoStream(
        process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 0, 100, 1), 100)),
        blue_format,
        (-10, 17))
    })
slist[u'noload'] = FailedSource(u'noload')
slist[u'noload_silent'] = SilentFailedSource(u'noload_silent')
slist[u'nostreams'] = model.RuntimeSource(u'nostreams', {})

def getcolor(source, frame):
    return source.get_frame_f32(frame, box2i(0, 0, 0, 0)).pixel(0, 0)

class test_VideoSourceRefConnector(unittest.TestCase):
    def check_no_alerts(self, alert_source):
        if len(alert_source.alerts):
            self.fail(u'Failed, found alert: ' + unicode(alert_source.alerts[0]))

    def check_red(self, source):
        colors = [getcolor(source, i) for i in range(0, 5)]

        for i in range(0, 5):
            msg = u'Wrong color ' + repr(colors[i]) + u' in frame ' + str(i)
            self.assertAlmostEqual(float(i), colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

    def check_green(self, source):
        colors = [getcolor(source, i) for i in range(0, 5)]

        for i in range(0, 5):
            msg = u'Wrong color ' + repr(colors[i]) + u' in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i), colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

    def test_connect(self):
        ref = model.StreamSourceRef(u'red', u'video')
        conn = model.VideoSourceRefConnector(slist, ref, model_obj='blimog')

        self.check_no_alerts(conn)
        self.check_red(conn)
        self.assertEqual(conn.format, red_format)
        self.assertEqual(conn.defined_range, (4, 50))

        ref = model.StreamSourceRef(u'green', u'video')
        conn.set_ref(ref)

        self.check_no_alerts(conn)
        self.check_green(conn)
        self.assertEqual(conn.format, green_format)
        self.assertEqual(conn.defined_range, (1, 12))

    # From this point, tests involving alerts
    def check_empty(self, stream):
        colors = [getcolor(stream, i) for i in range(0, 5)]

        for i in range(0, 5):
            msg = u'Frame {0} should be empty ({1} instead)'.format(i, repr(colors[i]))
            self.assertEqual(None, colors[i], msg)

    def test_alert_missing_source(self):
        ref = model.StreamSourceRef(u'red', u'video')
        conn = model.VideoSourceRefConnector(slist, ref, model_obj='blimog')

        self.check_no_alerts(conn)

        ref = model.StreamSourceRef(u'badsource', u'video')
        conn.set_ref(ref)

        self.assertEquals(1, len(conn.alerts))
        alert = conn.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u'Reference refers to source "badsource" which doesn\'t exist.', unicode(alert))
        self.check_empty(conn)
        self.assertEquals((None, None), conn.defined_range)
        self.assertEquals(None, conn.format)

    def test_alert_offline_silent_source(self):
        ref = model.StreamSourceRef(u'red', u'video')
        conn = model.VideoSourceRefConnector(slist, ref, model_obj='blimog')

        self.check_no_alerts(conn)

        ref = model.StreamSourceRef(u'noload_silent', u'video')
        conn.set_ref(ref)

        self.assertEquals(1, len(conn.alerts))
        alert = conn.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u'Unable to bring source "noload_silent" online.', unicode(alert))
        self.assertEquals('blimog', alert.model_object)
        self.check_empty(conn)
        self.assertEquals((None, None), conn.defined_range)
        self.assertEquals(None, conn.format)

    def test_alert_offline_source(self):
        ref = model.StreamSourceRef(u'red', u'video')
        conn = model.VideoSourceRefConnector(slist, ref, model_obj='blimog')

        self.check_no_alerts(conn)

        ref = model.StreamSourceRef(u'noload', u'video')
        conn.set_ref(ref)

        self.assertEquals(1, len(conn.alerts))
        alert = conn.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u"noload: Can't load maaaan", unicode(alert))
        self.assertEquals(slist[u'noload'], alert.model_object)
        self.check_empty(conn)
        self.assertEquals((None, None), conn.defined_range)
        self.assertEquals(None, conn.format)

    def test_alert_missing_stream(self):
        ref = model.StreamSourceRef(u'red', u'video')
        conn = model.VideoSourceRefConnector(slist, ref, model_obj='blimog')

        self.check_no_alerts(conn)

        ref = model.StreamSourceRef(u'nostreams', u'video')
        conn.set_ref(ref)

        self.assertEquals(1, len(conn.alerts))
        alert = conn.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u'Can\'t find stream "video" in source "nostreams".', unicode(alert))
        self.assertEquals('blimog', alert.model_object)
        self.check_empty(conn)
        self.assertEquals((None, None), conn.defined_range)
        self.assertEquals(None, conn.format)


