# -*- coding: utf-8 -*-
# This file is part of the Fluggo Media Library for high-quality
# video and audio processing.
#
# Copyright 2010 Brian J. Crowell <brian@fluggo.com>
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

import fractions, yaml
from fluggo.media.basetypes import *
from fluggo.media import process

class KnownColorPrimaries:
    '''
    Known RGB primary sets, aliases, and their colors in xy-space.

    Each set is a three-tuple with the respective xy-coordinates for R, G, and B.
    '''
    AdobeRGB = (v2f(0.6400, 0.3300), v2f(0.2100, 0.7100), v2f(0.1500, 0.0600))
    AppleRGB = (v2f(0.6250, 0.3400), v2f(0.2800, 0.5950), v2f(0.1550, 0.0700))
    sRGB = (v2f(0.6400, 0.3300), v2f(0.3000, 0.6000), v2f(0.1500, 0.0600))
    Rec709 = sRGB

