import unittest, fractions
from fluggo.media import process
from fluggo.media.basetypes import *

class test_SystemPresentationClock(unittest.TestCase):
    def test_callback(self):
        results = []

        def callback(speed, time, data):
            data.append((speed, time))

        clock = process.SystemPresentationClock()
        handle = clock.register_callback(callback, results)
        clock.play(1)

        self.assertEqual((fractions.Fraction(1, 1), 0), results[0])

        clock.stop()

        self.assertEqual(fractions.Fraction(0, 1), results[1][0])
        time = results[1][1]

        clock.play(fractions.Fraction(-1, 2))

        self.assertEqual((fractions.Fraction(-1, 2), time), results[2])

        handle.unregister()

        clock.stop()
        self.assertEqual(len(results), 3)

