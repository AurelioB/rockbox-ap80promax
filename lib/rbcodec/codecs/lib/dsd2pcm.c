/***************************************************************************
 * Streaming DSD64/128/256 to 176.4 kHz PCM conversion.
 *
 * The first stage is a third-order CIC decimator to 352.8 kHz. A short
 * half-band FIR then removes the images and decimates to 176.4 kHz.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#include "dsd2pcm.h"
#include <string.h>

#define DSD_BASE_RATE 2822400u
#define CIC_RATE      (DSD2PCM_OUTPUT_RATE * 2)
#define PCM_MAX       0x0fffffffl

/* 31-tap Hamming-windowed half-band low-pass, Q30. */
static const int32_t fir_coefficients[DSD2PCM_FIR_TAPS] =
{
      -1825787,          0,    3153936,          0,
      -7226381,          0,   15133197,          0,
     -28760213,          0,   52719608,          0,
    -104086742,          0,  338893926,  537738736,
     338893926,          0, -104086742,          0,
      52719608,          0,  -28760213,          0,
      15133197,          0,   -7226381,          0,
       3153936,          0,   -1825787
};

/*
 * State deltas for four consecutive +1/-1 samples. Splitting a DSD byte
 * into two table lookups lets the third-order CIC integrators advance eight
 * samples at once. This is algebraically identical to updating them one bit
 * at a time, but avoids most 64-bit work on 32-bit targets.
 */
struct cic_nibble_delta
{
    int8_t sum;
    int8_t integral;
    int8_t double_integral;
};

static const struct cic_nibble_delta cic_nibble_deltas[16] =
{
    { -4, -10, -20 }, { -2,  -8, -18 },
    { -2,  -6, -14 }, {  0,  -4, -12 },
    { -2,  -4,  -8 }, {  0,  -2,  -6 },
    {  0,   0,  -2 }, {  2,   2,   0 },
    { -2,  -2,   0 }, {  0,   0,   2 },
    {  0,   2,   6 }, {  2,   4,   8 },
    {  0,   4,  12 }, {  2,   6,  14 },
    {  2,   8,  18 }, {  4,  10,  20 },
};

static int64_t modular_signed(uint64_t value)
{
    if (value <= (uint64_t)INT64_MAX)
        return (int64_t)value;

    return -(int64_t)(~value) - 1;
}

static inline void cic_integrate_byte(struct dsd2pcm_state *state,
                                      unsigned int channel, uint8_t byte)
{
    const struct cic_nibble_delta *high = &cic_nibble_deltas[byte >> 4];
    const struct cic_nibble_delta *low = &cic_nibble_deltas[byte & 0x0f];
    int delta1 = high->sum + low->sum;
    int delta2 = high->integral + 4 * high->sum + low->integral;
    int delta3 = high->double_integral + 4 * high->integral +
                 10 * high->sum + low->double_integral;
    uint64_t integrator1 = state->integrator[channel][0];
    uint64_t integrator2 = state->integrator[channel][1];
    uint64_t integrator3 = state->integrator[channel][2];

    state->integrator[channel][2] =
        integrator3 + 8 * integrator2 + 36 * integrator1 +
        (uint64_t)(int64_t)delta3;
    state->integrator[channel][1] =
        integrator2 + 8 * integrator1 + (uint64_t)(int64_t)delta2;
    state->integrator[channel][0] =
        integrator1 + (uint64_t)(int64_t)delta1;
}

static int32_t cic_output(struct dsd2pcm_state *state, unsigned int channel)
{
    uint64_t value = state->integrator[channel][2];

    for (unsigned int stage = 0; stage < 3; ++stage)
    {
        uint64_t difference = value - state->comb_delay[channel][stage];
        state->comb_delay[channel][stage] = value;
        value = difference;
    }

    int64_t sample = modular_signed(value);
    int64_t scaled = sample * PCM_MAX;

    /*
     * The supported CIC ratios are 8, 16, and 32, whose cubic gains are
     * 2^9, 2^12, and 2^15. Avoid a signed 64-bit division for every channel
     * at 352.8 kHz, while retaining C division's rounding toward zero.
     */
    if (scaled < 0)
        return (int32_t)-((-scaled) >> state->cic_shift);
    return (int32_t)(scaled >> state->cic_shift);
}

static int32_t fir_output(const struct dsd2pcm_state *state,
                          unsigned int channel)
{
    int64_t accumulator = 0;
    const int32_t *history = state->fir_history[channel];
    unsigned int origin = state->fir_position + DSD2PCM_FIR_TAPS;

    /*
     * Every coefficient except the centre and the even-indexed symmetric
     * pairs is zero. Fold each pair into one multiply: 9 multiplies replace
     * a 31-tap loop without changing the result.
     */
    for (unsigned int tap = 0; tap < DSD2PCM_FIR_TAPS / 2; tap += 2)
    {
        accumulator += (int64_t)fir_coefficients[tap] *
                       ((int64_t)history[origin - tap] +
                        history[origin - (DSD2PCM_FIR_TAPS - 1 - tap)]);
    }
    accumulator += (int64_t)fir_coefficients[DSD2PCM_FIR_TAPS / 2] *
                   history[origin - DSD2PCM_FIR_TAPS / 2];

    accumulator += (int64_t)1 << 29;
    accumulator >>= 30;
    if (accumulator > PCM_MAX)
        accumulator = PCM_MAX;
    else if (accumulator < -PCM_MAX)
        accumulator = -PCM_MAX;
    return (int32_t)accumulator;
}

bool dsd2pcm_init(struct dsd2pcm_state *state, unsigned int dsd_rate)
{
    if (dsd_rate < DSD_BASE_RATE || dsd_rate > DSD_BASE_RATE * 4 ||
        dsd_rate % CIC_RATE != 0)
        return false;

    unsigned int decimation = dsd_rate / CIC_RATE;
    if (decimation != 8 && decimation != 16 && decimation != 32)
        return false;

    memset(state, 0, sizeof(*state));
    state->cic_decimation = decimation;
    state->cic_shift = decimation == 8 ? 9 :
                       decimation == 16 ? 12 : 15;
    return true;
}

size_t dsd2pcm_convert(struct dsd2pcm_state *state,
                       const uint8_t *input, size_t input_size,
                       int32_t *output, size_t output_frames)
{
    size_t produced = 0;

    /* A stereo DSD stream always contains complete L/R byte pairs. */
    input_size &= ~(size_t)1;
    for (size_t byte = 0; byte < input_size; byte += 2)
    {
        cic_integrate_byte(state, 0, input[byte]);
        cic_integrate_byte(state, 1, input[byte + 1]);

        state->cic_count += 8;
        if (state->cic_count != state->cic_decimation)
            continue;

        state->cic_count = 0;
        for (unsigned int channel = 0; channel < 2; ++channel)
        {
            int32_t sample = cic_output(state, channel);
            state->fir_history[channel][state->fir_position] = sample;
            state->fir_history[channel]
                              [state->fir_position + DSD2PCM_FIR_TAPS] =
                sample;
        }

        if (++state->fir_phase == 2)
        {
            state->fir_phase = 0;
            if (produced >= output_frames)
                return produced;

            output[produced * 2] = fir_output(state, 0);
            output[produced * 2 + 1] = fir_output(state, 1);
            ++produced;
        }

        if (++state->fir_position == DSD2PCM_FIR_TAPS)
            state->fir_position = 0;
    }

    return produced;
}
