import unittest
from fluggo.media import process

class tupletester(unittest.TestCase):
    def assertTupleAlmost(self, a, b):
        self.assertEqual(len(a), len(b), "Lengths of tuples aren't equal.")

        for a, b in zip(a, b):
            self.assertAlmostEqual(a, b)

class test_const(tupletester):
    def test_i32(self):
        self.assertEqual([(15, 0, 0, 0)], process.frame_func_get(source=15, frames=19))
        self.assertEqual([(22, 0, 0, 0)] * 3, process.frame_func_get(source=22, frames=[5, 10, 15]))
        self.assertEqual([], process.frame_func_get(source=27, frames=[]))

class test_LerpFunc(tupletester):

    def test_quad(self):
        func = process.LerpFunc((1.0, 2.0, 3.0, 4.0), (-1.0, -2.0, -3.0, -4.0), 4)

        self.assertTupleAlmost((1.0, 2.0, 3.0, 4.0), process.frame_func_get(source=func, frames=0)[0])
        self.assertTupleAlmost((0.5, 1.0, 1.5, 2.0), process.frame_func_get(source=func, frames=1)[0])
        self.assertTupleAlmost((0.0, 0.0, 0.0, 0.0), process.frame_func_get(source=func, frames=2)[0])
        self.assertTupleAlmost((-0.5, -1.0, -1.5, -2.0), process.frame_func_get(source=func, frames=3)[0])
        self.assertTupleAlmost((-1.0, -2.0, -3.0, -4.0), process.frame_func_get(source=func, frames=4)[0])

        a = [(-1.0, -2.0, -3.0, -4.0), (0.5, 1.0, 1.5, 2.0), (0.0, 0.0, 0.0, 0.0), (1.0, 2.0, 3.0, 4.0), (-0.5, -1.0, -1.5, -2.0)]
        b = process.frame_func_get(source=func, frames=[4, 1, 2, 0, 3])

        for a, b in zip(a, b):
            self.assertTupleAlmost(a, b)

if __name__ == '__main__':
    unittest.main()

