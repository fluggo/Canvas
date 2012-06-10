import unittest
from fluggo.media.basetypes import *
from fluggo.editor import model

class test_RemoveAdjacentItemsFromSequenceCommand(unittest.TestCase):
    def test_1_remove_single_from_start(self):
        '''Delete a single item from the start of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[0]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 20)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq2')
        self.assertEqual(sequence[1].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].x, 20)
        self.assertEqual(sequence[2].transition_length, 0)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_single_from_start_transition(self):
        '''Delete a single item from the start of a sequence when the next item has a transition.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=3, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[0]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 17)
        self.assertEqual(sequence.length, 20)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq2')
        self.assertEqual(sequence[1].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 7)
        self.assertEqual(sequence[1].transition_length, 3)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].transition_length, 0)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_single_from_start_gap(self):
        '''Delete a single item from the start of a sequence when the next item has a gap.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=-4, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[0]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 24)
        self.assertEqual(sequence.length, 20)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq2')
        self.assertEqual(sequence[1].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 14)
        self.assertEqual(sequence[1].transition_length, -4)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].transition_length, 0)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_single_from_middle(self):
        self.remove_single_from_middle(0, 0)
        self.remove_single_from_middle(-3, -4)
        self.remove_single_from_middle(5, 5)

    def remove_single_from_middle(self, seq2_trans=0, seq3_trans=0):
        '''Delete a single item from the middle of a sequence.'''
        # TODO: Delete when next item has (positive|negative) transition_length
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[1]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 20 - seq2_trans - seq3_trans)
        self.assertEqual(sequence[1].transition_length, -10 + seq2_trans + seq3_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10 - seq2_trans)
        self.assertEqual(sequence[1].transition_length, seq2_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_single_from_end(self):
        '''Delete a single item from the end of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[2]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence.length, 20)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].transition_length, 0)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_single_from_end_gap(self):
        '''Delete a single item with a gap from the end of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=-4, offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[2]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence.length, 20)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].transition_length, -4)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_single_from_end_transition(self):
        '''Delete a single item with a transition from the end of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=4, offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[2]])
        command.redo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence.length, 20)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10)
        self.assertEqual(sequence[1].transition_length, 0)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].transition_length, 4)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

    def test_1_remove_double_from_start_transition(self):
        self.remove_double_from_start(0, 0)
        self.remove_double_from_start(3, 4)
        self.remove_double_from_start(3, -4)
        self.remove_double_from_start(-3, 4)
        self.remove_double_from_start(-3, -4)

    def remove_double_from_start(self, seq2_trans, seq3_trans):
        '''Delete two items from the start of a sequence when the next item has a transition.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10)])

        command = model.RemoveAdjacentItemsFromSequenceCommand([sequence[0], sequence[1]])
        command.redo()

        self.assertEqual(len(sequence), 1)
        self.assertEqual(sequence.x, 30 - seq2_trans - seq3_trans)
        self.assertEqual(sequence.length, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10 - seq2_trans)
        self.assertEqual(sequence[1].transition_length, seq2_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans)
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[2].source.source_name, 'seq3')

class test_AddOverlapItemsToSequenceCommand(unittest.TestCase):
    def test_add_single_to_middle(self):
        self.add_single_to_middle(0)
        self.add_single_to_middle(5)
        self.add_single_to_middle(-5)
        self.add_single_to_middle(9)
        self.add_single_to_middle(10)
        self.add_single_to_middle(-9)
        self.add_single_to_middle(-5, 0)
        self.add_single_to_middle(-3, -3)

        with self.assertRaises(model.NoRoomError):
            self.add_single_to_middle(-3, 3)

    def add_single_to_middle(self, offset=0, seq3_trans=-10):
        '''Straight-up add an item to the middle of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10)])

        items = [model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10)]
        mover = model.SequenceOverlapItemsMover(items)

        command = model.AddOverlapItemsToSequenceCommand(sequence, mover, 20 + offset)
        command.redo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[1].x, 10 + offset)
        self.assertEqual(sequence[2].x, 10 - seq3_trans)
        self.assertEqual(sequence[1].transition_length, -offset)
        self.assertEqual(sequence[2].transition_length, 10 + offset + seq3_trans)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10 - seq3_trans)
        self.assertEqual(sequence[1].transition_length, seq3_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq3')

    def test_add_single_to_start(self):
        self.add_single_to_start(0)
        self.add_single_to_start(-1)
        self.add_single_to_start(-6)
        self.add_single_to_start(-10)
        self.add_single_to_start(-14)

        self.add_single_to_start(-3, 3)
        self.add_single_to_start(-4, 3)
        self.add_single_to_start(-10, 3)
        self.add_single_to_start(-14, 3)

        with self.assertRaises(model.NoRoomError):
            self.add_single_to_start(-2, 3)

    def add_single_to_start(self, offset=0, seq3_trans=0):
        '''Add an item to the start of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10)])

        items = [model.SequenceItem(source=model.StreamSourceRef('seq1', 1), offset=1, length=10)]
        mover = model.SequenceOverlapItemsMover(items)

        command = model.AddOverlapItemsToSequenceCommand(sequence, mover, 10 + offset)
        command.redo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10 + offset)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[1].x, 0 - offset)
        self.assertEqual(sequence[2].x, 10 - offset - seq3_trans)
        self.assertEqual(sequence[1].transition_length, 10 + offset)
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq2')
        self.assertEqual(sequence[1].x, 10 - seq3_trans)
        self.assertEqual(sequence[1].transition_length, seq3_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq3')

    def test_add_single_to_end(self):
        self.add_single_to_end(0)
        self.add_single_to_end(10)
        self.add_single_to_end(-5)
        self.add_single_to_end(-9)
        self.add_single_to_end(5)
        self.add_single_to_end(0, 5)
        self.add_single_to_end(-5, 5)
        self.add_single_to_end(-10, 5)

        with self.assertRaises(model.NoRoomError):
            # At this point, we run into seq1
            self.add_single_to_end(-11, 5)

    def add_single_to_end(self, offset=0, seq2_trans=0):
        '''Add an item to the end of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=1, length=10)])

        items = [model.SequenceItem(source=model.StreamSourceRef('seq3', 1), offset=1, length=10)]
        mover = model.SequenceOverlapItemsMover(items)

        command = model.AddOverlapItemsToSequenceCommand(sequence, mover, 30 + offset)
        command.redo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence.length, max(20 - seq2_trans, 30 + offset))
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[1].x, 10 - seq2_trans)
        self.assertEqual(sequence[2].x, 20 + offset)
        self.assertEqual(sequence[1].transition_length, seq2_trans)
        self.assertEqual(sequence[2].transition_length, 0 - seq2_trans - offset)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].source.source_name, 'seq2')
        self.assertEqual(sequence[2].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10 - seq2_trans)
        self.assertEqual(sequence[1].transition_length, seq2_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq2')

    def test_add_double_to_middle(self):
        self.add_double_to_middle(0)
        self.add_double_to_middle(-5)

        with self.assertRaises(model.NoRoomError):
            # At this point, our transition runs into seq1
            self.add_double_to_middle(-6)

        with self.assertRaises(model.NoRoomError):
            # At this point, our transition runs into seq3
            self.add_double_to_middle(1)

    def add_double_to_middle(self, offset=0, seq3_trans=-10):
        '''Add overlapping item to the middle of a sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10)])

        seq2a_length = 10
        seq2b_trans = 5

        items = [model.SequenceItem(source=model.StreamSourceRef('seq2a', 0), offset=1, length=seq2a_length),
            model.SequenceItem(source=model.StreamSourceRef('seq2b', 0), transition_length=seq2b_trans, offset=1, length=10)]
        mover = model.SequenceOverlapItemsMover(items)

        command = model.AddOverlapItemsToSequenceCommand(sequence, mover, 20 + offset)
        command.redo()

        self.assertEqual(len(sequence), 4)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[1].transition_length, -offset)
        self.assertEqual(sequence[1].x, 10 + offset)
        self.assertEqual(sequence[2].transition_length, seq2b_trans)
        self.assertEqual(sequence[2].x, 10 + offset + (seq2a_length - seq2b_trans))
        self.assertEqual(sequence[3].transition_length, 15 + offset + seq3_trans)
        self.assertEqual(sequence[3].x, 10 - seq3_trans)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].source.source_name, 'seq2a')
        self.assertEqual(sequence[2].source.source_name, 'seq2b')
        self.assertEqual(sequence[3].source.source_name, 'seq3')

        command.undo()

        self.assertEqual(len(sequence), 2)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[0].x, 0)
        self.assertEqual(sequence[0].transition_length, 0)
        self.assertEqual(sequence[0].source.source_name, 'seq1')
        self.assertEqual(sequence[1].x, 10 - seq3_trans)
        self.assertEqual(sequence[1].transition_length, seq3_trans)
        self.assertEqual(sequence[1].source.source_name, 'seq3')

class test_MoveSequenceOverlapItemsInPlaceCommand(unittest.TestCase):
    def test_move_single_at_middle(self):
        self.move_single_at_middle(0)
        self.move_single_at_middle(-5)
        self.move_single_at_middle(-5, seq2_trans=5, seq5_trans=5)
        self.move_single_at_middle(-10)

        with self.assertRaises(model.NoRoomError):
            # At this point, we would run past the beginning of seq2
            self.move_single_at_middle(-11)

        with self.assertRaises(model.NoRoomError):
            # At this point, we would into the transition from seq1
            self.move_single_at_middle(-10, seq2_trans=1)

        self.move_single_at_middle(5)
        self.move_single_at_middle(5, seq2_trans=5, seq5_trans=5)
        self.move_single_at_middle(10)

        with self.assertRaises(model.NoRoomError):
            # At this point, we would run past the end of seq4
            self.move_single_at_middle(11)

        with self.assertRaises(model.NoRoomError):
            # At this point, we would into the transition into seq5
            self.move_single_at_middle(10, seq5_trans=1)

    def move_single_at_middle(self, offset, seq2_trans=0, seq5_trans=0):
        '''Move a single item at the middle of the sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq4', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq5', 0), transition_length=seq5_trans, offset=1, length=10)
            ])

        items = [sequence[2]]
        mover = model.SequenceOverlapItemsMover(items)

        try:
            command = model.MoveSequenceOverlapItemsInPlaceCommand(mover, offset)
            command.redo()

            self.assertEqual(len(sequence), 5)
            self.assertEqual(sequence.x, 10)
            self.assertEqual(sequence[1].transition_length, seq2_trans)
            self.assertEqual(sequence[1].x, 10 - seq2_trans)
            self.assertEqual(sequence[2].transition_length, -offset)
            self.assertEqual(sequence[2].x, 20 + offset - seq2_trans)
            self.assertEqual(sequence[3].transition_length, offset)
            self.assertEqual(sequence[3].x, 30 - seq2_trans)
            self.assertEqual(sequence[4].transition_length, seq5_trans)
            self.assertEqual(sequence[4].x, 40 - seq2_trans - seq5_trans)

            command.undo()
        finally:
            self.assertEqual(len(sequence), 5)
            self.assertEqual(sequence.x, 10)
            self.assertEqual(sequence[1].transition_length, seq2_trans)
            self.assertEqual(sequence[1].x, 10 - seq2_trans)
            self.assertEqual(sequence[2].transition_length, 0)
            self.assertEqual(sequence[2].x, 20 - seq2_trans)
            self.assertEqual(sequence[3].transition_length, 0)
            self.assertEqual(sequence[3].x, 30 - seq2_trans)
            self.assertEqual(sequence[4].transition_length, seq5_trans)
            self.assertEqual(sequence[4].x, 40 - seq2_trans - seq5_trans)

    def test_move_single_at_start(self):
        self.move_single_at_start(0)
        self.move_single_at_start(5)
        self.move_single_at_start(10)

        with self.assertRaises(model.NoRoomError):
            # This moves us past the end of seq2
            self.move_single_at_start(11)

        self.move_single_at_start(-5)
        self.move_single_at_start(-10)
        self.move_single_at_start(-15)

        self.move_single_at_start(5, seq3_trans=5)

        with self.assertRaises(model.NoRoomError):
            # This bumps us into the transition to seq3
            self.move_single_at_start(6, seq3_trans=5)

    def move_single_at_start(self, offset, seq2_trans=0, seq3_trans=0):
        '''Move a single item at the start of the sequence.'''
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10),
            ])

        items = [sequence[0]]
        mover = model.SequenceOverlapItemsMover(items)

        try:
            command = model.MoveSequenceOverlapItemsInPlaceCommand(mover, offset)
            command.redo()

            self.assertEqual(len(sequence), 3)
            self.assertEqual(sequence.x, 10 + offset)
            self.assertEqual(sequence[1].transition_length, seq2_trans + offset)
            self.assertEqual(sequence[1].x, 10 - seq2_trans - offset)
            self.assertEqual(sequence[2].transition_length, seq3_trans)
            self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans - offset)

            command.undo()
        finally:
            self.assertEqual(len(sequence), 3)
            self.assertEqual(sequence.x, 10)
            self.assertEqual(sequence[1].transition_length, seq2_trans)
            self.assertEqual(sequence[1].x, 10 - seq2_trans)
            self.assertEqual(sequence[2].transition_length, seq3_trans)
            self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans)

    def test_combine_commands(self):
        '''Test combining two move commands.'''
        seq2_trans, seq3_trans = 0, 0

        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10),
            ])

        items = [sequence[0]]
        mover = model.SequenceOverlapItemsMover(items)

        command1 = model.MoveSequenceOverlapItemsInPlaceCommand(mover, 1)
        command1.redo()

        offset = 1

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10 + offset)
        self.assertEqual(sequence[1].transition_length, seq2_trans + offset)
        self.assertEqual(sequence[1].x, 10 - seq2_trans - offset)
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans - offset)

        command2 = model.MoveSequenceOverlapItemsInPlaceCommand(mover, -2)
        command2.redo()

        offset = -1

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10 + offset)
        self.assertEqual(sequence[1].transition_length, seq2_trans + offset)
        self.assertEqual(sequence[1].x, 10 - seq2_trans - offset)
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans - offset)

        command1.mergeWith(command2)
        command1.undo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10)
        self.assertEqual(sequence[1].transition_length, seq2_trans)
        self.assertEqual(sequence[1].x, 10 - seq2_trans)
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans)

        command1.redo()

        self.assertEqual(len(sequence), 3)
        self.assertEqual(sequence.x, 10 + offset)
        self.assertEqual(sequence[1].transition_length, seq2_trans + offset)
        self.assertEqual(sequence[1].x, 10 - seq2_trans - offset)
        self.assertEqual(sequence[2].transition_length, seq3_trans)
        self.assertEqual(sequence[2].x, 20 - seq2_trans - seq3_trans - offset)

class test_SequenceItemsMover(unittest.TestCase):
    def test_to_item1(self):
        self.to_item1(0, 0)
        self.to_item1(-5, -6)
        self.to_item1(5, -6)
        self.to_item1(5, 6)

    def to_item1(self, seq2_trans=0, seq3_trans=0):
        items = [
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=6),
            model.SequenceItem(source=model.StreamSourceRef('seq2', 0), transition_length=seq2_trans, offset=2, length=19),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=5, length=10)
        ]

        mover = model.SequenceItemsMover(items)

        seq = mover.to_item(height=4.5)
        self.assertEqual(seq.height, 4.5)
        self.assertEqual(len(seq), 3)
        self.assertEqual(seq[0].source.source_name, 'seq1')
        self.assertEqual(seq[1].source.source_name, 'seq2')
        self.assertEqual(seq[2].source.source_name, 'seq3')
        self.assertEqual(seq[0].offset, 1)
        self.assertEqual(seq[1].offset, 2)
        self.assertEqual(seq[2].offset, 5)
        self.assertEqual(seq[0].transition_length, 0)
        self.assertEqual(seq[1].transition_length, seq2_trans)
        self.assertEqual(seq[2].transition_length, seq3_trans)
        self.assertEqual(seq[0].length, 6)
        self.assertEqual(seq[1].length, 19)
        self.assertEqual(seq[2].length, 10)

class test_MoveSequenceItemsInPlaceCommand(unittest.TestCase):
    def test_slide_two_around(self):
        self.slide_two_around([0, -1, -2, -6, 15])
        self.slide_two_around([0, -1, -2, -6, 15, 30], die_on=5)

    def slide_two_around(self, offsets=[], seq2b_trans=0, seq3_trans=0, die_on=None):
        sequence = model.Sequence(x=10, y=10.0, items=[
            model.SequenceItem(source=model.StreamSourceRef('seq1', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2a', 0), offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq2b', 0), transition_length=seq2b_trans, offset=1, length=10),
            model.SequenceItem(source=model.StreamSourceRef('seq3', 0), transition_length=seq3_trans, offset=1, length=10),
            ])

        mover = model.SequenceItemsMover([sequence[1], sequence[2]])
        current_offset = 0

        for i, offset in enumerate(offsets):
            try:
                command = model.MoveSequenceItemsInPlaceCommand(mover, offset)

                if die_on == i:
                    with self.assertRaises(model.NoRoomError):
                        command.redo()
                else:
                    command.redo()
                    current_offset += offset
            finally:
                self.assertEqual(len(sequence), 4)
                self.assertEqual(sequence[0].source.source_name, 'seq1')
                self.assertEqual(sequence[1].source.source_name, 'seq2a')
                self.assertEqual(sequence[2].source.source_name, 'seq2b')
                self.assertEqual(sequence[3].source.source_name, 'seq3')
                self.assertEqual(sequence[0].x, 0)
                self.assertEqual(sequence[1].x, 10 + current_offset)
                self.assertEqual(sequence[2].x, 20 - seq2b_trans + current_offset)
                self.assertEqual(sequence[3].x, 30 - seq2b_trans - seq3_trans)


