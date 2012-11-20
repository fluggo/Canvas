import unittest
from fluggo.media import process
from fluggo.editor import plugins
from fluggo.editor import graph
from fluggo.editor import model
from fluggo.editor.plugins import test
from fluggo.media.basetypes import *

red_format = plugins.VideoFormat(active_area=box2i(-1, -1, 20, 20))

red = plugins.VideoStream(process.SolidColorVideoSource(rgba(1.0, 0.0, 0.0, 1.0)),
    red_format)

class test_EffectVideoManager(unittest.TestCase):
    def check_color(self, x, y):
        self.assertAlmostEqual(x.r, y.r, 6, 'Red')
        self.assertAlmostEqual(x.g, y.g, 6, 'Green')
        self.assertAlmostEqual(x.b, y.b, 6, 'Blue')
        self.assertAlmostEqual(x.a, y.a, 6, 'Alpha')

    def test_1(self):
        stack = model.EffectStack([test.TestEffect(gain=2.0, offset=0.5)])
        mgr = graph.EffectVideoManager(red, stack)

        frame = mgr.get_frame_f32(0, box2i(0, 0, 3, 3))

        self.assertEqual(frame.current_window, box2i(0, 0, 3, 3))
        self.check_color(frame.pixel(0, 0), rgba(2.5, 0.5, 0.5, 1.0))

