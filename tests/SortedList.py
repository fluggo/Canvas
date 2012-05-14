import unittest
import itertools
from fluggo.sortlist import SortedList

class TestSortedList(unittest.TestCase):
    def testinit(self):
        l = SortedList([5,9,2,3,6])

        for j, k in itertools.zip_longest(l, [2,3,5,6,9]):
            self.assertEquals(j, k)

        l = SortedList([5,9,2,3,6], keyfunc=lambda j: -j)

        for j, k in itertools.zip_longest(l, [9,6,5,3,2]):
            self.assertEquals(j, k)

    def testadd(self):
        l = SortedList([2,3,6,9])

        l.add(5)

        for j, k in itertools.zip_longest(l, [2,3,5,6,9]):
            self.assertEquals(j, k)

        l = SortedList([9,6,3,2], keyfunc=lambda j: -j)

        l.add(5)

        for j, k in itertools.zip_longest(l, [9,6,5,3,2]):
            self.assertEquals(j, k)

        l = SortedList([9,6,3,2], keyfunc=lambda a: -a)

        l.add(5)

        for j, k in itertools.zip_longest(l, [9,6,5,3,2]):
            self.assertEquals(j, k)

    def testfind(self):
        l = SortedList([9,7,1,3,6,2])

        for j, k in itertools.zip_longest(l.find(), [1,2,3,6,7,9]):
            self.assertEquals(j, k)

        for j, k in itertools.zip_longest(l.find(min_key=3), [3,6,7,9]):
            self.assertEquals(j, k)

        for j, k in itertools.zip_longest(l.find(max_key=6), [1,2,3,6]):
            self.assertEquals(j, k)

        for j, k in itertools.zip_longest(l.find(min_key=3, max_key=6), [3,6]):
            self.assertEquals(j, k)

    def testindexes(self):
        class Item(object):
            def __init__(self, value):
                self._index = None
                self.value = value

            @property
            def indx(self):
                return self._index

            @indx.setter
            def indx(self, value):
                self._index = value

        def check(l):
            for i in range(len(l)):
                self.assertEquals(l[i].indx, i)

        l = SortedList([Item(v) for v in [9,7,1,3,6,2]], keyfunc=lambda a: a.value, index_attr='indx')
        check(l)

        l.add(Item(5))
        check(l)

        del l[3]
        check(l)

        l[2].value = 16
        l.move(2)
        check(l)

        l[4].value = -1
        l.move(4)
        check(l)

        l[0].value = 4
        l.move(0)
        check(l)

