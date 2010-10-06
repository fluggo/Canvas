.. highlight:: c

*****
Audio
*****

Audio frames
============

.. c:type:: audio_frame

    The :c:type:`audio_frame` structure is used to store a frame of audio,
    containing one or more floating-point audio samples::

        typedef struct {
            float *data;
            int channels;
            int full_min_sample, full_max_sample;
            int current_min_sample, current_max_sample;
        } audio_frame;

    .. c:member:: float *data

        Buffer where the sample data is stored. It is at least
        ``sizeof(float) * channels * (full_max_sample - full_min_sample + 1)``
        bytes. Channels are interleaved; a sample can be found at
        ``data[(sample - full_min_sample) * channels + channel]``.

    .. c:member:: int channels

        The number of channels in the buffer. Note that this is
        constant: An audio source should not set this to the number of channels
        it can produce, but rather produce zero samples for channels it
        does not have.

    The samples covered by the frame are defined by the full window (:c:member:`full_min_sample`
    to :c:member:`full_max_sample`) and the current window (:c:member:`current_min_sample`
    to :c:member:`current_max_sample`). The current window defines which samples
    in the full window are valid. Samples outside the current window should be
    considered zero.

    .. c:member:: int full_min_sample

        The index of the sample found at ``data[0]``.

    .. c:member:: int full_max_sample

        The index of the last possible sample in the buffer,
        which is at ``data[(full_max_sample - full_min_sample) * channels]``.

    .. c:member:: int current_min_sample

        The index of the first valid sample in the buffer. If the frame isn't
        empty, this should be at least *full_min_sample*, at most *full_max_sample*,
        and less than or equal to *current_max_sample*. If the frame contains no
        valid samples, this is greater than *current_max_sample*, but doesn't
        have any relationship with the buffer. Empty frames often have a min
        sample of zero and a max sample of ``-1``.

    .. c:member:: int current_max_sample

        The index of the last valid sample in the buffer. If the frame isn't
        empty, this should be at least *full_min_sample*, at most *full_max_sample*,
        and greater than or equal to *current_min_sample*. If the frame contains
        no valid samples, this is less than *current_min_sample*, but doesn't
        have any relationship with the buffer. Empty frames often have a min
        sample of zero and a max sample of ``-1``.

.. c:function:: static inline float *audio_get_sample(const audio_frame *frame, int sample, int channel)

    Get a pointer to the sample *sample* on channel *channel* in frame *frame*.
    This function doesn't check the parameters. If you want to dereference the
    returned pointer, *sample* should be between :c:member:`full_min_sample` and
    :c:member:`full_max_sample`, and *channel* should be between zero and
    :c:member:`channels`.

.. c:function:: void audio_get_frame(const audio_source *source, audio_frame *frame)

    Fetch a frame *frame* from audio source *source*. Before you call, set
    :c:member:`full_min_sample` and :c:member:`full_max_sample` to
    the sample range you want to retrieve (be sure to also set :c:member:`data`
    and :c:member:`channels`). Before it returns, :c:func:`audio_get_frame` will
    set :c:member:`current_min_sample` and :c:member:`current_max_sample`.

.. c:function:: void audio_copy_frame(audio_frame *out, const audio_frame *in, int offset)

    Copy frame *in* to frame *out*, moving it *offset* samples earlier.
    The *data*, *channels*, *full_min_sample*, and *full_max_sample* fields are required for *out*; the *full_min_sample*
    and *full_max_sample* fields determine what samples are copied from *in*.

.. c:function:: void audio_copy_frame_attenuate(audio_frame *out, const audio_frame *in, float factor, int offset)

.. c:function:: void audio_attenuate(audio_frame *frame, float factor)

    Attenuate (multiply) *frame* by factor *factor*.

.. c:function:: void audio_mix_add(audio_frame *out, const audio_frame *a, float mix_a, int offset)

    Add the contents of frame *a* into existing frame *out*, moving it *offset*
    samples earlier. *mix_a* determines how much *a* contributes to the result.

.. c:function:: void audio_mix_add_pull(audio_frame *out, const audio_source *a, float mix_a, int offset_a)

    Like :c:func:`audio_mix_add`, but pulls the source frame from an :c:type:`audio_source`.


