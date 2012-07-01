import unittest
from fluggo.media import process, formats
from fluggo.media.basetypes import *
from fluggo.editor import model, plugins
from PyQt4.QtGui import *

vidformat = plugins.VideoFormat(
    pixel_aspect_ratio = fractions.Fraction(40, 33),
    frame_rate = fractions.Fraction(24000, 1001),
    full_frame = box2i((0, -1), (719, 478)))

frame_rate = float(vidformat.frame_rate)

audformat = plugins.AudioFormat(
    sample_rate = 48000, channel_assignment=['FL', 'FR'])

class test_ClipManipulator(unittest.TestCase):
    def test_sample(self):
        '''Sample'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=30, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0))]

        self.assertEqual(space[0].source.source_name, 'red')
        self.assertEqual(space[1].source.source_name, 'green')

    def test_1_grab_move_once(self):
        '''Grab and do a single move'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=30, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0))]

        manip = model.ClipManipulator(space[0], 5, 5.0)

        manip.set_space_item(space, 10, 10.0)

        self.assertEqual(space[0].source.source_name, 'red')
        self.assertEqual(space[0].x, 5)
        self.assertEqual(space[0].y, 5.0)
        self.assertEqual(space[0].height, 20.0)
        self.assertEqual(space[0].length, 30)
        self.assertEqual(space[0].offset, 0)

        self.assertEqual(space[1].source.source_name, 'green')
        self.assertEqual(space[1].x, 20)
        self.assertEqual(space[1].y, 10.0)
        self.assertEqual(space[1].height, 15.0)
        self.assertEqual(space[1].length, 35)
        self.assertEqual(space[1].offset, 10)

        self.assertIsInstance(manip.finish(), QUndoCommand)

        self.assertEqual(space[0].source.source_name, 'red')
        self.assertEqual(space[0].x, 5)
        self.assertEqual(space[0].y, 5.0)
        self.assertEqual(space[0].height, 20.0)
        self.assertEqual(space[0].length, 30)
        self.assertEqual(space[0].offset, 0)

        self.assertEqual(space[1].source.source_name, 'green')
        self.assertEqual(space[1].x, 20)
        self.assertEqual(space[1].y, 10.0)
        self.assertEqual(space[1].height, 15.0)
        self.assertEqual(space[1].length, 35)
        self.assertEqual(space[1].offset, 10)

    def test_1_grab_move_twice(self):
        '''Grab and do two moves'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=30, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0))]

        manip = model.ClipManipulator(space[0], 5, 5.0)

        manip.set_space_item(space, 10, 10.0)

        self.assertEqual(space[0].x, 5)
        self.assertEqual(space[0].y, 5.0)

        manip.set_space_item(space, -5, 7.0)

        self.assertEqual(space[0].source.source_name, 'red')
        self.assertEqual(space[0].x, -10)
        self.assertEqual(space[0].y, 2.0)
        self.assertEqual(space[0].height, 20.0)
        self.assertEqual(space[0].length, 30)
        self.assertEqual(space[0].offset, 0)

        self.assertEqual(space[1].source.source_name, 'green')
        self.assertEqual(space[1].x, 20)
        self.assertEqual(space[1].y, 10.0)
        self.assertEqual(space[1].height, 15.0)
        self.assertEqual(space[1].length, 35)
        self.assertEqual(space[1].offset, 10)

        self.assertIsInstance(manip.finish(), QUndoCommand)

        self.assertEqual(space[0].source.source_name, 'red')
        self.assertEqual(space[0].x, -10)
        self.assertEqual(space[0].y, 2.0)
        self.assertEqual(space[0].height, 20.0)
        self.assertEqual(space[0].length, 30)
        self.assertEqual(space[0].offset, 0)

        self.assertEqual(space[1].source.source_name, 'green')
        self.assertEqual(space[1].x, 20)
        self.assertEqual(space[1].y, 10.0)
        self.assertEqual(space[1].height, 15.0)
        self.assertEqual(space[1].length, 35)
        self.assertEqual(space[1].offset, 10)

    def test_one_item_add_seq(self):
        '''Drag one item into a sequence'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEqual(len(seq), 2)
        self.assertNotEqual(item.space, None)

        manip.set_sequence_item(seq, -6, 'add')     # Before sequence
        self.assertEqual(seq.x, -6)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, -1)

        manip.set_sequence_item(seq, -5, 'add')     # Beginning of sequence, no overlap
        self.assertEqual(seq.x, -5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 0)

        manip.set_sequence_item(seq, -4, 'add')     # Beginning of sequence, one frame overlap
        self.assertEqual(seq.x, -4)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 1)

        manip.set_sequence_item(seq, 5, 'add')      # Beginning of sequence, full overlap
        self.assertEqual(seq.x, 5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 10)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 6, 'add')      # Cross two transitions

        # We now expect that the clip has disappeared from both the space *and*
        # the sequence; if we want it back, we need to reset() (to put it back
        # in the space) or specify a valid target sequence.
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 2)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].transition_length, 0)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 9, 'add')      # Cross two transitions

        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 2)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].transition_length, 0)

        manip.set_sequence_item(seq, 10, 'add')         # Cross transition
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 10)
        self.assertEqual(seq[2].transition_length, 5)

        manip.set_sequence_item(seq, 15, 'add')         # Cross transition
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 5)
        self.assertEqual(seq[2].transition_length, 10)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 16, 'add')     # Cross two transitions (end)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 19, 'add')     # Cross two transitions (end)

        manip.set_sequence_item(seq, 20, 'add')         # End, full overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 10)

        manip.set_sequence_item(seq, 29, 'add')         # End, one frame overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 1)

        manip.set_sequence_item(seq, 20, 'add')         # End, one frame overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 10)

        manip.set_sequence_item(seq, 30, 'add')         # End, no overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 0)

        manip.set_sequence_item(seq, 31, 'add')         # After sequence
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, -1)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_one_item_add_seq_gap(self):
        '''Drag one item into a sequence in the middle of a gap'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=-6)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEqual(len(seq), 2)
        self.assertNotEqual(item.space, None)
        self.assertEqual(seq.x, 10)

        manip.set_sequence_item(seq, 20, 'add')
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[2].transition_length, 9)
        self.assertEqual(seq[2].x, 16)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_one_item_add_seq_gap_short(self):
        '''Drag one short item into a sequence at the beginning of a gap'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=3, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=-6)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEqual(len(seq), 2)
        self.assertNotEqual(item.space, None)
        self.assertEqual(seq.x, 10)

        manip.set_sequence_item(seq, 20, 'add')
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[2].transition_length, -3)
        self.assertEqual(seq[2].x, 16)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_one_item_add_seq_cross_transition(self):
        '''Drag one short item into a sequence where it should fail to insert across a transition'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=3, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=5)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        # Can't set from 11 (just after clip #1 starts) to 22 (three frames before clip #2 ends)
        for x in range(11, 22):
            try:
                manip.set_sequence_item(seq, x, 'add')
                self.fail('Did not throw exception for x = {0}'.format(x))
            except model.NoRoomError:
                pass

        with self.assertRaises(RuntimeError):
            manip.finish()

    def test_one_item_add_seq_backwards(self):
        '''Like test_one_item_add_seq, but in reverse order'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEqual(len(seq), 2)
        self.assertNotEqual(item.space, None)

        manip.set_sequence_item(seq, 31, 'add')         # End, no overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, -1)

        manip.set_sequence_item(seq, 30, 'add')         # End, no overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 0)

        manip.set_sequence_item(seq, 20, 'add')         # End, one frame overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 10)

        manip.set_sequence_item(seq, 29, 'add')         # End, one frame overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 1)

        manip.set_sequence_item(seq, 20, 'add')         # End, full overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 10)

        manip.set_sequence_item(seq, 15, 'add')         # Cross transition
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 5)
        self.assertEqual(seq[2].transition_length, 10)

        manip.set_sequence_item(seq, 10, 'add')         # Cross transition
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 10)
        self.assertEqual(seq[2].transition_length, 5)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 9, 'add')      # Cross two transitions

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 6, 'add')      # Cross two transitions

        manip.set_sequence_item(seq, 5, 'add')          # Beginning of sequence, full overlap
        self.assertEqual(seq.x, 5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 10)

        manip.set_sequence_item(seq, -4, 'add')         # Beginning of sequence, one frame overlap
        self.assertEqual(seq.x, -4)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 1)

        manip.set_sequence_item(seq, -5, 'add')         # Beginning of sequence, no overlap
        self.assertEqual(seq.x, -5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 0)

        manip.set_sequence_item(seq, -6, 'add')         # Before sequence')
        self.assertEqual(seq.x, -6)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, -1)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_one_item_add_seq_overlap(self):
        '''Drag one item into a sequence'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=5)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEqual(len(seq), 2)
        self.assertNotEqual(item.space, None)

        manip.set_sequence_item(seq, -6, 'add')         # Before sequence
        self.assertEqual(seq.x, -6)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, -1)

        manip.set_sequence_item(seq, -5, 'add')         # Beginning of sequence, no overlap
        self.assertEqual(seq.x, -5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 0)

        manip.set_sequence_item(seq, -4, 'add')         # Beginning of sequence, one frame overlap
        self.assertEqual(seq.x, -4)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 1)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 5, 'add')      # Beginning of sequence, full overlap

        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 2)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].transition_length, 5)

        with self.assertRaises(RuntimeError):
            manip.finish()

    def test_one_item_add_seq_short(self):
        '''Drag one item into a sequence, but the item is short'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=15, source=model.StreamSourceRef('red', 0), type='noon'),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(type='noon', x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEqual(len(seq), 2)
        self.assertNotEqual(item.space, None)

        manip.set_sequence_item(seq, 4, 'add')          # Before sequence
        self.assertEqual(seq.x, 4)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, -1)
        self.assertEqual(seq[0].offset, 15)
        self.assertEqual(seq[0].type(), 'noon')

        manip.set_sequence_item(seq, 5, 'add')          # Beginning of sequence, no overlap
        self.assertEqual(seq.x, 5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 0)

        manip.set_sequence_item(seq, 6, 'add')          # Beginning of sequence, one frame overlap
        self.assertEqual(seq.x, 6)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 1)

        manip.set_sequence_item(seq, 10, 'add')         # Beginning of sequence, full overlap
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'red')
        self.assertEqual(seq[1].transition_length, 5)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 11, 'add')     # Middle of clip

        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 2)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].transition_length, 0)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 14, 'add')     # Middle of clip

        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 2)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].transition_length, 0)

        manip.set_sequence_item(seq, 15, 'add')         # Cross transition
        self.assertEqual(seq.x, 10)
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 5)
        self.assertEqual(seq[2].transition_length, 0)

        manip.set_sequence_item(seq, 16, 'add')         # Cross transition
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 4)
        self.assertEqual(seq[2].transition_length, 1)

        manip.set_sequence_item(seq, 20, 'add')         # Cross transition
        self.assertEqual(len(seq), 3)
        self.assertEqual(item.space, None)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'red')
        self.assertEqual(seq[2].source.source_name, 'seq2')
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 5)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 21, 'add')     # Middle of clip

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 24, 'add')     # Middle of clip

        manip.set_sequence_item(seq, 25, 'add')         # End, full overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 5)

        manip.set_sequence_item(seq, 29, 'add')         # End, one frame overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 1)

        manip.set_sequence_item(seq, 30, 'add')         # End, no overlap
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, 0)

        manip.set_sequence_item(seq, 31, 'add')         # After sequence
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'red')
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[2].transition_length, -1)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_one_item_add_seq_reset(self):
        '''Test resets from various spots'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ClipManipulator(space[0], 0, 0.0)
        item = space[0]
        seq = space[2]

        manip.set_sequence_item(seq, 6, 'add')
        manip.reset()

        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertNotEqual(item.space, None)

        manip.set_sequence_item(seq, 16, 'add')
        manip.reset()

        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertNotEqual(item.space, None)

        manip.set_sequence_item(seq, 26, 'add')
        manip.reset()

        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertNotEqual(item.space, None)

    def test_seq_item_simple_move_fail_moveback(self):
        '''Test that we can move an item back after failing to place it'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=3, length=10, transition_length=3)])]

        seq = space[1]
        item = space[0]

        manip = model.ClipManipulator(item, 0, 0.0)

        with self.assertRaises(model.NoRoomError):
            manip.set_sequence_item(seq, 11, 'add')

        manip.set_space_item(space, 0, 0.0)

        self.assertEqual(item.x, 0)
        self.assertEqual(item.y, 0.0)
        self.assertEqual(item.space, space)

class test_SequenceItemGroupManipulator(unittest.TestCase):
    def test_seq_item_simple_move(self):
        '''Test moving a single sequence item around within the same sequence'''
        space = model.Space('', vidformat, audformat)
        space[:] = [
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        seq = space[0]
        item = seq[0]

        manip = model.SequenceItemGroupManipulator([item], 10, 10.0)

        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 10, 'add')         # Leaving it where it is should work
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 5, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 5)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 15)
        self.assertEqual(seq[1].transition_length, -5)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 15, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 15)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 5)
        self.assertEqual(seq[1].transition_length, 5)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 25, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 20)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq2')
        self.assertEqual(seq[1].x, 5)
        self.assertEqual(seq[1].transition_length, 5)
        self.assertEqual(seq[1].source.source_name, 'seq1')

        manip.set_sequence_item(seq, 35, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 20)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq2')
        self.assertEqual(seq[1].x, 15)
        self.assertEqual(seq[1].transition_length, -5)
        self.assertEqual(seq[1].source.source_name, 'seq1')

        manip.reset()
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.finish(), None)

    def test_seq_item_simple_move_middle(self):
        '''Test moving a single sequence item starting in the middle'''
        space = model.Space('', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=3, length=10, transition_length=3)])]

        seq = space[1]
        item = space[0]

        # Weird case I found where moving an item in the middle caused an existing gap to increase
        manip = model.ClipManipulator(item, 0, 0.0)
        manip.set_sequence_item(seq, 35, 'add')
        manip.finish()

        manip = model.SequenceItemGroupManipulator([seq[1]], 17, 0.0)

        self.assertEqual(seq[1].x, 7)
        self.assertEqual(seq[1].transition_length, 3)
        self.assertEqual(seq[2].x, 25)
        self.assertEqual(seq[2].transition_length, -8)
        manip.set_space_item(space, 0, 0.0)
        self.assertEqual(seq[1].x, 25)
        manip.set_sequence_item(seq, 17, 'add')
        self.assertEqual(seq[1].x, 7)
        self.assertEqual(seq[1].transition_length, 3)
        self.assertEqual(seq[2].x, 25)
        self.assertEqual(seq[2].transition_length, -8)
        manip.set_space_item(space, 0, 0.0)
        manip.set_sequence_item(seq, 18, 'add')
        self.assertEqual(seq[1].x, 8)
        self.assertEqual(seq[1].transition_length, 2)
        self.assertEqual(seq[2].x, 25)
        self.assertEqual(seq[2].transition_length, -7)

    def test_seq_item_simple_move_backwards(self):
        '''Test moving a single sequence item around within the same sequence'''
        space = model.Space('', vidformat, audformat)
        space[:] = [
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        seq = space[0]
        item = seq[1]

        manip = model.SequenceItemGroupManipulator([item], 20, 10.0)

        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 20, 'add')         # Leaving it where it is should work
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 25, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 15)
        self.assertEqual(seq[1].transition_length, -5)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 15, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 5)
        self.assertEqual(seq[1].transition_length, 5)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_sequence_item(seq, 4, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 4)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq2')
        self.assertEqual(seq[1].x, 6)
        self.assertEqual(seq[1].transition_length, 4)
        self.assertEqual(seq[1].source.source_name, 'seq1')

        manip.set_sequence_item(seq, -5, 'add')
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, -5)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq2')
        self.assertEqual(seq[1].x, 15)
        self.assertEqual(seq[1].transition_length, -5)
        self.assertEqual(seq[1].source.source_name, 'seq1')

        manip.reset()
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.finish(), None)

    def test_seq_item_single_move_space(self):
        '''Move a single sequence item into a space, where it should manifest as a clip'''
        space = model.Space('', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=12, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)
        item = seq[0]

        manip = model.SequenceItemGroupManipulator([item], 10, 10.0)

        self.assertEqual(len(seq), 2)
        self.assertEqual(len(space), 0)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 6)
        self.assertEqual(seq[1].transition_length, 4)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        manip.set_space_item(space, 4, 19.0)
        self.assertEqual(len(seq), 1)
        self.assertEqual(seq.x, 16)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq2')
        self.assertEqual(len(space), 1)
        self.assertEqual(space[0].x, 4)
        self.assertEqual(space[0].y, 19.0)
        self.assertEqual(space[0].length, 10)
        self.assertEqual(space[0].height, 3.0)
        self.assertEqual(space[0].source.source_name, 'seq1')
        self.assertEqual(space[0].type(), 'video')
        self.assertEqual(space[0].offset, 12)

        manip.reset()
        self.assertEqual(len(seq), 2)
        self.assertEqual(len(space), 0)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 6)
        self.assertEqual(seq[1].transition_length, 4)
        self.assertEqual(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.finish(), None)

    def test_seq_item_multiple_move_space(self):
        '''Move a multiple sequences item into a space, where they should manifest as a sequence'''
        space = model.Space('', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=6, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq1.5', 0), offset=13, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)

        manip = model.SequenceItemGroupManipulator(seq[0:2], 10, 10.0)

        self.assertEqual(len(seq), 3)
        self.assertEqual(len(space), 0)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq1.5')
        self.assertEqual(seq[2].x, 16)
        self.assertEqual(seq[2].transition_length, 4)
        self.assertEqual(seq[2].source.source_name, 'seq2')

        manip.set_space_item(space, 4, 19.0)
        self.assertEqual(len(seq), 1)
        self.assertEqual(seq.x, 26)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq2')
        self.assertEqual(len(space), 1)
        self.assertEqual(len(space[0]), 2)
        self.assertEqual(space[0].x, 4)
        self.assertEqual(space[0].y, 19.0)
        self.assertEqual(space[0].height, 3.0)
        self.assertEqual(space[0].type(), 'video')
        self.assertEqual(space[0][0].x, 0)
        self.assertEqual(space[0][0].source.source_name, 'seq1')
        self.assertEqual(space[0][0].type(), 'video')
        self.assertEqual(space[0][0].offset, 6)
        self.assertEqual(space[0][1].x, 10)
        self.assertEqual(space[0][1].source.source_name, 'seq1.5')
        self.assertEqual(space[0][1].type(), 'video')
        self.assertEqual(space[0][1].offset, 13)

        manip.reset()
        self.assertEqual(len(seq), 3)
        self.assertEqual(len(space), 0)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq1.5')
        self.assertEqual(seq[2].x, 16)
        self.assertEqual(seq[2].transition_length, 4)
        self.assertEqual(seq[2].source.source_name, 'seq2')

        self.assertEquals(manip.finish(), None)

    def test_seq_item_single_move_space_from_middle(self):
        '''Move a single sequence item from the middle of a sequence into a space, where it should manifest as a clip and leave a gap behind'''
        space = model.Space('', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=12, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq1.5', 0), offset=18, length=10, transition_length=0),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)
        item = seq[1]

        manip = model.SequenceItemGroupManipulator([item], 20, 10.0)

        self.assertEqual(len(seq), 3)
        self.assertEqual(len(space), 0)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq1.5')
        self.assertEqual(seq[2].x, 16)
        self.assertEqual(seq[2].transition_length, 4)
        self.assertEqual(seq[2].source.source_name, 'seq2')

        manip.set_space_item(space, 4, 19.0)
        self.assertEqual(len(seq), 2)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 16)
        self.assertEqual(seq[1].transition_length, -6)
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(len(space), 1)
        self.assertEqual(space[0].x, 4)
        self.assertEqual(space[0].y, 19.0)
        self.assertEqual(space[0].length, 10)
        self.assertEqual(space[0].height, 3.0)
        self.assertEqual(space[0].source.source_name, 'seq1.5')
        self.assertEqual(space[0].type(), 'video')
        self.assertEqual(space[0].offset, 18)

        manip.reset()
        self.assertEqual(len(seq), 3)
        self.assertEqual(len(space), 0)
        self.assertEqual(seq.x, 10)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].x, 10)
        self.assertEqual(seq[1].transition_length, 0)
        self.assertEqual(seq[1].source.source_name, 'seq1.5')
        self.assertEqual(seq[2].x, 16)
        self.assertEqual(seq[2].transition_length, 4)
        self.assertEqual(seq[2].source.source_name, 'seq2')

        self.assertEquals(manip.finish(), None)

class test_ItemManipulator(unittest.TestCase):
    def test_move_anchored_videos(self):
        space = model.Space('', vidformat, audformat)

        item0 = model.Clip(source=model.StreamSourceRef('red', 0), x=5, y=4.5,
            offset=13, length=10, type='video')
        item1 = model.Clip(source=model.StreamSourceRef('blue', 0), x=2, y=17.3,
            offset=13, length=10, type='video', anchor=model.Anchor(target=item0))

        space[:] = [item0, item1]

        manip = model.ItemManipulator([item0], 7.0 / frame_rate, 4.5)

        manip.set_space_item(space, 8.0 / frame_rate, 4.5)
        self.assertEqual(item0.x, 6)
        self.assertEqual(item0.y, 4.5)
        self.assertEqual(item1.x, 3)
        self.assertEqual(item1.y, 17.3)

        manip.set_space_item(space, 9.3 / frame_rate, 5.0)
        self.assertEqual(item0.x, 7)
        self.assertEqual(item0.y, 5.0)
        self.assertEqual(item1.x, 4)
        self.assertEqual(item1.y, 17.3 + 0.5)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_move_seqanditems(self):
        space = model.Space('', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=12, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq1.5', 0), offset=18, length=10, transition_length=0),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)
        item = seq[1]

        space[:] = [seq]

        manip = model.ItemManipulator([seq[1], seq], 10.0 / frame_rate, 10.0)

        manip.set_space_item(space, 12.0 / frame_rate, 10.0)
        self.assertEqual(seq.x, 12)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[1].x, 10)

        manip.set_space_item(space, 8.0 / frame_rate, 10.0)
        self.assertEqual(seq.x, 8)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[1].x, 10)

        self.assertIsInstance(manip.finish(), QUndoCommand)

    def test_move_seqanditems2(self):
        space = model.Space('', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=12, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq1.5', 0), offset=18, length=10, transition_length=0),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)
        item = seq[1]

        space[:] = [seq]

        manip = model.ItemManipulator([seq, seq[1]], 10.0 / frame_rate, 10.0)

        manip.set_space_item(space, 12.0 / frame_rate, 10.0)
        self.assertEqual(seq.x, 12)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[1].x, 10)

        manip.set_space_item(space, 8.0 / frame_rate, 10.0)
        self.assertEqual(seq.x, 8)
        self.assertEqual(seq[0].x, 0)
        self.assertEqual(seq[1].x, 10)

        self.assertIsInstance(manip.finish(), QUndoCommand)

