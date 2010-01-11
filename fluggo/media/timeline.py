# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009 Brian J. Crowell <brian@fluggo.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import collections

class Take(object):
    '''
    tags = []
    tracks = []
    '''
    pass

class TakeTrack(object):
    '''
    offset = n
    source = Source()
    '''
    pass

class Clip(object):
    '''
    source = Source()
    length = n frames
    offset = n frames

    A slug is a clip with no source.
    '''

    def __init__(self, timeline, index):
        self._timeline = timeline
        self._source = None
        self._length = 1
        self._offset = 0

        # Internal -- index is this clip's index in the narrative
        # Start is the frame at which this clip starts
        self._index = index
        self._start = 0

    @property
    def length(self):
        return self._length

    @length.setter
    def setLength(self, value):
        self._timeline.setClipLength(self._index, value)

    @property
    def source(self):
        return self._source

    @source.setter
    def setSource(self, value):
        self._timeline.setClipSource(self._index, value)

    @property
    def offset(self):
        return self._offset

    @offset.setter
    def setOffset(self, value):
        self._timeline.setClipOffset(self._index, value)

class Transition(object):
    '''
    transitionName = '...'
    function = '...' (linear, fast, slow, custom)
    cutPoint = n frames
    length = n frames
    '''
    def __init__(self, length=1, name='crossfade', cutPoint=0):
        if length < 1:
            raise ValueError('length cannot be less than 1')

        if cutPoint < 0 or cutPoint > length:
            raise ValueError('cutPoint must be between zero and length inclusive')

        self._timeline = None
        self._name = name
        self._cutPoint = cutPoint

        # A transition's length is negative
        # such that adding all event lengths together
        # yields the length of the timeline
        self._length = -length

        # Internal -- index is this clip's index in the narrative
        # Start is the frame at which this transition starts
        self._index = 0
        self._start = 0

    @property
    def length(self):
        return -self._length

    @length.setter
    def setLength(self, value)
        if value < 1:
            raise ValueError('length cannot be less than 1')

        if self._timeline:
            self._timeline.setClipLength(self._index, value)
        else
            self._length = -value

class Timeline(collections.MutableSequence):
    class _Ref(object):
        def __init__(self, frame):
            self.frame = frame

        def getRef(self):
            return self

    class _Marker(object):
        def getRef(self):
            return Ref(frame)

    '''
    narrative = []
    clipStarts = []

    markers = []
    refs = []
    '''
    def setClipLength(self, index, length):
        if length < 1:
            raise ValueError('length cannot be less than 1')

        oldLength = self.narrative[index]._length

        for clip in self.narrative[index + 1:]:
            clip._start += length - oldLength

        self.narrative[index]._length = length

    def setClipOffset(self, index, offset):
        self.narrative[index]._offset = offset

    def setClipSource(self, index, source):
        self.narrative[index]._source = source

    def __len__(self):
        return len(narrative)

    def __getitem__(self, key):
        return narrative[key]

    def __setitem__(self, key, value):
        start, stop, step = None, None, None
        seq = isinstance(value, collections.Sequence):

        if seq and len(value) == 0:
            return self.__delitem__(key)

        if isinstance(key, slice):
            start, stop, step = key.indices(len(self.narrative))
        else:
            start, stop, step = key, key, 1

        if step != 1:
            raise NotImplementedError('A step other than one is not implemented.')

        if start > stop:
            start, stop = stop, start

            if seq:
                value = list(reversed(value))

        # What we can't do is let two transitions end up
        # next to each other (or at the beginning or end)
        enterTrans, exitTrans = None, None

        if seq:
            enterTrans = isinstance(value[0], Transition)
            exitTrans = isinstance(value[len(value) - 1], Transition)
        else:
            enterTrans = isinstance(value, Transition)
            exitTrans = enterTrans

        # Start of value and insert point in narrative
        if ((start == 0 or isinstance(self.narrative[start - 1], Transition))
            and enterTrans):
            raise ValueError('Cannot place two transitions next to each other.')

        # End of value and insert point in narrative
        if ((end == len(self.narrative) or isinstance(self.narrative[end], Transition))
            and exitTrans):
            raise ValueError('Cannot place two transitions next to each other.')

        # Middle of given sequence
        if seq:
            for i in range(len(value) - 1):
                if isinstance(value[i], Transition) and isinstance(value[i + 1], Transition):
                    raise ValueError('Cannot place two transitions next to each other.')

        self.narrative[key] = value
        self._recalc(start)

    def __delitem__(self, key):
        # Just a special case of __setitem__ above
        start, stop, step = None, None, None

        if isinstance(key, slice):
            start, stop, step = key.indices(len(self.narrative))
        else:
            start, stop, step = key, key, 1

        if step != 1:
            raise NotImplementedError('A step other than one is not implemented.')

        if start > stop:
            start, stop = stop, start

        if (start != 0 and isinstance(self.narrative[start - 1], Transition)
            and end != len(self.narrative) and isinstance(self.narrative[end], Transition)):
            raise ValueError('Cannot place two transitions next to each other.')

        del self.narrative[key]
        self._recalc(start)

    def _recalc(self, startIndex):
        for i in range(startIndex, len(self.narrative)):
            if i == 0:
                self.narrative[0]._start = 0
            else:
                self.narrative[i]._start = self.narrative[i - 1]._start - self.narrative[i - 1]._length

            self.narrative[i]._index = i
            self.narrative[i]._timeline = self

class AttachedTimeline(Timeline):
    '''
    anchor = event
    anchorOffset = n frames
    '''
    pass

class AudioTimeline(Timeline, AudioSource):
    pass

class VideoTimeline(Timeline, VideoSource):
    pass


