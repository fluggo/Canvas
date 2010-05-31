import weakref

class Signal(object):
    '''
    Simple signal implementation by Thiago Marcos P. Santos (thanks!).
    '''
    def __init__(self):
        self.__slots = weakref.WeakValueDictionary()

    def __call__(self, *args, **kargs):
        for key in self.__slots:
            func, _ = key
            func(self.__slots[key], *args, **kargs)

    def connect(self, slot):
        key = (slot.im_func, id(slot.im_self))
        self.__slots[key] = slot.im_self

    def disconnect(self, slot):
        key = (slot.im_func, id(slot.im_self))
        if key in self.__slots:
            self.__slots.pop(key)

    def clear(self):
        self.__slots.clear()

