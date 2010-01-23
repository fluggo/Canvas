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

(REGION_BAD

Clip = namedtuple('Clip', 'source offset length')
Transition = namedtuple('Transition', 'cut_point length')

class Take(object):
    '''
    tags = []
    tracks = []
    '''
    pass

class TakeTrack(object):
    '''
    sync_offset = n (difference between take time and source time)
    start_offset = n (frame in source at which take starts)
    length = n
    source = Source()

    '''
    pass

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
    clips = []
    transitions = []
    clipStarts = []

    markers = []
    refs = []

    The clips list is one longer than the transitions list. Every entry in the
    clip list must refer to a clip. Each entry in the transition list represents
    the transition that follows the corresponding clip; a None entry is a cut.

    The length of the timeline is the total length of the clips less the total
    length of the transitions.

    For all i:

        clips[i].length >= (transitions[i-1].length + transitions[i].length)

    transition
        length
        cut_point

    clip
        source
        offset
        length

    '''

    def set_clip_length(self, index, length):
        pass

    def set_transition_length(self, index, length):
        pass

    def set_clip(self, index, clip):
        pass

    def set_transition(self, index, transition):
        pass

    def insert_edit(self, source, source_point, target_ref, length, transition = None, transition_length):
        raise NotImplementedError

    def overwrite_edit(self, source, source_point, start_ref, length):
        raise NotImplementedError

    def scissor(self, ref):
        '''
        Split a clip into two continuous parts at the reference.

        If a split already exists at the reference, nothing happens.

        TODO: What happens if the ref is in a transition?
        '''
        raise NotImplementedError

    def join_clips(self, clip_index):
        '''
        Join the clip at clip_index with the next clip.
        '''
        raise NotImplementedError

    def setClipLength(self, index, length):
        if length < 1:
            raise ValueError('length cannot be less than 1')

        oldLength = self.clips[index]._length

        for clip in self.clips[index + 1:]:
            clip._start += length - oldLength

        self.clips[index]._length = length

    def setClipOffset(self, index, offset):
        self.clips[index]._offset = offset

    def setClipSource(self, index, source):
        self.clips[index]._source = source

    def __len__(self):
        return len(self.clips)

    def __getitem__(self, key):
        return self.clips[key]

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
        if ((start == 0 or isinstance(self.clips[start - 1], Transition))
            and enterTrans):
            raise ValueError('Cannot place two transitions next to each other.')

        # End of value and insert point in narrative
        if ((end == len(self.clips) or isinstance(self.clips[end], Transition))
            and exitTrans):
            raise ValueError('Cannot place two transitions next to each other.')

        # Middle of given sequence
        if seq:
            for i in range(len(value) - 1):
                if isinstance(value[i], Transition) and isinstance(value[i + 1], Transition):
                    raise ValueError('Cannot place two transitions next to each other.')

        self.clips[key] = value
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


