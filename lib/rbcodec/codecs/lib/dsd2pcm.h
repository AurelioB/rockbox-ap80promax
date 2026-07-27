/***************************************************************************
 * Streaming DSD64/128/256 to 176.4 kHz PCM conversion.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#ifndef DSD2PCM_H
#define DSD2PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DSD2PCM_OUTPUT_RATE 176400u
#define DSD2PCM_FIR_TAPS 31

struct dsd2pcm_state
{
    unsigned int cic_decimation;
    unsigned int cic_shift;
    unsigned int cic_count;
    unsigned int fir_phase;
    unsigned int fir_position;
    uint64_t integrator[2][3];
    uint64_t comb_delay[2][3];
    /*
     * Mirror the ring once so the symmetric half-band FIR can address its
     * complete window without per-tap wraparound branches.
     */
    int32_t fir_history[2][DSD2PCM_FIR_TAPS * 2];
};

/* Input is byte-interleaved stereo DSD, MSB first: L0, R0, L1, R1...
 * input_size must contain complete L/R pairs. The output buffer must hold
 * input_size * 2 / (dsd_rate / 352800) stereo frames. */
bool dsd2pcm_init(struct dsd2pcm_state *state, unsigned int dsd_rate);
size_t dsd2pcm_convert(struct dsd2pcm_state *state,
                       const uint8_t *input, size_t input_size,
                       int32_t *output, size_t output_frames);

#endif
