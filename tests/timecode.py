import unittest
from fluggo.media import timecode

class test_format(unittest.TestCase):
    def test_Frames(self):
        t = timecode.Frames()

        self.assertEqual(t.format(3), '3')
        self.assertEqual(t.format(-200), '-200')

    def test_TimeAndFrames30(self):
        t = timecode.TimeAndFrames(30)

        self.assertEqual(t.format(0), '00:00:00:00')
        self.assertEqual(t.format(3), '00:00:00:03')
        self.assertEqual(t.format(-3), '-00:00:00:03')
        self.assertEqual(t.format(60*60*30), '01:00:00:00')
        self.assertEqual(t.format(60*60*30 - 1), '00:59:59:29')

    def test_TimeAndFrames24(self):
        t = timecode.TimeAndFrames(24)

        self.assertEqual(t.format(0), '00:00:00:00')
        self.assertEqual(t.format(3), '00:00:00:03')
        self.assertEqual(t.format(-3), '-00:00:00:03')
        self.assertEqual(t.format(60*60*24), '01:00:00:00')
        self.assertEqual(t.format(60*60*24 - 1), '00:59:59:23')

    def test_NtscDropFrame(self):
        t = timecode.NtscDropFrame()

        self.assertEqual(t.format(0), '00:00:00;00')
        self.assertEqual(t.format(3), '00:00:00;03')
        self.assertEqual(t.format(-3), '-00:00:00;03')
        self.assertEqual(t.format(60*30 - 1), '00:00:59;29')
        self.assertEqual(t.format(60*30), '00:01:00;02')
        self.assertEqual(t.format(10*60*30 - 19), '00:09:59;29')
        self.assertEqual(t.format(10*60*30 - 18), '00:10:00;00')
        self.assertEqual(t.format(60*60*30000 // 1001), '01:00:00;00')
        self.assertEqual(t.format(60*60*30000 // 1001 - 1), '00:59:59;29')

class test_parse(unittest.TestCase):
    def test_Frames(self):
        t = timecode.Frames()

        self.assertEqual(t.parse('3'), 3)
        self.assertEqual(t.parse('-200'), -200)

    def test_TimeAndFrames30(self):
        t = timecode.TimeAndFrames(30)

        self.assertEqual(t.parse('00:00:00:00'), 0)

        self.assertEqual(t.parse('00:00:00:03'), 3)
        self.assertEqual(t.parse('00:00:03'), 3)
        self.assertEqual(t.parse('00:03'), 3)
        self.assertEqual(t.parse('3'), 3)

        self.assertEqual(t.parse('-00:00:00:03'), -3)
        self.assertEqual(t.parse('-3'), -3)

        self.assertEqual(t.parse('01:00:00:00'), 60*60*30)
        self.assertEqual(t.parse('00:59:59:29'), 60*60*30 - 1)
        self.assertEqual(t.parse('59:59:29'), 60*60*30 - 1)

    def test_TimeAndFrames24(self):
        t = timecode.TimeAndFrames(24)

        self.assertEqual(t.parse('00:00:00:00'), 0)

        self.assertEqual(t.parse('00:00:00:03'), 3)
        self.assertEqual(t.parse('00:00:03'), 3)
        self.assertEqual(t.parse('00:03'), 3)
        self.assertEqual(t.parse('3'), 3)

        self.assertEqual(t.parse('-00:00:00:03'), -3)
        self.assertEqual(t.parse('-3'), -3)

        self.assertEqual(t.parse('01:00:00:00'), 60*60*24)
        self.assertEqual(t.parse('00:59:59:23'), 60*60*24 - 1)
        self.assertEqual(t.parse('59:59:23'), 60*60*24 - 1)

    def test_NtscDropFrame(self):
        t = timecode.NtscDropFrame()

        self.assertEqual(t.parse('00:00:00;00'), 0)
        self.assertEqual(t.parse('00:00:00;03'), 3)
        self.assertEqual(t.parse('-00:00:00;03'), -3)

        self.assertEqual(t.parse('00:00:59;29'), 60*30 - 1)
        self.assertEqual(t.parse('00:01:00;02'), 60*30)
        self.assertEqual(t.parse('00:09:59;29'), 10*60*30 - 19)
        self.assertEqual(t.parse('00:10:00;00'), 10*60*30 - 18)
        self.assertEqual(t.parse('01:00:00;00'), 60*60*30000 // 1001)
        self.assertEqual(t.parse('00:59:59;29'), 60*60*30000 // 1001 - 1)

