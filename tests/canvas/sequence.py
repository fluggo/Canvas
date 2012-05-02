import unittest
from fluggo.media import process, formats
from fluggo.media.basetypes import *
from fluggo.editor.graph.video import SequenceVideoManager
from fluggo.editor import model, plugins

class FailedSource(plugins.Source):
    '''A source that refuses to come online.'''
    def __init__(self, name):
        plugins.Source.__init__(self, name)
        self._load_error = plugins.Alert(id(self), u'Can\'t load maaaan', source=name, icon=plugins.AlertIcon.Error)

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

slist = model.SourceList()
slist[u'red'] = model.RuntimeSource(u'red', {u'video': plugins.VideoStream(process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (100, 0, 0, 1), 100)))})
slist[u'green'] = model.RuntimeSource(u'green', {u'video': plugins.VideoStream(process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 100, 0, 1), 100)))})
slist[u'blue'] = model.RuntimeSource(u'blue', {u'video': plugins.VideoStream(process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 0, 100, 1), 100)))})
slist[u'noload'] = FailedSource(u'noload')
slist[u'noload_silent'] = SilentFailedSource(u'noload_silent')
slist[u'nostreams'] = model.RuntimeSource(u'nostreams', {})

vidformat = plugins.VideoFormat()

def getcolor(source, frame):
    return source.get_frame_f32(frame, box2i(0, 0, 0, 0)).pixel(0, 0)

class UpdateTracker(object):
    def __init__(self, track):
        track.frames_updated.connect(self.update_frames)
        self.reset()

    def update_frames(self, min_frame, max_frame):
        self.min_frame = min(min_frame, self.min_frame or min_frame)
        self.max_frame = max(max_frame, self.max_frame or max_frame)

    def reset(self):
        self.min_frame, self.max_frame = None, None

class test_SequenceVideoManager(unittest.TestCase):
    def check_no_alerts(self, alert_source):
        if len(alert_source.alerts):
            self.fail(u'Failed, found alert: ' + unicode(alert_source.alerts[0]))

    def check1(self, source):
        # A lot of tests produce this same sequence:
        # Ten frames of red, cut to green, then fade to blue
        colors = [getcolor(source, i) for i in range(0, 30)]

        # Red from one
        for i in range(0, 10):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(float(i + 1), colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Green from one
        for i in range(10, 15):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i - 10 + 1), colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Fade to blue
        for i in range(15, 20):
            msg = 'Wrong color ' + repr(colors[i]) + ' in frame ' + str(i) + ', expected ' + repr(rgba(0.0, float(i - 10 + 1) * (1.0 - float(i - 15) / 5.0), float(i - 15 + 1) * float(i - 15) / 5.0, 1.0))
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i - 10 + 1) * (1.0 - float(i - 15) / 5.0), colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - 15 + 1) * float(i - 15) / 5.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Blue from six
        for i in range(20, 25):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - 15 + 1), colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        for i in range(25, 30):
            msg = 'Frame {0} should be empty ({1} instead)'.format(i, colors[i])
            self.assertEqual(None, colors[i], msg)

    def test_1_start(self):
        '''Start in the correct position'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        self.check1(manager)

    def test_1_adjlen1(self):
        '''Adjust the length of an item'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=7),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=19),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=100, transition_length=5)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        sequence[0].update(length=10)
        self.assertEqual(track.min_frame, 7)
        self.assertEqual(track.max_frame, 10 + 19 + 100 - 5 - 1)
        track.reset()

        sequence[1].update(length=10)
        self.assertEqual(track.min_frame, 15)
        self.assertEqual(track.max_frame, 10 + 19 + 100 - 5 - 1)
        track.reset()

        sequence[2].update(length=10)
        self.assertEqual(track.min_frame, 25)
        self.assertEqual(track.max_frame, 10 + 10 + 100 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_adjlen2(self):
        '''Adjust the length of an item (different order)'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=17),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=5),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=22, transition_length=5)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        sequence[2].update(length=10)
        self.assertEqual(track.min_frame, 27)
        self.assertEqual(track.max_frame, 17 + 5 + 22 - 5 - 1)
        track.reset()

        sequence[0].update(length=10)
        self.assertEqual(track.min_frame, 10)
        self.assertEqual(track.max_frame, 17 + 5 + 10 - 5 - 1)
        track.reset()

        sequence[1].update(length=10)
        self.assertEqual(track.min_frame, 10)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_adjtranslength(self):
        '''Adjust the transition_length of an item'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10, transition_length=3),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=7)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        sequence[1].update(transition_length=0)
        self.assertEqual(track.min_frame, 7)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 7 - 1)
        track.reset()

        sequence[2].update(transition_length=5)
        self.assertEqual(track.min_frame, 13)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_add(self):
        '''Add items one at a time'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        sequence.append(model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5))
        self.assertEqual(track.min_frame, 5)
        self.assertEqual(track.max_frame, 14)
        track.reset()

        sequence.insert(0, model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10))
        self.assertEqual(track.min_frame, 0)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_addmultiple(self):
        '''Add multiple items at once'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        sequence.extend([
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5)])
        self.assertEqual(track.min_frame, 10)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)

        self.check1(manager)

    def test_1_remove(self):
        '''Remove items one at a time'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=25, length=14, transition_length=2),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=9, length=7),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        del sequence[1]
        self.assertEqual(track.min_frame, 8)
        self.assertEqual(track.max_frame, 10 + 14 + 10 + 7 + 10 - 5 - 2 - 1)
        track.reset()

        del sequence[2]
        self.assertEqual(track.min_frame, 15)
        self.assertEqual(track.max_frame, 10 + 10 + 7 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_removeends(self):
        '''Remove items at the ends'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=9, length=114),
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=23, length=8, transition_length=5),
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=9, length=7),
            ])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        track = UpdateTracker(manager)
        self.check_no_alerts(manager)

        sequence[0:2] = []
        self.assertEqual(track.min_frame, 0)
        self.assertEqual(track.max_frame, 114 + 8 + 10 + 10 + 10 + 7 - 5 - 5 - 1)
        track.reset()

        del sequence[3]
        self.assertEqual(track.min_frame, 10 + 10 + 10 - 5)
        self.assertEqual(track.max_frame, 10 + 10 + 10 + 7 - 5 - 1)
        track.reset()

        self.check1(manager)

    def check2(self, source):
        # Five frames of red, crossfade to green, then immediately crossfade to blue
        colors = [getcolor(source, i) for i in range(0, 25)]

        # Red from one
        for i in range(0, 5):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(float(i + 1), colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Fade to green
        for i in range(5, 10):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(float(i + 1) * (1.0 - float(i - 5) / 5.0), colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i - 5 + 1) * float(i - 5) / 5.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Fade to blue
        for i in range(10, 15):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i - 5 + 1) * (1.0 - float(i - 10) / 5.0), colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - 10 + 1) * float(i - 10) / 5.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Blue from 15
        for i in range(15, 20):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - 10 + 1), colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        for i in range(20, 25):
            msg = 'Frame {0} should be empty ({1} instead)'.format(i, colors[i])
            self.assertEqual(None, colors[i], msg)

    def test_2_start(self):
        '''Start in the correct position'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10, transition_length=5),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        self.check2(manager)

    def test_2_add_transitions(self):
        '''Add both transitions'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        sequence[1].update(transition_length=5)
        sequence[2].update(transition_length=5)
        self.check2(manager)

    def test_2_add_green(self):
        '''Add green'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=5)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        sequence.insert(1, model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10, transition_length=5))
        self.check2(manager)

    def test_2_adjust_transitions_1(self):
        '''Add both transitions'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10, transition_length=7),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=3)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        sequence[1].update(transition_length=5)
        sequence[2].update(transition_length=5)
        self.check2(manager)

    def test_2_adjust_transitions_2(self):
        '''Add both transitions'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10, transition_length=3),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10, transition_length=7)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        sequence[1].update(transition_length=5)
        sequence[2].update(transition_length=5)
        self.check2(manager)

    def check3(self, source):
        # Ten frames red, five blank, five green, crossfade blue
        colors = [getcolor(source, i) for i in range(0, 35)]

        red_start, red_offset = 0, 1
        green_start, green_offset = 15, 1
        blue_start, blue_offset = 20, 1

        # Red from one
        for i in range(0, 10):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(float(i - red_start + red_offset), colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        for i in range(10, 15):
            msg = 'Frame {0} should be empty ({1} instead)'.format(i, colors[i])
            self.assertEqual(None, colors[i], msg)

        # Green
        for i in range(15, 20):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i - green_start + green_offset), colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Fade to blue
        for i in range(20, 25):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(float(i - green_start + green_offset) * (1.0 - float(i - 20) / 5.0), colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - blue_start + blue_offset) * float(i - 20) / 5.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        # Blue from 25
        for i in range(25, 30):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - blue_start + blue_offset), colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        for i in range(30, 35):
            msg = 'Frame {0} should be empty ({1} instead)'.format(i, colors[i])
            self.assertEqual(None, colors[i], msg)

    def test_3_add_transitions(self):
        '''Add both transitions'''
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'green', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_no_alerts(manager)
        sequence[1].update(transition_length=-5)
        sequence[2].update(transition_length=5)
        self.check3(manager)

    # From this point, tests involving alerts
    def check_bad_sequence(self, stream):
        # Ten frames red, ten blank, ten blue
        colors = [getcolor(stream, i) for i in range(0, 35)]

        red_start, red_offset = 0, 1
        blue_start, blue_offset = 20, 1

        # Red from one
        for i in range(0, 10):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(float(i - red_start + red_offset), colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        for i in range(10, 20):
            msg = 'Frame {0} should be empty ({1} instead)'.format(i, colors[i])
            self.assertEqual(None, colors[i], msg)

        # Blue from 20
        for i in range(20, 30):
            msg = 'Wrong color in frame ' + str(i)
            self.assertAlmostEqual(0.0, colors[i].r, 6, msg)
            self.assertAlmostEqual(0.0, colors[i].g, 6, msg)
            self.assertAlmostEqual(float(i - blue_start + blue_offset), colors[i].b, 6, msg)
            self.assertAlmostEqual(1.0, colors[i].a, 6, msg)

        for i in range(30, 35):
            msg = 'Frame {0} should be empty ({1} instead)'.format(i, colors[i])
            self.assertEqual(None, colors[i], msg)

    def test_alert_missing_source(self):
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'badsource', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_bad_sequence(manager)

        self.assertEquals(1, len(manager.alerts))
        alert = manager.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u'Reference refers to source "badsource" which doesn\'t exist.', alert.description)

    def test_alert_offline_silent_source(self):
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'noload_silent', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_bad_sequence(manager)

        self.assertEquals(1, len(manager.alerts))
        alert = manager.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u'Unable to bring source "noload_silent" online.', alert.description)

    def test_alert_offline_source(self):
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'noload', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_bad_sequence(manager)

        self.assertEquals(1, len(manager.alerts))
        alert = manager.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u"Can't load maaaan", alert.description)

    def test_alert_missing_stream(self):
        self.maxDiff = None
        sequence = model.Sequence(type='video', items=[
            model.SequenceItem(source=model.StreamSourceRef(u'red', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'nostreams', u'video'), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef(u'blue', u'video'), offset=1, length=10)])

        manager = SequenceVideoManager(sequence, slist, vidformat)
        self.check_bad_sequence(manager)

        self.assertEquals(1, len(manager.alerts))
        alert = manager.alerts[0]

        self.assertEquals(plugins.AlertIcon.Error, alert.icon)
        self.assertEquals(u'Can\'t find stream "video" in source "nostreams".', unicode(alert))


