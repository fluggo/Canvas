import unittest
from fluggo.media import process
from fluggo.media.basetypes import *

class test_SolidColorVideoSource(unittest.TestCase):
    def check_color(self, color1, color2):
        for x, y in zip(color1, color2):
            self.assertAlmostEqual(x, y, 6)

    def test_const_color(self):
        color = (1.0, 0.5, 0.333333, 0.2)
        solid = process.SolidColorVideoSource(color)
        frame = solid.get_frame_f32(0, box2i(0, 0, 3, 3))

        self.assertEqual(frame.current_window, box2i(0, 0, 3, 3))
        self.check_color(frame.pixel(0, 0), color)

    def test_const_window(self):
        color = (1.0, 0.5, 0.333333, 0.2)
        solid = process.SolidColorVideoSource(color, box2i(0, 0, 2, 2))
        frame = solid.get_frame_f32(0, box2i(0, 0, 3, 3))

        self.assertEqual(frame.current_window, box2i(0, 0, 2, 2))
        self.check_color(frame.pixel(0, 0), color)

        frame2 = frame.get_frame_f32(0, box2i(-1, -1, 1, 1))

        self.assertEqual(frame2.current_window, box2i(0, 0, 1, 1))
        self.check_color(frame.pixel(0, 0), color)

    def test_moving_color(self):
        solid = process.SolidColorVideoSource(process.LerpFunc((0.5, 0.25, 2.0, 1.0), (-0.5, -0.25, -2.0, 0.0), 3))

        frame = solid.get_frame_f32(0, box2i(0, 0, 0, 0))
        self.check_color(frame.pixel(0, 0), rgba(0.5, 0.25, 2.0, 1.0))

        frame = solid.get_frame_f32(1, box2i(0, 0, 0, 0))
        self.check_color(frame.pixel(0, 0), rgba(0.0, 0.0, 0.0, 0.5))

        frame = solid.get_frame_f32(2, box2i(0, 0, 0, 0))
        self.check_color(frame.pixel(0, 0), rgba(-0.5, -0.25, -2.0, 0.0))

    def test_moving_window(self):
        solid = process.SolidColorVideoSource(rgba(0.0, 0.0, 1.0, 1.0), process.LerpFunc((-2, -2, 2, 2), (-4, -4, 0, 6), 3))

        frame = solid.get_frame_f32(0, box2i(-5, -5, 5, 6))
        self.assertEquals(frame.current_window, box2i(-2, -2, 2, 2))

        frame = solid.get_frame_f32(1, box2i(-5, -5, 5, 6))
        self.assertEquals(frame.current_window, box2i(-3, -3, 1, 4))

        frame = solid.get_frame_f32(2, box2i(-5, -5, 5, 6))
        self.assertEquals(frame.current_window, box2i(-4, -4, 0, 6))

