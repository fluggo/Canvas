import unittest
from fluggo.media import process, sources
from fluggo.media.basetypes import *
from fluggo.editor.graph.video import TimelineVideoManager
from fluggo.editor import canvas

class DeadMuxer(object):
    @classmethod
    def handles_container(cls, container):
        return True

    @classmethod
    def get_stream(cls, container, stream_index):
        return container

slist = sources.SourceList([DeadMuxer])
slist['red'] = process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (100, 0, 0, 1), 100))
slist['green'] = process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 100, 0, 1), 100))
slist['blue'] = process.SolidColorVideoSource(process.LerpFunc((0, 0, 0, 1), (0, 0, 100, 1), 100))

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

class test_TimelineVideoManager(unittest.TestCase):
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
            msg = 'Wrong color in frame ' + str(i)
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
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=10, transition_length=5)])

        manager = TimelineVideoManager(timeline, slist)
        self.check1(manager)

    def test_1_adjlen1(self):
        '''Adjust the length of an item'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=7),
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=19),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=100, transition_length=5)])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        timeline[0].update(length=10)
        self.assertEqual(track.min_frame, 7)
        self.assertEqual(track.max_frame, 10 + 19 + 100 - 5 - 1)
        track.reset()

        timeline[1].update(length=10)
        self.assertEqual(track.min_frame, 15)
        self.assertEqual(track.max_frame, 10 + 19 + 100 - 5 - 1)
        track.reset()

        timeline[2].update(length=10)
        self.assertEqual(track.min_frame, 25)
        self.assertEqual(track.max_frame, 10 + 10 + 100 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_adjlen2(self):
        '''Adjust the length of an item (different order)'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=17),
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=5),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=22, transition_length=5)])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        timeline[2].update(length=10)
        self.assertEqual(track.min_frame, 27)
        self.assertEqual(track.max_frame, 17 + 5 + 22 - 5 - 1)
        track.reset()

        timeline[0].update(length=10)
        self.assertEqual(track.min_frame, 10)
        self.assertEqual(track.max_frame, 17 + 5 + 10 - 5 - 1)
        track.reset()

        timeline[1].update(length=10)
        self.assertEqual(track.min_frame, 10)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_adjtranslength(self):
        '''Adjust the transition_length of an item'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=10, transition_length=3),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=10, transition_length=7)])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        timeline[1].update(transition_length=0)
        self.assertEqual(track.min_frame, 7)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 7 - 1)
        track.reset()

        timeline[2].update(transition_length=5)
        self.assertEqual(track.min_frame, 13)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_add(self):
        '''Add items one at a time'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=10)])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        timeline.append(canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=10, transition_length=5))
        self.assertEqual(track.min_frame, 5)
        self.assertEqual(track.max_frame, 14)
        track.reset()

        timeline.insert(0, canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=10))
        self.assertEqual(track.min_frame, 0)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_addmultiple(self):
        '''Add multiple items at once'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=10)])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        timeline.extend([
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=10, transition_length=5)])
        self.assertEqual(track.min_frame, 10)
        self.assertEqual(track.max_frame, 10 + 10 + 10 - 5 - 1)

        self.check1(manager)

    def test_1_remove(self):
        '''Remove items one at a time'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=25, length=14, transition_length=2),
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=9, length=7),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=10, transition_length=5)])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        del timeline[1]
        self.assertEqual(track.min_frame, 8)
        self.assertEqual(track.max_frame, 10 + 14 + 10 + 7 + 10 - 5 - 2 - 1)
        track.reset()

        del timeline[2]
        self.assertEqual(track.min_frame, 15)
        self.assertEqual(track.max_frame, 10 + 10 + 7 + 10 - 5 - 1)
        track.reset()

        self.check1(manager)

    def test_1_removeends(self):
        '''Remove items at the ends'''
        timeline = canvas.Timeline(type='video', items=[
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=9, length=114),
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=23, length=8, transition_length=5),
            canvas.TimelineItem(source=canvas.StreamSourceRef('red', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('green', 0), offset=1, length=10),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=1, length=10, transition_length=5),
            canvas.TimelineItem(source=canvas.StreamSourceRef('blue', 0), offset=9, length=7),
            ])

        manager = TimelineVideoManager(timeline, slist)
        track = UpdateTracker(manager)

        timeline[0:2] = []
        self.assertEqual(track.min_frame, 0)
        self.assertEqual(track.max_frame, 114 + 8 + 10 + 10 + 10 + 7 - 5 - 5 - 1)
        track.reset()

        del timeline[3]
        self.assertEqual(track.min_frame, 10 + 10 + 10 - 5)
        self.assertEqual(track.max_frame, 10 + 10 + 10 + 7 - 5 - 1)
        track.reset()

        self.check1(manager)

