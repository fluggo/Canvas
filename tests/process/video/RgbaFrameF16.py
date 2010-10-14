import unittest
from fluggo.media import process
from fluggo.media.basetypes import *

class test_RgbaFrameF16(unittest.TestCase):
    def test_solid(self):
        color = (1.0, 0.5, 0.333333, 0.2)
        solid = process.SolidColorVideoSource(color, box2i((0, 0), (2, 2)))
        frame = solid.get_frame_f16(0, box2i((0, 0), (3, 3)))

        self.assertEqual(frame.current_window, box2i(0, 0, 2, 2))
        self.assertEqual(frame.full_window, box2i(0, 0, 3, 3))

        for x, y in zip(frame.pixel(0, 0), color):
            self.assertAlmostEqual(x, y, 3)

        frame2 = frame.get_frame_f16(0, box2i(-1, -1, 1, 1))

        self.assertEqual(frame2.current_window, box2i(0, 0, 1, 1))
        self.assertEqual(frame2.full_window, box2i(-1, -1, 1, 1))

        for x, y in zip(frame.pixel(0, 0), color):
            self.assertAlmostEqual(x, y, 3)

