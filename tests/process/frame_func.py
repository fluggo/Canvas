import unittest
from fluggo.media import process
from fluggo.media.basetypes import *

class tupletester(unittest.TestCase):
    def assertTupleAlmost(self, a, b):
        self.assertEqual(len(a), len(b), "Lengths of tuples aren't equal.")

        for c, d in zip(a, b):
            self.assertAlmostEqual(c, d)

class test_LerpFunc(tupletester):

    def test_quad(self):
        func = process.LerpFunc((1.0, 2.0, 3.0, 4.0), (-1.0, -2.0, -3.0, -4.0), 5)

        self.assertTupleAlmost((1.0, 2.0, 3.0, 4.0), func.get_values(0)[0])
        self.assertTupleAlmost((0.5, 1.0, 1.5, 2.0), func.get_values(1)[0])
        self.assertTupleAlmost((0.0, 0.0, 0.0, 0.0), func.get_values(2)[0])
        self.assertTupleAlmost((-0.5, -1.0, -1.5, -2.0), func.get_values(3)[0])
        self.assertTupleAlmost((-1.0, -2.0, -3.0, -4.0), func.get_values(4)[0])

        a = [(-1.0, -2.0, -3.0, -4.0), (0.5, 1.0, 1.5, 2.0), (0.0, 0.0, 0.0, 0.0), (1.0, 2.0, 3.0, 4.0), (-0.5, -1.0, -1.5, -2.0)]
        b = func.get_values([4, 1, 2, 0, 3])

        for a, b in zip(a, b):
            self.assertTupleAlmost(a, b)


class test_AnimationFunc(tupletester):
    def test_basic(self):
        func = process.AnimationFunc()
        func.add(process.AnimationPoint(process.POINT_HOLD, 0.0, 4.0))
        func.add(process.AnimationPoint(process.POINT_LINEAR, 1.0, 2.0))
        func.add(process.AnimationPoint(process.POINT_LINEAR, 2.0, 6.0))

        self.assertAlmostEqual(4.0, func.get_values(-0.50)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.00)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.25)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.50)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.75)[0][0])
        self.assertAlmostEqual(2.0, func.get_values( 1.00)[0][0])
        self.assertAlmostEqual(3.0, func.get_values( 1.25)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 1.50)[0][0])
        self.assertAlmostEqual(5.0, func.get_values( 1.75)[0][0])
        self.assertAlmostEqual(6.0, func.get_values( 2.00)[0][0])
        self.assertAlmostEqual(6.0, func.get_values( 2.50)[0][0])

        # Now in random order
        self.assertAlmostEqual(4.0, func.get_values(-0.50)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.75)[0][0])
        self.assertAlmostEqual(3.0, func.get_values( 1.25)[0][0])
        self.assertAlmostEqual(6.0, func.get_values( 2.50)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.00)[0][0])
        self.assertAlmostEqual(6.0, func.get_values( 2.00)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.25)[0][0])
        self.assertAlmostEqual(5.0, func.get_values( 1.75)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 0.50)[0][0])
        self.assertAlmostEqual(2.0, func.get_values( 1.00)[0][0])
        self.assertAlmostEqual(4.0, func.get_values( 1.50)[0][0])

