.. highlight:: python
.. default-domain:: py
.. currentmodule:: fluggo.editor.plugins

****************************************************************
:mod:`fluggo.editor.plugins` --- Canvas plugin support
****************************************************************

Alerts
======

Alerts are notifications that tell the user when something is *currently* and
*temporarily* wrong, and may give the user options to fix the problem. An object
can publish alerts about its own state, and other objects can follow those
alerts if their success depends on that object. Eventually, alerts will bubble
up to the project level and be shown to the user.

:class:`Alert` --- Alert class
------------------------------

This class holds text to display to the user. This is the most general alert
class--- only use it if your alert doesn't belong to a more specific category.

Alerts are immutable. If you want to change the text of an alert, unpublish it
and then publish a new alert with new text.

BJC: In the future, I want there to be many kinds of alerts so that the GUI can
offer specific solutions to those alerts.

.. class:: Alert(key, description, [icon=AlertIcon.NoIcon, source='', model_obj=None, exc_info=False, key=None])

    Creates an :class:`Alert` with the given *description*. The description
    is the text of the alert, to display to the end user. It should already be
    translated to the user's current language.

    The *icon* is one of the :class:`AlertIcon` values for an icon to display,
    either NoIcon, Information, Warning, or Error.

    The *source* parameter lets you set an optional string identifying the source
    of the alert.

    The *model_obj* is an optional parameter that you can set to one of the objects
    from the project, such as an :class:`fluggo.media.model.Asset`, a clip,
    or an effect. If set, the GUI may allow the user to navigate to the object.

    If *exc_info* is True, the :class:`Alert` captures the current exception.
    You can retrieve it later from the :attr:`exc_info` attribute.

    The *key* is an object that uniquely identifies this alert. If the same
    alert reaches the user twice, as determined by the key, it's only shown to
    the user once. By default, every alert is considered unique.

	Read-only attributes:

	.. attribute:: description

		The localized text of the alert.

    .. attribute:: source

        Optional text description of the source of the alert.

        BJC: I don't like this attribute. It may go away in favor of :attr:`model_obj`.

    .. attribute:: icon

        One of the values from :class:`AlertIcon`.

    .. attribute:: model_object

        Optional model object, such as an :class:`fluggo.media.model.Asset`,
        which is associated with this alert. Having this object lets the user
        navigate from this alert to the object.

    .. attribute:: exc_info

        Optional exception info captured at the time of the alert.

	.. attribute:: key

		The key for this alert.

    .. method: __str__()

        Return a text summary of the alert, useful for printing to the console.


:class:`AlertPublisher` --- Mixin class for objects that publish alerts
=======================================================================

.. class:: AlertPublisher()

    Initializes the :class:`AlertPublisher` instance. You must call this if you
    inherit from this class.

    Inheriting this class allows other alert publishers to follow your alerts and
    republish them.

    .. attribute: alerts

        The list of :class:`Alert` alerts published here.

    .. method: show_alert(alert)

        Publishes the given alert. It's safe to call this more than once for the
        same alert--- the alert is only published once.

    .. method: hide_alert(alert)

        Unpublishes the given alert.

    .. method: follow_alerts(publisher)

        Republishes alerts from *publisher*. Follow alerts from another publisher
        if its failure impacts you.

    .. method: unfollow_alerts(publisher)

        Stops following alerts from *publisher*, and unpublishes any alerts from
        it.

    Signals:

    .. method: alert_added(alert)

        A signal that gets invoked after a new *alert* is published.

    .. attribute: alert_removed(alert)

        A signal that gets invoked after *alert* is unpublished.


