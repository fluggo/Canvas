import unittest
import itertools
from fluggo.sortlist import SortedList

class TestSortedList(unittest.TestCase):
    def testinit(self):
        l = SortedList([5,9,2,3,6])

        for j, k in itertools.izip_longest(l, [2,3,5,6,9]):
            self.assertEquals(j, k)

        l = SortedList([5,9,2,3,6], keyfunc=lambda j: -j)

        for j, k in itertools.izip_longest(l, [9,6,5,3,2]):
            self.assertEquals(j, k)

    def testadd(self):
        l = SortedList([2,3,6,9])

        l.add(5)

        for j, k in itertools.izip_longest(l, [2,3,5,6,9]):
            self.assertEquals(j, k)

        l = SortedList([9,6,3,2], keyfunc=lambda j: -j)

        l.add(5)

        for j, k in itertools.izip_longest(l, [9,6,5,3,2]):
            self.assertEquals(j, k)

    def testfind(self):
        l = SortedList([9,7,1,3,6,2])

        for j, k in itertools.izip_longest(l.find(), [1,2,3,6,7,9]):
            self.assertEquals(j, k)

        for j, k in itertools.izip_longest(l.find(min_key=3), [3,6,7,9]):
            self.assertEquals(j, k)

        for j, k in itertools.izip_longest(l.find(max_key=6), [1,2,3,6]):
            self.assertEquals(j, k)

        for j, k in itertools.izip_longest(l.find(min_key=3, max_key=6), [3,6]):
            self.assertEquals(j, k)

