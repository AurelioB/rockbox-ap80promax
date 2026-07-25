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

static int64_t modular_signed(uint64_t value)
{
    if (value <= (uint64_t)INT64_MAX)
        return (int64_t)value;

    return -(int64_t)(~value) - 1;
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
    int64_t gain = (int64_t)state->cic_decimation *
                   state->cic_decimation * state->cic_decimation;
    return (int32_t)(sample * PCM_MAX / gain);
}

static int32_t fir_output(const struct dsd2pcm_state *state,
                          unsigned int channel)
{
    int64_t accumulator = 0;
    unsigned int position = state->fir_position;

    for (unsigned int tap = 0; tap < DSD2PCM_FIR_TAPS; ++tap)
    {
        accumulator += (int64_t)fir_coefficients[tap] *
                       state->fir_history[channel][position];
        position = position == 0 ? DSD2PCM_FIR_TAPS - 1 : position - 1;
    }

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
        for (unsigned int bit = 0; bit < 8; ++bit)
        {
            for (unsigned int channel = 0; channel < 2; ++channel)
            {
                uint64_t sample =
                    input[byte + channel] & (0x80u >> bit) ? 1 : UINT64_MAX;
                state->integrator[channel][0] += sample;
                state->integrator[channel][1] +=
                    state->integrator[channel][0];
                state->integrator[channel][2] +=
                    state->integrator[channel][1];
            }

            if (++state->cic_count != state->cic_decimation)
                continue;

            state->cic_count = 0;
            for (unsigned int channel = 0; channel < 2; ++channel)
            {
                state->fir_history[channel][state->fir_position] =
                    cic_output(state, channel);
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
    }

    return produced;
}
