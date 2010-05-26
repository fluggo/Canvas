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
import process, sources

__clipclass = collections.namedtuple('Clip', 'source offset length transition transition_cut_point transition_length')

def Clip(source, offset, length, transition=None, transition_cut_point=None, transition_length=0):
    return __clipclass(source, offset, length, transition, transition_cut_point, transition_length)

def check_clip(clip):
    if clip is None:
        raise ValueError('Clip cannot be None.')

    if clip.source is None:
        raise ValueError('Clip must have a source.')

    if not clip.transition and clip.transition_length != 0:
        raise ValueError('Clip without a transition must have a transition_length of zero.')

def check_clip_pair(clip1, clip2):
    if not clip2:
        # Just verify that our transition is shorter than ourselves
        if clip1.transition and clip1.transition_length > clip1.length:
            raise ValueError('The last clip had a transition longer than the clip.')
    elif not clip1:
        if clip2.transition:
            raise ValueError('The timeline must not start with a transition.')
    else:
        if clip1.transition_length + clip2.transition_length > clip1.length:
            raise ValueError('A clip was shorter than the surrounding transitions.')

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
    source = SourceRef()

    '''
    pass

class Timeline(object):
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
    clipStarts = []

    markers = []
    refs = []

    Each entry in the clip list must refer to a clip and may refer to a transition,
    except the first clip, which must not have a transition. The transition is the
    transition that precedes the given clip; a None transition is a cut.

    The length of the timeline is the total length of the clips less the total
    length of the transitions.

    For all i:

        clips[i].length >= (clips[i].transition_length + clips[i+1].transition_length)

    '''
    def __init__(self):
        self.clips = []

    def set_clip_length(self, index, length):
        raise NotImplementedError

    def set_transition_length(self, index, length):
        raise NotImplementedError

    def set_clip_source(self, index, clip):
        raise NotImplementedError

    def set_transition(self, index, transition):
        raise NotImplementedError

    def insert_edit(self, source, source_point, target_ref, length, transition = None, transition_length = None):
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

    def __len__(self):
        return len(self.clips)

    def __getitem__(self, key):
        return self.clips[key]

    def _replace_clip_range(self, start, stop, clips):
        # Verify the clips pair-wise
        for clip1, clip2 in zip(clips[0:-1], clips[1:]):
            check_clip_pair(clip1, clip2)

        # Verify the ends
        if len(clips):
            if start > 0:
                check_clip_pair(self.clips[start - 1], clips[0])
            else:
                check_clip_pair(None, clips[0])

            if stop < len(self.clips):
                check_clip_pair(clips[-1], self.clips[stop])
            else:
                check_clip_pair(clips[-1], None)
        else:
            # Removal only
            if start > 0:
                if stop < len(self.clips):
                    check_clip_pair(self.clips[start - 1], self.clips[stop])
                else:
                    check_clip_pair(self.clips[start - 1], None)
            elif stop < len(self.clips):
                check_clip_pair(None, self.clips[stop])

        self.clips[start:stop] = clips

    def __setitem__(self, key, value):
        start, stop, step = None, None, None
        is_slice = isinstance(key, slice)
        clips = None

        if is_slice:
            start, stop, step = key.indices(len(self.clips))
            clips = value
        else:
            start, stop, step = key, key, 1
            clips = [value]

        if step == 1:
            self._replace_clip_range(start, stop, clips)
        else:
            # Reduce it to solid ranges
            i = 0

            for j in range(start, stop, step):
                if i < len(clips):
                    self._replace_clip_range(j, j + 1, [clips[i]])
                else:
                    self._replace_clip_range(j, j + 1, [])

    def __delitem__(self, key):
        # Just a special case of __setitem__ above
        if isinstance(key, slice):
            self[key] = []
        else:
            self[key:key + 1] = []

class AttachedTimeline(Timeline):
    '''
    anchor = event
    anchorOffset = n frames
    '''
    pass

class AudioTimeline(Timeline, sources.AudioSource):
    pass

class VideoTimeline(Timeline, sources.VideoSource):
    def __init__(self, format):
        Timeline.__init__(self)
        sources.VideoSource.__init__(self, format)
        self.seq = None

    def create_underlying_source(self):
        if self.seq:
            return self.seq

        self.seq = process.VideoSequence()

        # Walk through the timeline and set it up
        for clip1, clip2 in zip(self.clips[0:-1], self.clips[1:]):
            self.seq.append((clip1.source, clip1.offset, clip1.length - clip1.transition_length - clip2.transition_length))

            if clip2.transition:
                self.seq.append((clip2.transition.create_source(clip1.source, clip2.source, clip2.transition_length)))

        self.seq.append((clip1.source, clip1.offset, clip1.length - clip1.transition_length))

        for elem in self.seq:
            print elem

        return self.seq

    def _replace_clip_range(self, start, stop, clips):
        Timeline._replace_clip_range(self, start, stop, clips)






