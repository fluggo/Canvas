
class DragDropSource(object):
    '''Represents a media source (from the local source list) in a drag-drop operation.'''

    def __init__(self, source_name):
        self.source_name = source_name

class Undoable(object):
    '''Represents an action that can be undone and redone.'''

    def text(self):
        '''Returns text that describes the action.'''
        pass

    def redo(self):
        '''Performs the action if it hasn't already been performed.'''
        pass

    def undo(self):
        '''Undoes the action if it hasn't already been undone.'''
        pass


