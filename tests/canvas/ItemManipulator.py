import unittest
from fluggo.media import process, formats
from fluggo.media.basetypes import *
from fluggo.editor import model

vidformat = formats.StreamFormat('video')
vidformat.override[formats.VideoProperty.SAMPLE_ASPECT_RATIO] = fractions.Fraction(40, 33)
vidformat.override[formats.VideoProperty.FRAME_RATE] = fractions.Fraction(24000, 1001)
vidformat.override[formats.VideoProperty.MAX_DATA_WINDOW] = box2i((0, -1), (719, 478))
audformat = formats.StreamFormat('audio')
audformat.override[formats.AudioProperty.SAMPLE_RATE] = 48000
audformat.override[formats.AudioProperty.CHANNELS] = ['FL', 'FR']

class test_ItemManipulator(unittest.TestCase):
    def test_sample(self):
        '''Sample'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=30, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0))]

        self.assertEquals(space[0].source.source_name, 'red')
        self.assertEquals(space[1].source.source_name, 'green')

    def test_1_grab_move_once(self):
        '''Grab and do a single move'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=30, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0))]

        manip = model.ItemManipulator([space[0]], 5, 5.0)

        self.assertEquals(manip.can_set_space_item(space, 10, 10.0), True)

        self.assertEquals(space[0].x, 0)
        self.assertEquals(space[0].y, 0.0)

        self.assertEquals(manip.set_space_item(space, 10, 10.0), True)

        self.assertEquals(space[0].source.source_name, 'red')
        self.assertEquals(space[0].x, 5)
        self.assertEquals(space[0].y, 5.0)
        self.assertEquals(space[0].height, 20.0)
        self.assertEquals(space[0].length, 30)
        self.assertEquals(space[0].offset, 0)

        self.assertEquals(space[1].source.source_name, 'green')
        self.assertEquals(space[1].x, 20)
        self.assertEquals(space[1].y, 10.0)
        self.assertEquals(space[1].height, 15.0)
        self.assertEquals(space[1].length, 35)
        self.assertEquals(space[1].offset, 10)

        self.assertEquals(manip.finish(), True)

        self.assertEquals(space[0].source.source_name, 'red')
        self.assertEquals(space[0].x, 5)
        self.assertEquals(space[0].y, 5.0)
        self.assertEquals(space[0].height, 20.0)
        self.assertEquals(space[0].length, 30)
        self.assertEquals(space[0].offset, 0)

        self.assertEquals(space[1].source.source_name, 'green')
        self.assertEquals(space[1].x, 20)
        self.assertEquals(space[1].y, 10.0)
        self.assertEquals(space[1].height, 15.0)
        self.assertEquals(space[1].length, 35)
        self.assertEquals(space[1].offset, 10)

    def test_1_grab_move_twice(self):
        '''Grab and do two moves'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=30, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0))]

        manip = model.ItemManipulator([space[0]], 5, 5.0)

        self.assertEquals(manip.set_space_item(space, 10, 10.0), True)
        self.assertEquals(manip.can_set_space_item(space, -5, 7.0), True)

        self.assertEquals(space[0].x, 5)
        self.assertEquals(space[0].y, 5.0)

        self.assertEquals(manip.set_space_item(space, -5, 7.0), True)

        self.assertEquals(space[0].source.source_name, 'red')
        self.assertEquals(space[0].x, -10)
        self.assertEquals(space[0].y, 2.0)
        self.assertEquals(space[0].height, 20.0)
        self.assertEquals(space[0].length, 30)
        self.assertEquals(space[0].offset, 0)

        self.assertEquals(space[1].source.source_name, 'green')
        self.assertEquals(space[1].x, 20)
        self.assertEquals(space[1].y, 10.0)
        self.assertEquals(space[1].height, 15.0)
        self.assertEquals(space[1].length, 35)
        self.assertEquals(space[1].offset, 10)

        self.assertEquals(manip.finish(), True)

        self.assertEquals(space[0].source.source_name, 'red')
        self.assertEquals(space[0].x, -10)
        self.assertEquals(space[0].y, 2.0)
        self.assertEquals(space[0].height, 20.0)
        self.assertEquals(space[0].length, 30)
        self.assertEquals(space[0].offset, 0)

        self.assertEquals(space[1].source.source_name, 'green')
        self.assertEquals(space[1].x, 20)
        self.assertEquals(space[1].y, 10.0)
        self.assertEquals(space[1].height, 15.0)
        self.assertEquals(space[1].length, 35)
        self.assertEquals(space[1].offset, 10)

    def test_one_item_add_seq(self):
        '''Drag one item into a sequence'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(manip.can_set_sequence_item(seq, -6, 'add'), True, 'Before sequence')
        self.assertEquals(len(seq), 2)
        self.assertNotEquals(item.space, None)
        self.assertEquals(manip.set_sequence_item(seq, -6, 'add'), True, 'Before sequence')
        self.assertEquals(seq.x, -6)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, -1)

        self.assertEquals(manip.can_set_sequence_item(seq, -5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(seq.x, -6)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, -1)
        self.assertEquals(manip.set_sequence_item(seq, -5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(seq.x, -5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, -4, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, -5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(manip.set_sequence_item(seq, -4, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, -4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 5, 'add'), True, 'Beginning of sequence, full overlap')
        self.assertEquals(seq.x, -4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)
        self.assertEquals(manip.set_sequence_item(seq, 5, 'add'), True, 'Beginning of sequence, full overlap')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 6, 'add'), False, 'Cross two transitions')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 9, 'add'), False, 'Cross two transitions')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 10, 'add'), True, 'Cross transition')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 10)
        self.assertEquals(manip.set_sequence_item(seq, 10, 'add'), True, 'Cross transition')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 10)
        self.assertEquals(seq[2].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 15, 'add'), True, 'Cross transition')
        self.assertEquals(manip.set_sequence_item(seq, 15, 'add'), True, 'Cross transition')
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[2].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 16, 'add'), False, 'Cross two transitions (end)')
        self.assertEquals(manip.can_set_sequence_item(seq, 19, 'add'), False, 'Cross two transitions (end)')

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True, 'End, full overlap')
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True, 'End, full overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 29, 'add'), True, 'End, one frame overlap')
        self.assertEquals(manip.set_sequence_item(seq, 29, 'add'), True, 'End, one frame overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True, 'End, one frame overlap')
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True, 'End, one frame overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 30, 'add'), True, 'End, no overlap')
        self.assertEquals(manip.set_sequence_item(seq, 30, 'add'), True, 'End, no overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, 31, 'add'), True, 'After sequence')
        self.assertEquals(manip.set_sequence_item(seq, 31, 'add'), True, 'After sequence')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, -1)

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_gap(self):
        '''Drag one item into a sequence in the middle of a gap'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=-6)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertNotEquals(item.space, None)
        self.assertEquals(seq.x, 10)
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True)
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[2].transition_length, 9)
        self.assertEquals(seq[2].x, 16)

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_gap_short(self):
        '''Drag one short item into a sequence at the beginning of a gap'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=3, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=-6)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertNotEquals(item.space, None)
        self.assertEquals(seq.x, 10)
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True)
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[2].transition_length, -3)
        self.assertEquals(seq[2].x, 16)

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_cross_transition(self):
        '''Drag one short item into a sequence where it should fail to insert across a transition'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=3, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=5)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        # Can't set from 11 (just after clip #1 starts) to 22 (three frames before clip #2 ends)
        for x in range(11, 22):
            self.assertEquals(manip.can_set_sequence_item(seq, x, 'add'), False, 'Test failed for x = {0}'.format(x))

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_backwards(self):
        '''Like test_one_item_add_seq, but in reverse order'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(len(seq), 2)
        self.assertNotEquals(item.space, None)

        self.assertEquals(manip.can_set_sequence_item(seq, 31, 'add'), True, 'After sequence')
        self.assertEquals(manip.set_sequence_item(seq, 31, 'add'), True, 'End, no overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, -1)

        self.assertEquals(manip.can_set_sequence_item(seq, 30, 'add'), True, 'End, no overlap')
        self.assertEquals(manip.set_sequence_item(seq, 30, 'add'), True, 'End, no overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True, 'End, one frame overlap')
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True, 'End, one frame overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 29, 'add'), True, 'End, one frame overlap')
        self.assertEquals(manip.set_sequence_item(seq, 29, 'add'), True, 'End, one frame overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True, 'End, full overlap')
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True, 'End, full overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 15, 'add'), True, 'Cross transition')
        self.assertEquals(manip.set_sequence_item(seq, 15, 'add'), True, 'Cross transition')
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[2].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, 10, 'add'), True, 'Cross transition')
        self.assertEquals(manip.set_sequence_item(seq, 10, 'add'), True, 'Cross transition')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 10)
        self.assertEquals(seq[2].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 9, 'add'), False, 'Cross two transitions')
        self.assertEquals(manip.can_set_sequence_item(seq, 6, 'add'), False, 'Cross two transitions')

        self.assertEquals(manip.can_set_sequence_item(seq, 5, 'add'), True, 'Beginning of sequence, full overlap')
        self.assertEquals(manip.set_sequence_item(seq, 5, 'add'), True, 'Beginning of sequence, full overlap')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 10)

        self.assertEquals(manip.can_set_sequence_item(seq, -4, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(manip.set_sequence_item(seq, -4, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, -4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, -5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(manip.set_sequence_item(seq, -5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(seq.x, -5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, -6, 'add'), True, 'Before sequence')
        self.assertEquals(manip.set_sequence_item(seq, -6, 'add'), True, 'Before sequence')
        self.assertEquals(seq.x, -6)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, -1)

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_overlap(self):
        '''Drag one item into a sequence'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=15, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10, transition_length=5)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(manip.can_set_sequence_item(seq, -6, 'add'), True, 'Before sequence')
        self.assertEquals(len(seq), 2)
        self.assertNotEquals(item.space, None)
        self.assertEquals(manip.set_sequence_item(seq, -6, 'add'), True, 'Before sequence')
        self.assertEquals(seq.x, -6)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, -1)

        self.assertEquals(manip.can_set_sequence_item(seq, -5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(manip.set_sequence_item(seq, -5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(seq.x, -5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, -4, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, -5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(manip.set_sequence_item(seq, -4, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, -4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 5, 'add'), False, 'Beginning of sequence, full overlap')
        self.assertEquals(seq.x, -4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)
        self.assertEquals(manip.set_sequence_item(seq, 5, 'add'), False, 'Beginning of sequence, full overlap')

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_short(self):
        '''Drag one item into a sequence, but the item is short'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=15, source=model.StreamSourceRef('red', 0), type='noon'),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(manip.can_set_sequence_item(seq, 4, 'add'), True, 'Before sequence')
        self.assertEquals(len(seq), 2)
        self.assertNotEquals(item.space, None)
        self.assertEquals(manip.set_sequence_item(seq, 4, 'add'), True, 'Before sequence')
        self.assertEquals(seq.x, 4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, -1)
        self.assertEquals(seq[0].offset, 15)
        self.assertEquals(seq[0].type(), 'noon')

        self.assertEquals(manip.can_set_sequence_item(seq, 5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(seq.x, 4)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, -1)
        self.assertEquals(manip.set_sequence_item(seq, 5, 'add'), True, 'Beginning of sequence, no overlap')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, 6, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, 5)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(manip.set_sequence_item(seq, 6, 'add'), True, 'Beginning of sequence, one frame overlap')
        self.assertEquals(seq.x, 6)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 10, 'add'), True, 'Beginning of sequence, full overlap')
        self.assertEquals(seq.x, 6)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 1)
        self.assertEquals(manip.set_sequence_item(seq, 10, 'add'), True, 'Beginning of sequence, full overlap')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 11, 'add'), False, 'Middle of clip')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 14, 'add'), False, 'Middle of clip')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 15, 'add'), True, 'Cross transition')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'red')
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(manip.set_sequence_item(seq, 15, 'add'), True, 'Cross transition')
        self.assertEquals(seq.x, 10)
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[2].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, 16, 'add'), True, 'Cross transition')
        self.assertEquals(manip.set_sequence_item(seq, 16, 'add'), True, 'Cross transition')
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 4)
        self.assertEquals(seq[2].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True, 'Cross transition')
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True, 'Cross transition')
        self.assertEquals(len(seq), 3)
        self.assertEquals(item.space, None)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'red')
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 21, 'add'), False, 'Middle of clip')
        self.assertEquals(manip.can_set_sequence_item(seq, 24, 'add'), False, 'Middle of clip')

        self.assertEquals(manip.can_set_sequence_item(seq, 25, 'add'), True, 'End, full overlap')
        self.assertEquals(manip.set_sequence_item(seq, 25, 'add'), True, 'End, full overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 5)

        self.assertEquals(manip.can_set_sequence_item(seq, 29, 'add'), True, 'End, one frame overlap')
        self.assertEquals(manip.set_sequence_item(seq, 29, 'add'), True, 'End, one frame overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 1)

        self.assertEquals(manip.can_set_sequence_item(seq, 30, 'add'), True, 'End, no overlap')
        self.assertEquals(manip.set_sequence_item(seq, 30, 'add'), True, 'End, no overlap')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, 0)

        self.assertEquals(manip.can_set_sequence_item(seq, 31, 'add'), True, 'After sequence')
        self.assertEquals(manip.set_sequence_item(seq, 31, 'add'), True, 'After sequence')
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(seq[2].source.source_name, 'red')
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[2].transition_length, -1)

        self.assertEquals(manip.finish(), True)

    def test_one_item_add_seq_reset(self):
        '''Test resets from various spots'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Clip(x=20, y=10.0, height=15.0, length=35, offset=10, source=model.StreamSourceRef('green', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        manip = model.ItemManipulator([space[0]], 0, 0.0)
        item = space[0]
        seq = space[2]

        self.assertEquals(manip.set_sequence_item(seq, 6, 'add'), True)
        manip.reset()

        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertNotEquals(item.space, None)

        self.assertEquals(manip.set_sequence_item(seq, 16, 'add'), True)
        manip.reset()

        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertNotEquals(item.space, None)

        self.assertEquals(manip.set_sequence_item(seq, 26, 'add'), True)
        manip.reset()

        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertNotEquals(item.space, None)

    def test_seq_item_simple_move(self):
        '''Test moving a single sequence item around within the same sequence'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        seq = space[0]
        item = seq[0]

        manip = model.ItemManipulator([item], 10, 10.0)

        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 10, 'add'), True) # Leaving it where it is should work
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 10, 'add'), True) # Leaving it where it is should work
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 5, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 5, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 5)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 15)
        self.assertEquals(seq[1].transition_length, -5)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 15, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 5)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 15)
        self.assertEquals(seq[1].transition_length, -5)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 15, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 15)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 5)
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 25, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 15)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 5)
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 25, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 20)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(seq[1].x, 5)
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[1].source.source_name, 'seq1')

        self.assertEquals(manip.can_set_sequence_item(seq, 35, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 20)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(seq[1].x, 5)
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[1].source.source_name, 'seq1')
        self.assertEquals(manip.set_sequence_item(seq, 35, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 20)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(seq[1].x, 15)
        self.assertEquals(seq[1].transition_length, -5)
        self.assertEquals(seq[1].source.source_name, 'seq1')

        manip.reset()
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        manip.finish()

    def test_seq_item_simple_move_middle(self):
        '''Test moving a single sequence item starting in the middle'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [model.Clip(x=0, y=0.0, height=20.0, length=5, offset=0, source=model.StreamSourceRef('red', 0)),
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=3, length=10, transition_length=3)])]

        seq = space[1]
        item = space[0]

        # Weird case I found where moving an item in the middle caused an existing gap to increase
        manip = model.ItemManipulator([item], 0, 0.0)
        self.assertEquals(manip.set_sequence_item(seq, 35, 'add'), True)
        manip.finish()

        manip = model.ItemManipulator([seq[1]], 17, 0.0)

        self.assertEquals(seq[1].x, 7)
        self.assertEquals(seq[1].transition_length, 3)
        self.assertEquals(seq[2].x, 25)
        self.assertEquals(seq[2].transition_length, -8)
        self.assertEquals(manip.set_space_item(space, 0, 0.0), True)
        self.assertEquals(seq[1].x, 25)
        self.assertEquals(manip.set_sequence_item(seq, 17, 'add'), True)
        self.assertEquals(seq[1].x, 7)
        self.assertEquals(seq[1].transition_length, 3)
        self.assertEquals(seq[2].x, 25)
        self.assertEquals(seq[2].transition_length, -8)
        self.assertEquals(manip.set_space_item(space, 0, 0.0), True)
        self.assertEquals(manip.set_sequence_item(seq, 18, 'add'), True)
        self.assertEquals(seq[1].x, 8)
        self.assertEquals(seq[1].transition_length, 2)
        self.assertEquals(seq[2].x, 25)
        self.assertEquals(seq[2].transition_length, -7)

    def test_seq_item_simple_move_backwards(self):
        '''Test moving a single sequence item around within the same sequence'''
        space = model.Space(u'', vidformat, audformat)
        space[:] = [
            model.Sequence(x=10, y=10.0, items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)])]

        seq = space[0]
        item = seq[1]

        manip = model.ItemManipulator([item], 20, 10.0)

        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 20, 'add'), True) # Leaving it where it is should work
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 20, 'add'), True) # Leaving it where it is should work
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 25, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 25, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 15)
        self.assertEquals(seq[1].transition_length, -5)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 15, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 15)
        self.assertEquals(seq[1].transition_length, -5)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 15, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 5)
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_sequence_item(seq, 4, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 5)
        self.assertEquals(seq[1].transition_length, 5)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_sequence_item(seq, 4, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 4)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(seq[1].x, 6)
        self.assertEquals(seq[1].transition_length, 4)
        self.assertEquals(seq[1].source.source_name, 'seq1')

        self.assertEquals(manip.can_set_sequence_item(seq, -5, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 4)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(seq[1].x, 6)
        self.assertEquals(seq[1].transition_length, 4)
        self.assertEquals(seq[1].source.source_name, 'seq1')
        self.assertEquals(manip.set_sequence_item(seq, -5, 'add'), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, -5)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(seq[1].x, 15)
        self.assertEquals(seq[1].transition_length, -5)
        self.assertEquals(seq[1].source.source_name, 'seq1')

        manip.reset()
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        manip.finish()

    def test_seq_item_single_move_space(self):
        '''Move a single sequence item into a space, where it should manifest as a clip'''
        space = model.Space(u'', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=12, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)
        item = seq[0]

        manip = model.ItemManipulator([item], 10, 10.0)

        self.assertEquals(len(seq), 2)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 6)
        self.assertEquals(seq[1].transition_length, 4)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_space_item(space, 4, 19.0), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 6)
        self.assertEquals(seq[1].transition_length, 4)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(manip.set_space_item(space, 4, 19.0), True)
        self.assertEquals(len(seq), 1)
        self.assertEquals(seq.x, 16)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(len(space), 1)
        self.assertEquals(space[0].x, 4)
        self.assertEquals(space[0].y, 19.0)
        self.assertEquals(space[0].length, 10)
        self.assertEquals(space[0].height, 3.0)
        self.assertEquals(space[0].source.source_name, 'seq1')
        self.assertEquals(space[0].type(), 'video')
        self.assertEquals(space[0].offset, 12)

        manip.reset()
        self.assertEquals(len(seq), 2)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 6)
        self.assertEquals(seq[1].transition_length, 4)
        self.assertEquals(seq[1].source.source_name, 'seq2')

        manip.finish()

    def test_seq_item_multiple_move_space(self):
        '''Move a multiple sequences item into a space, where they should manifest as a sequence'''
        space = model.Space(u'', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=6, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq1.5', 0), offset=13, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)

        manip = model.ItemManipulator(seq[0:2], 10, 10.0)

        self.assertEquals(len(seq), 3)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq1.5')
        self.assertEquals(seq[2].x, 16)
        self.assertEquals(seq[2].transition_length, 4)
        self.assertEquals(seq[2].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_space_item(space, 4, 19.0), True)
        self.assertEquals(len(seq), 3)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq1.5')
        self.assertEquals(seq[2].x, 16)
        self.assertEquals(seq[2].transition_length, 4)
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(manip.set_space_item(space, 4, 19.0), True)
        self.assertEquals(len(seq), 1)
        self.assertEquals(seq.x, 26)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq2')
        self.assertEquals(len(space), 1)
        self.assertEquals(len(space[0]), 2)
        self.assertEquals(space[0].x, 4)
        self.assertEquals(space[0].y, 19.0)
        self.assertEquals(space[0].height, 3.0)
        self.assertEquals(space[0].type(), 'video')
        self.assertEquals(space[0][0].x, 0)
        self.assertEquals(space[0][0].source.source_name, 'seq1')
        self.assertEquals(space[0][0].type(), 'video')
        self.assertEquals(space[0][0].offset, 6)
        self.assertEquals(space[0][1].x, 10)
        self.assertEquals(space[0][1].source.source_name, 'seq1.5')
        self.assertEquals(space[0][1].type(), 'video')
        self.assertEquals(space[0][1].offset, 13)

        manip.reset()
        self.assertEquals(len(seq), 3)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq1.5')
        self.assertEquals(seq[2].x, 16)
        self.assertEquals(seq[2].transition_length, 4)
        self.assertEquals(seq[2].source.source_name, 'seq2')

        manip.finish()

    def test_seq_item_single_move_space_from_middle(self):
        '''Move a single sequence item from the middle of a sequence into a space, where it should manifest as a clip and leave a gap behind'''
        space = model.Space(u'', vidformat, audformat)

        seq = model.Sequence(x=10, y=10.0, type='video', items=[model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=12, length=10),
                model.SequenceItem(source=model.StreamSourceRef('seq1.5', 0), offset=18, length=10, transition_length=0),
                model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=21, length=10, transition_length=4)], height=3.0)
        item = seq[1]

        manip = model.ItemManipulator([item], 20, 10.0)

        self.assertEquals(len(seq), 3)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq1.5')
        self.assertEquals(seq[2].x, 16)
        self.assertEquals(seq[2].transition_length, 4)
        self.assertEquals(seq[2].source.source_name, 'seq2')

        self.assertEquals(manip.can_set_space_item(space, 4, 19.0), True)
        self.assertEquals(len(seq), 3)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq1.5')
        self.assertEquals(seq[2].x, 16)
        self.assertEquals(seq[2].transition_length, 4)
        self.assertEquals(seq[2].source.source_name, 'seq2')
        self.assertEquals(manip.set_space_item(space, 4, 19.0), True)
        self.assertEquals(len(seq), 2)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 16)
        self.assertEquals(seq[1].transition_length, -6)
        self.assertEquals(seq[1].source.source_name, 'seq2')
        self.assertEquals(len(space), 1)
        self.assertEquals(space[0].x, 4)
        self.assertEquals(space[0].y, 19.0)
        self.assertEquals(space[0].length, 10)
        self.assertEquals(space[0].height, 3.0)
        self.assertEquals(space[0].source.source_name, 'seq1.5')
        self.assertEquals(space[0].type(), 'video')
        self.assertEquals(space[0].offset, 18)

        manip.reset()
        self.assertEquals(len(seq), 3)
        self.assertEquals(len(space), 0)
        self.assertEquals(seq.x, 10)
        self.assertEquals(seq[0].x, 0)
        self.assertEquals(seq[0].transition_length, 0)
        self.assertEquals(seq[0].source.source_name, 'seq1')
        self.assertEquals(seq[1].x, 10)
        self.assertEquals(seq[1].transition_length, 0)
        self.assertEquals(seq[1].source.source_name, 'seq1.5')
        self.assertEquals(seq[2].x, 16)
        self.assertEquals(seq[2].transition_length, 4)
        self.assertEquals(seq[2].source.source_name, 'seq2')

        manip.finish()

