.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.media.process

Frame functions
===============

.. class:: FrameFunction

    :class:`FrameFunction` is the base class for frame functions.

    :class:`FrameFunction` has one public method for retrieving function values:

    .. method:: get_values(frames)

        Produce a list of four-tuples with the value of the function at each frame
        in *frames*, which can be either a single float or a list of floats.

:class:`LinearFrameFunc` --- Simple linear function
---------------------------------------------------

The :class:`LinearFrameFunc` represents the simple function:

.. math::

    f(x) = \begin{pmatrix}
        ax + b\\
        0\\
        0\\
        0\\
      \end{pmatrix}

.. class:: LinearFrameFunc(a, b)

    Create a :class:`LinearFrameFunc` with parameters *a* and *b*.

:class:`LerpFunc` --- Linear interpolation
------------------------------------------

:class:`LerpFunc` linearly interpolates between two sets of values:

.. math::

    \text{lerp}(x, v_0, v_1)& = \begin{cases}
        v_0& x < 0\\
        v_0 + \dfrac{x(v_1 - v_0)}{\ell}& 0 \leq x \leq \ell\\
        v_1& x > \ell
        \end{cases}\\
    f(x)& = \begin{pmatrix}
        \text{lerp}(x, s_0, e_0)\\
        \text{lerp}(x, s_1, e_1)\\
        \text{lerp}(x, s_2, e_2)\\
        \text{lerp}(x, s_3, e_3)
    \end{pmatrix}

.. class:: LerpFunc(start, end, length)

    Create a :class:`LerpFunc` that interpolates from *start* to *end* over *length*
    frames. *start* and *end* should be tuples or lists of floats, and *length* can
    be a float. If *start* or *end* is shorter than four values, they are padded
    with zeroes.

