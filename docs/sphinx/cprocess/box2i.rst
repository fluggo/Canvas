.. highlight:: c

box2i
=====

:c:type:`box2i` and :c:type:`box2f` are the media library's rectangle types. ``box2i`` describes a rectangle of pixels in terms of their minimum and maximum coordinates.

Since it stores the minimum and maximum coordinates, the size of the box is ``(max - min) + v2i(1, 1)``, so that a box with both corners at ``v2i(0, 0)`` will have a width and height of one. This is subtly different from ``box2f``, where the same box would have a width and height of zero. You might think of the pixel plane as a grid of dots, in which case the ``box2i`` with both corners at ``v2i(0, 0)`` will enclose the pixel at (0, 0) and extend half the distance to the next pixel in each direction. (That is, ``box2i(v2i(0, 0), v2i(0, 0))`` is conceptually the same as ``box2f(v2f(-0.5, -0.5), v2i(0.5, 0.5))``.)

A ``box2i`` could also be empty, which means that it doesn't contain any pixels. For this, the max coordinate will be less than the min coordinate in either axis. The :c:func:`box2i_get_size` function (in C) and the :py:func:`box2i.size()` method (in Python) take this into account and return zero for those axes.

box2i in C
----------

Include: framework.h

::

    typedef struct {
        v2i min, max;
    } box2i;

.. c:function:: static inline void box2i_set(box2i *box, int minX, int minY, int maxX, int maxY)

    Set the *box*'s corners to the specified minimum and maximum coordinates.

.. c:function:: static inline void box2i_set_empty(box2i *box)

    Set the given *box* to empty.

.. c:function:: static inline bool box2i_is_empty(const box2i *box)

    Determine if the given *box* is empty (one of the axes' min coordinates is greater than its max) and return true if it is.

.. c:function:: static inline void box2i_intersect(box2i *result, const box2i *first, const box2i *second)

    Intersect *first* with *second* and give the result in *result*.

    If *first* or *second* is empty, the result will be empty.

.. c:function:: static inline void box2i_union(box2i *result, const box2i *first, const box2i *second)

    Get the union of *first* and *second* and store the result in *result*.

    **Unlike** :c:func:`box2i_intersect`, if either of the boxes is empty, the result is undefined.

.. c:function:: static inline void box2i_normalize(box2i *result)

    Normalize a box so that if either of its axes are backwards (resulting in an empty box), they are flipped so that the box is non-empty.

.. c:function:: static inline void box2i_get_size(const box2i *box, v2i *result)

    Get the size of the *box* and store it in *result*. If the box is empty, one or both of the axes will have a size of zero.

box2i in Python
---------------

The :py:class:`box2i` class in :py:mod:`fluggo.media.basetypes` works the same way the C version does, except that it is read-only (a tuple).

.. py:class:: box2i(min_x, min_y, max_x, max_y)

    Create a ``box2i``. There are several ways to supply the parameters: you can give four coordinates, two :py:class:`v2i` values for the corners, or another box.

    .. code-block:: python

        from fluggo.media.basetypes import box2i, v2i

        # All of these forms will work
        box = box2i(v2i(10, 20), v2i(30, 40))
        box = box2i(10, 20, 30, 40)
        box = box2i(box)

    .. py:attribute:: box2i.min

        A :py:class:`v2i` value with the minimum coordinates of the box. Read-only.

    .. py:attribute:: box2i.max

        A :py:class:`v2i` value with the maximum coordinates of the box. Read-only.

    .. py:attribute: box2i.width

        The width of the box, which may be zero. Read-only.

    .. py:attribute: box2i.height

        The height of the box, which may be zero. Read-only.

    .. py:method:: box2i.size()

        Return a :py:class:`v2i` value with the size of the box.

    .. py:method:: box2i.empty()

        Return ``True`` if the box is empty.

    .. py:method:: box2i.__nonzero__()
        box2i.__bool__()

        Return ``True`` if the box is not empty (``if box``).

Converting between C and Python
-------------------------------

Include: pyframework.h

.. c:function:: PyObject *py_make_box2i(box2i *box)
    bool py_parse_box2i(PyObject *obj, box2i *box)

    These functions convert between the Python :py:class:`box2i` in :py:mod:`basetypes` and the C :c:type:`box2i`.

    :c:func:`py_parse_box2i` sets an exception and returns false if it failed to convert the given *obj* to a box.

