/***************************************************************************
 * MSEB coefficient and response calculations
 *
 * Copyright (C) 2026 The Rockbox Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include "mseb_math.h"
#include <limits.h>
#include <math.h>

#define MSEB_RESPONSE_POINTS 512
#define MSEB_HEADROOM_MARGIN_CDB 10
#define MSEB_PI 3.14159265358979323846
#define MSEB_PEAK_FRACBITS  28
#define MSEB_PEAK_SHIFT      4
#define MSEB_SHELF_FRACBITS 26
#define MSEB_SHELF_SHIFT     6

static int32_t quantize_coefficient(double value, unsigned int fracbits)
{
    double scaled = value * (double)(1UL << fracbits);

    if (scaled >= (double)INT32_MAX)
        return INT32_MAX;
    if (scaled <= (double)INT32_MIN)
        return INT32_MIN;

    return (int32_t)(scaled < 0.0 ? scaled - 0.5 : scaled + 0.5);
}

void mseb_make_biquad(const struct mseb_filter_spec *spec, int gain_millidb,
                      unsigned int sample_rate, struct mseb_biquad *coefs)
{
    const double w0 = 2.0 * MSEB_PI * spec->frequency / sample_rate;
    const double cs = cos(w0);
    const double alpha = sin(w0) / (2.0 * spec->q_x10000 / 10000.0);
    const double gain_db = gain_millidb / 1000.0;
    const double A = pow(10.0, gain_db / 40.0);
    double b0, b1, b2, a0, a1, a2;

    if (spec->type == MSEB_FILTER_PEAK)
    {
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cs;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha / A;
    }
    else
    {
        const double ap1 = A + 1.0;
        const double am1 = A - 1.0;
        const double two_sqrtA_alpha = 2.0 * sqrt(A) * alpha;

        if (spec->type == MSEB_FILTER_LOW_SHELF)
        {
            b0 = A * (ap1 - am1 * cs + two_sqrtA_alpha);
            b1 = 2.0 * A * (am1 - ap1 * cs);
            b2 = A * (ap1 - am1 * cs - two_sqrtA_alpha);
            a0 = ap1 + am1 * cs + two_sqrtA_alpha;
            a1 = -2.0 * (am1 + ap1 * cs);
            a2 = ap1 + am1 * cs - two_sqrtA_alpha;
        }
        else
        {
            b0 = A * (ap1 + am1 * cs + two_sqrtA_alpha);
            b1 = -2.0 * A * (am1 + ap1 * cs);
            b2 = A * (ap1 + am1 * cs - two_sqrtA_alpha);
            a0 = ap1 - am1 * cs + two_sqrtA_alpha;
            a1 = 2.0 * (am1 - ap1 * cs);
            a2 = ap1 - am1 * cs - two_sqrtA_alpha;
        }
    }

    coefs->b0 = b0 / a0;
    coefs->b1 = b1 / a0;
    coefs->b2 = b2 / a0;
    coefs->a1 = a1 / a0;
    coefs->a2 = a2 / a0;
}

void mseb_quantize_biquad(const struct mseb_biquad *coefs,
                          enum mseb_filter_type type, int32_t fixed[5],
                          uint8_t *shift)
{
    unsigned int fracbits = type == MSEB_FILTER_PEAK ?
                            MSEB_PEAK_FRACBITS : MSEB_SHELF_FRACBITS;

    fixed[0] = quantize_coefficient(coefs->b0, fracbits);
    fixed[1] = quantize_coefficient(coefs->b1, fracbits);
    fixed[2] = quantize_coefficient(coefs->b2, fracbits);
    fixed[3] = quantize_coefficient(-coefs->a1, fracbits);
    fixed[4] = quantize_coefficient(-coefs->a2, fracbits);
    *shift = type == MSEB_FILTER_PEAK ? MSEB_PEAK_SHIFT : MSEB_SHELF_SHIFT;
}

void mseb_load_quantized_biquad(const int32_t fixed[5],
                                enum mseb_filter_type type,
                                struct mseb_biquad *coefs)
{
    unsigned int fracbits = type == MSEB_FILTER_PEAK ?
                            MSEB_PEAK_FRACBITS : MSEB_SHELF_FRACBITS;
    const double scale = (double)(1UL << fracbits);

    coefs->b0 = fixed[0] / scale;
    coefs->b1 = fixed[1] / scale;
    coefs->b2 = fixed[2] / scale;
    coefs->a1 = -fixed[3] / scale;
    coefs->a2 = -fixed[4] / scale;
}

double mseb_response_magnitude_squared(const struct mseb_biquad coefs[],
                                       uint32_t active_mask,
                                       unsigned int sample_rate,
                                       double frequency)
{
    const double w = 2.0 * MSEB_PI * frequency / sample_rate;
    const double c1 = cos(w), s1 = sin(w);
    const double c2 = 2.0 * c1 * c1 - 1.0;
    const double s2 = 2.0 * s1 * c1;
    double magnitude = 1.0;

    for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
    {
        if (!(active_mask & (1u << i)))
            continue;

        const struct mseb_biquad *c = &coefs[i];
        const double nr = c->b0 + c->b1 * c1 + c->b2 * c2;
        const double ni = -c->b1 * s1 - c->b2 * s2;
        const double dr = 1.0 + c->a1 * c1 + c->a2 * c2;
        const double di = -c->a1 * s1 - c->a2 * s2;
        magnitude *= (nr * nr + ni * ni) / (dr * dr + di * di);
    }

    return magnitude;
}

double mseb_prefix_peak_magnitude_squared(const struct mseb_biquad coefs[],
                                          uint32_t active_mask,
                                          unsigned int sample_rate,
                                          double frequency)
{
    const double w = 2.0 * MSEB_PI * frequency / sample_rate;
    const double c1 = cos(w), s1 = sin(w);
    const double c2 = 2.0 * c1 * c1 - 1.0;
    const double s2 = 2.0 * s1 * c1;
    double magnitude = 1.0;
    double peak = 1.0;

    for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
    {
        if (!(active_mask & (1u << i)))
            continue;

        const struct mseb_biquad *c = &coefs[i];
        const double nr = c->b0 + c->b1 * c1 + c->b2 * c2;
        const double ni = -c->b1 * s1 - c->b2 * s2;
        const double dr = 1.0 + c->a1 * c1 + c->a2 * c2;
        const double di = -c->a1 * s1 - c->a2 * s2;
        magnitude *= (nr * nr + ni * ni) / (dr * dr + di * di);

        if (magnitude > peak)
            peak = magnitude;
    }

    return peak;
}

int mseb_calculate_precut(const struct mseb_biquad coefs[],
                          uint32_t active_mask, unsigned int sample_rate)
{
    if (active_mask == 0)
        return 0;

    double max_magnitude = mseb_prefix_peak_magnitude_squared(coefs,
                                             active_mask, sample_rate, 0.0);
    const double upper = sample_rate * 0.5;
    const double ratio = pow(upper / 20.0,
                             1.0 / (MSEB_RESPONSE_POINTS - 1));
    double frequency = 20.0;

    for (int i = 0; i < MSEB_RESPONSE_POINTS; i++, frequency *= ratio)
    {
        double magnitude = mseb_prefix_peak_magnitude_squared(coefs,
                                      active_mask, sample_rate, frequency);
        if (magnitude > max_magnitude)
            max_magnitude = magnitude;
    }

    /* Shelves reach their asymptotic gain at Nyquist. The logarithmic loop
     * can land a few ulps short due to repeated multiplication, so sample the
     * endpoint explicitly. This is also essential for 88.2/96 kHz output,
     * where the Air shelf continues rising above 20 kHz. */
    double magnitude = mseb_prefix_peak_magnitude_squared(coefs, active_mask,
                                                           sample_rate, upper);
    if (magnitude > max_magnitude)
        max_magnitude = magnitude;

    /* Include every exact centre in addition to the logarithmic grid. */
    for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
    {
        const struct mseb_filter_spec *spec = mseb_get_filter_spec(i);
        double magnitude = mseb_prefix_peak_magnitude_squared(coefs,
                               active_mask, sample_rate, spec->frequency);
        if (magnitude > max_magnitude)
            max_magnitude = magnitude;
    }

    if (max_magnitude <= 1.0000000001)
        return 0;

    return (int)ceil(1000.0 * log10(max_magnitude)) +
           MSEB_HEADROOM_MARGIN_CDB;
}

int32_t mseb_precut_gain_q24(int precut_cdb)
{
    const double unity = 16777216.0; /* 1 << 24 */
    double gain = pow(10.0, -precut_cdb / 2000.0) * unity;

    if (gain >= unity)
        return (int32_t)unity;
    if (gain <= 1.0)
        return 1;
    return (int32_t)(gain + 0.5);
}
