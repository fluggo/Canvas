# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2009-10 Brian J. Crowell <brian@fluggo.com>
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

class Timecode(object):
    '''
    Interface for timecode objects.

    Yeah, this is just a convenient place to put the documentation.

    '''
    def format(self, frame):
        '''
        Return the given frame in timecode format.

        frame - Frame number, which can be negative.
        '''
        raise NotImplementedError

    def parse(self, timecode):
        '''
        Parse the given timecode and return which frame it is.

        timecode - Formatted timecode to parse.
        '''
        raise NotImplementedError

    def get_major_ticks(self):
        '''
        Return an array of frame counts, from small to large, which separate significant
        frames.

        At 30 frames per second, for example, you might see:

            [30, 30*60, 30*60*60]

        ...for the number of frames between seconds, minutes, and hours.

        '''
        return []

class Frames(Timecode):
    '''
    The simplest timecode, which just yields the frame number.

    '''
    def format(self, frame):
        '''
        Return the given frame in timecode format.

        frame - Frame number, which can be negative.
        '''
        return str(frame)

    def parse(self, timecode):
        return int(timecode)

class TimeAndFrames(Timecode):
    '''
    A timecode with an integer number of frames each second.

    TimeFramesTimecode(frames_per_second, frames_separator=':')

    frames_per_second - The integer number of frames to assign
        to each second.
    frames_separator - The separator to use between seconds and frames.

    The timecode is displayed as 'hh:mm:ss:ff'.

    TimeFramesTimecode is perfect for integer frame rates.
    It's also useful for non-drop NTSC, where frames_per_second would be 30.

    Note that if the frame rate and frames_per_second are different,
    this kind of timecode will drift away from wall-clock over time.

    '''
    def __init__(self, frames_per_second, frames_separator=':'):
        self.frames_per_second = frames_per_second
        self.frames_separator = frames_separator

    def format(self, frame):
        (rem, frames) = divmod(abs(frame), self.frames_per_second)
        (rem, seconds) = divmod(rem, 60)
        (hours, minutes) = divmod(rem, 60)

        result = '{0:02}:{1:02}:{2:02}{4}{3:02}'.format(hours, minutes, seconds, frames, self.frames_separator)

        if frame < 0:
            return '-' + result
        else:
            return result

    def parse(self, timecode):
        if len(timecode) == 0:
            return 0

        negative = (timecode[0] == '-')
        timecode = timecode.lstrip('-').replace(self.frames_separator, ':')

        fields = reversed(timecode.split(':'))
        mult = 1
        result = 0

        for sig, value in zip([1, self.frames_per_second, 60, 60], fields):
            mult *= sig
            result += mult * int(value)

        if negative:
            result *= -1

        return result

    def get_major_ticks(self):
        return [self.frames_per_second, self.frames_per_second*60, self.frames_per_second*60*60]

class NtscDropFrame(TimeAndFrames):
    '''
    NTSC drop-frame timecode.

    NtscDropFrame(frames_separator=';')

    frames_separator - The separator to use between seconds and frames.
        The default (';') is common; another typical separator is '.'.

    The timecode is displayed as 'hh:mm:ss;ff', where ff has the range 0-29.

    Drop-frame timecode skips two frames at the beginning of every minute,
    except for minutes divisible by ten (so '00:00:59:29' is followed by
    '00:01:00:02', but '00:09:59:29' is followed by '00:10:00:00'.)

    Clever people invented drop-frame timecode to compensate for the drift
    that happens when a 30 fps timecode is used with a 30/1.001 fps frame rate.
    Drop-frame timecode more-or-less keeps pace with the wall clock
    when used with 30/1.001 fps material over a period of less than a day.
    '''
    def __init__(self, frames_separator=';'):
        TimeAndFrames.__init__(self, 30, frames_separator=frames_separator)

    def format(self, frame):
        return TimeAndFrames.format(self, frame + 2 * (frame // (30*60) - frame // (10*30*60)))

    def parse(self, timecode):
        frame = TimeAndFrames.parse(self, timecode)

        return frame - 2 * (frame // (30*60 + 2) - frame // (10*30*60 + 2))

