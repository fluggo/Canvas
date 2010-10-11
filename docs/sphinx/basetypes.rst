.. highlight:: c
.. currentmodule:: fluggo.media.basetypes

***************************************************************
:py:mod:`fluggo.media.basetypes` - Common types in C and Python
***************************************************************

Six types appear very often in the media library. They are documented here along
with the functions to transition them between their C structures and their Python
equivalents.

All of the C structures are defined in ``framework.h``; the functions to convert
them to Python and back are all defined in ``pyframework.h``.

:py:class:`v2i` and :py:class:`v2f` --- Vectors
===============================================

.. py:class:: v2i(x, y)

    A tuple representing an integer vector at (*x*, *y*). It can also be initialized
    with another tuple or :py:class:`v2i`.

    The :c:type:`v2i` structure is the C equivalent::

        typedef struct {
            int x, y;
        } v2i;

.. py:class:: v2f(x, y)

    A tuple representing a floating-point vector at (*x*, *y*). It can also be initialized
    with another tuple or :py:class:`v2f`.

    The :c:type:`v2f` structure is the C equivalent::

        typedef struct {
            float x, y;
        } v2f;

The following functions in ``pyframework.h`` perform conversions between Python and C:

.. c:function:: PyObject *py_make_v2i(v2i *v)
    PyObject *py_make_v2f(v2f *v)

    Convert the C :c:type:`v2i` and :c:type:`v2f` types to
    the equivalent Python :py:class:`v2i` and :py:class:`v2f`.

.. c:function:: bool py_parse_v2i(PyObject *obj, v2i *v)
    bool py_parse_v2f(PyObject *obj, v2f *v)

    Parse Python integer or float tuples and store their values in *v* as the
    equivalent :c:type:`v2i` and :c:type:`v2f` C types. If *obj* cannot be converted,
    these functions set an exception and return false.

:py:class:`box2i` and :py:class:`box2f` --- Boxes
=================================================

:c:type:`box2i` and :c:type:`box2f` are the media library's rectangle types.
:py:class:`box2i` describes a rectangle of pixels in terms of their minimum and
maximum coordinates. :py:class:`box2f` does the same, but allows subpixel positioning.

Since it stores the minimum and maximum coordinates, the size of a :py:class:`box2i`
is ``(max - min) + v2i(1, 1)``, so that a box with both corners at ``v2i(0, 0)``
will have a width and height of one. This is subtly different from ``box2f``,
where the same box would have a width and height of zero. You might think of the
pixel plane as a grid of dots, in which case the ``box2i`` with both corners at
``v2i(0, 0)`` will enclose the pixel at (0, 0) and extend half the distance to
the next pixel in each direction. (That is, ``box2i(v2i(0, 0), v2i(0, 0))`` is
conceptually like ``box2f(v2f(-0.5, -0.5), v2i(0.5, 0.5))``.)

A ``box2i`` or ``box2f`` could also be empty, which means that it doesn't contain
any pixels. For this, the max coordinate will be less than the min coordinate in
either axis. The :c:func:`box2i_get_size` function (in C) and the :py:func:`box2i.size()`
and :py:func:`box2f.size()` methods (in Python) take this into account and return
zero for those axes.

.. py:class:: box2i(min, max)

    A tuple containing two :py:class:`v2i` values. You can pass them as *min* and
    *max*, you can supply four coordinates as *min_x*, *min_y*, *max_x*, and *max_y*,
    or you can give another tuple:

    .. code-block:: python

        from fluggo.media.basetypes import box2i, v2i

        # All of these forms will work
        box = box2i(v2i(10, 20), v2i(30, 40))
        box = box2i(10, 20, 30, 40)
        box = box2i(box)

    The C equivalent in ``framework.h`` is::

        typedef struct {
            v2i min, max;
        } box2i;

    .. py:attribute:: box2i.min

        A :py:class:`v2i` value with the minimum coordinates of the box. Read-only.

    .. py:attribute:: box2i.max

        A :py:class:`v2i` value with the maximum coordinates of the box. Read-only.

    .. py:attribute:: box2i.width

        The width of the box, which may be zero. Read-only.

    .. py:attribute:: box2i.height

        The height of the box, which may be zero. Read-only.

    .. py:method:: box2i.size()

        Return a :py:class:`v2i` value with the size of the box.

    .. py:method:: box2i.empty()

        Return ``True`` if the box is empty.

    .. py:method:: box2i.__nonzero__()
        box2i.__bool__()

        Return ``True`` if the box is not empty (``if box``).

.. py:class:: box2f(min, max)

    The same concept as :py:class:`box2i`, except as a tuple of two :py:class:`v2f`
    values. The C equivalent is::

        typedef struct {
            v2f min, max;
        } box2f;

The following functions in ``pyframework.h`` perform conversions between Python and C:

.. c:function:: PyObject *py_make_box2i(box2i *box)
    PyObject *py_make_box2f(box2f *box)

    Convert the C :c:type:`box2i` and :c:type:`box2f` types to
    the equivalent Python :py:class:`box2i` and :py:class:`box2f`.

.. c:function:: bool py_parse_box2i(PyObject *obj, box2i *box)
    bool py_parse_box2f(PyObject *obj, box2f *box)

    Parse Python integer or float tuples and store their values in *box* as the
    equivalent :c:type:`box2i` and :c:type:`box2f` C types. If *obj* cannot be converted,
    these functions set an exception and return false.

Manipulating :c:type:`box2i` in C
---------------------------------

These functions in ``framework.h`` are useful for manipulating :c:type:`box2i` values in C:

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

:py:class:`rgba` --- Colors
===========================

.. py:class:: rgba(r=0.0, g=0.0, b=0.0, a=1.0)

    A tuple representing a floating-point color. *r*, *g*, and *b* can be any value
    (though usually only non-negative values are meaningful). *a* should be between 0.0
    and 1.0.

    .. note:: Colors in the media library are currently *not* premultiplied with
        respect to alpha (e.g. semi-transparent red would be ``rgba(1.0, 0.0, 0.0, 0.5)``).
        This may change in a future release.

    In Python, the precision of this type is the same as for the :py:class:`float`
    type. Using it with the media library may reduce the precision to 32-bit or
    16-bit floats.

    The :c:type:`rgba_f32` structure is the C equivalent::

        typedef struct {
            float r, g, b, a;
        } rgba_f32;

The following functions in ``pyframework.h`` perform conversions between Python and C:

.. c:function:: PyObject *py_make_rgba_f32(rgba_f32 *color)

    Convert the C :c:type:`rgba_f32` value *color* to
    the equivalent Python :py:class:`rgba`.

.. c:function:: bool py_parse_rgba_f32(PyObject *obj, rgba_f32 *color)

    Parse an :py:class:`rgba` value and store its value in *color*. Any tuple of
    four floats will do. If *obj* cannot be converted,
    this function sets an exception and returns false.

Other color types
-----------------

``framework.h`` defines two other color structures::

    typedef struct {
        uint8_t r, g, b, a;
    } rgba_u8;

    typedef struct {
        half r, g, b, a;
    } rgba_f16;

:c:type:`rgba_f16` represents the same *kind* of data as :c:type:`rgba_f32` and
:py:class:`rgba`, but with half-float precision. :c:type:`rgba_f16` colors can be
converted to :c:type:`rgba_f32` colors and back using the half-float API.

:c:type:`rgba_u8` is the usual 8-bit color structure. Each channel has the
range [0, 255], which maps onto the floating-point range [0.0, 1.0].

:c:type:`rational` and :py:class:`fractions.Fraction` --- Rationals
===================================================================

``framework.h`` defines the type :c:type:`rational` for precise fractions::

    typedef struct {
        int n;
        unsigned int d;
    } rational;

This maps onto an existing Python type, :py:class:`fractions.Fraction`. The
following functions in ``pyframework.h`` perform conversions between :c:type:`rational`
and :py:class:`fractions.Fraction`:

.. c:function:: PyObject *py_make_rational(rgba_f32 *in)

    Convert the C :c:type:`rational` value *in* to
    the equivalent Python :py:class:`fractions.Fraction`.

.. c:function:: bool py_parse_rational(PyObject *obj, rgba_f32 *out)

    Parse a :py:class:`fractions.Fraction` value and store its value in *out*.
    If *obj* cannot be converted, this function sets an exception and returns false.

