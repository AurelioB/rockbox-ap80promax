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
#ifndef MSEB_MATH_H
#define MSEB_MATH_H

#include "mseb.h"
#include <stdint.h>

struct mseb_biquad
{
    double b0, b1, b2;
    double a1, a2;
};

void mseb_make_biquad(const struct mseb_filter_spec *spec, int gain_millidb,
                      unsigned int sample_rate, struct mseb_biquad *coefs);
void mseb_quantize_biquad(const struct mseb_biquad *coefs,
                          enum mseb_filter_type type, int32_t fixed[5],
                          uint8_t *shift);
void mseb_load_quantized_biquad(const int32_t fixed[5],
                                enum mseb_filter_type type,
                                struct mseb_biquad *coefs);
double mseb_response_magnitude_squared(const struct mseb_biquad coefs[],
                                       uint32_t active_mask,
                                       unsigned int sample_rate,
                                       double frequency);
double mseb_prefix_peak_magnitude_squared(const struct mseb_biquad coefs[],
                                          uint32_t active_mask,
                                          unsigned int sample_rate,
                                          double frequency);
int mseb_calculate_precut(const struct mseb_biquad coefs[],
                          uint32_t active_mask, unsigned int sample_rate);
int32_t mseb_precut_gain_q24(int precut_cdb);

#endif /* MSEB_MATH_H */
