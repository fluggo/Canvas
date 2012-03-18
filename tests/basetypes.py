import unittest
from fluggo.media.basetypes import *

class test_v2f(unittest.TestCase):
    def test_convert(self):
        self.assertEqual(v2f(1, 2), v2f(1.0, 2.0))
        self.assertEqual(v2f((1, 2)), v2f(1.0, 2.0))
        self.assertEqual(v2f((1.5, 2.5)), v2f(1.5, 2.5))

    def test_add(self):
        self.assertEqual(v2f(1.5, 2.5) + v2f(1.0, 1.0), v2f(2.5, 3.5))


