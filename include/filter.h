
#if !defined(FLUGGO_FILTER_H)
#define FLUGGO_FILTER_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    // Coefficients of the filter's taps
    float *coeff;

    // Number of taps in the filter (length of the coeff array)
    int width;

    // Index of the center tap
    int center;
} fir_filter;

#if defined(__cplusplus)
}
#endif

/*
    Creates an FIR triangle filter suitable for 1:sub supersampling or sub:1 subsampling.

    Specify an offset of zero to have the filter centered on a sample. A nonzero offset
    will move the center by the specified fraction of taps. (filter->center will point to
    the tap that *would* have been the center)

    If filter->coeff is null, the coefficient array will be allocated for you.
    When you're done with the filter, free its coefficients with filter_free.

    If you would rather specify your own array, fill filter->coeff with the array and
    filter->width with its size. If the array is not big enough, coeff will be unaltered,
    center will be -1, and width will be the required size to hold the filter.
*/
void filter_createTriangle( float sub, float offset, fir_filter *filter );

/*
    Frees the coefficients
*/
void filter_free( fir_filter *filter );

#endif  // FLUGGO_FILTER_H


