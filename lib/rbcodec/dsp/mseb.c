/***************************************************************************
 * HiBy-compatible MSEB subjective equalizer
 *
 * Copyright (C) 2026 The Rockbox Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include "platform.h"
#include "dsp_filter.h"
#include "dsp_proc_entry.h"
#include "dsp_core.h"
#include "dsp_misc.h"
#include "fracmul.h"
#include "mseb.h"
#include "mseb_math.h"

static int control_values[MSEB_NUM_PARAMETERS];

struct mseb_filter_bank
{
    uint8_t active[MSEB_NUM_FILTERS + 1];
    struct dsp_filter filters[MSEB_NUM_FILTERS];
    int precut_cdb;
    int32_t precut_gain_q24;
};

static struct mseb_state
{
    struct mseb_filter_bank banks[2];
    unsigned int current_bank;
    unsigned int readers[2];
    unsigned int rebuild_active;
    unsigned int rebuild_pending;
    unsigned int requested_frequency;
} mseb_data IBSS_ATTR;

#define FOR_EACH_ACTIVE_FILTER(bank, f) \
    for (uint8_t *f = (bank)->active; *f < MSEB_NUM_FILTERS; f++)

static void store_biquad(struct dsp_filter *filter,
                         const struct mseb_biquad *coefs,
                         enum mseb_filter_type type)
{
    mseb_quantize_biquad(coefs, type, filter->coefs, &filter->shift);
}

static void load_quantized_biquad(const struct dsp_filter *filter,
                                  enum mseb_filter_type type,
                                  struct mseb_biquad *coefs)
{
    mseb_load_quantized_biquad(filter->coefs, type, coefs);
}

static struct mseb_filter_bank *pin_current_bank(unsigned int *index)
{
    /* If publication raced the pin, retry with the new current bank;
     * rebuild_filter_bank() will not reuse a pinned bank. */
    while (true)
    {
        *index = __atomic_load_n(&mseb_data.current_bank, __ATOMIC_SEQ_CST);
        __atomic_add_fetch(&mseb_data.readers[*index], 1, __ATOMIC_SEQ_CST);
        if (*index == __atomic_load_n(&mseb_data.current_bank,
                                      __ATOMIC_SEQ_CST))
            return &mseb_data.banks[*index];
        __atomic_sub_fetch(&mseb_data.readers[*index], 1, __ATOMIC_SEQ_CST);
    }
}

static void unpin_bank(unsigned int index)
{
    __atomic_sub_fetch(&mseb_data.readers[index], 1, __ATOMIC_SEQ_CST);
}

static void mseb_flush(void)
{
    unsigned int index;
    struct mseb_filter_bank *bank = pin_current_bank(&index);

    FOR_EACH_ACTIVE_FILTER(bank, f)
        filter_flush(&bank->filters[*f]);

    unpin_bank(index);
}

static void rebuild_filter_bank(struct dsp_config *dsp,
                                unsigned int frequency)
{
    unsigned int old_index = __atomic_load_n(&mseb_data.current_bank,
                                              __ATOMIC_SEQ_CST);
    unsigned int new_index = old_index ^ 1u;
    struct mseb_filter_bank *bank = &mseb_data.banks[new_index];
    struct mseb_biquad double_coefs[MSEB_NUM_FILTERS];
    int values[MSEB_NUM_PARAMETERS];
    uint32_t mask = 0;
    unsigned int active_count = 0;

    /* Never overwrite a bank while the codec thread is still consuming it.
     * Rebuilds are infrequent and a processing block is short, so this wait
     * normally completes immediately. */
    while (__atomic_load_n(&mseb_data.readers[new_index],
                           __ATOMIC_SEQ_CST) != 0)
        ;

    for (int i = 0; i < MSEB_NUM_PARAMETERS; i++)
        values[i] = __atomic_load_n(&control_values[i], __ATOMIC_SEQ_CST);

    if (mseb_rate_supported(frequency))
    {
        for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
        {
            const struct mseb_filter_spec *spec = mseb_get_filter_spec(i);
            int gain = mseb_filter_gain_millidb(i, values);

            if (gain == 0)
                continue;

            mseb_make_biquad(spec, gain, frequency, &double_coefs[i]);
            store_biquad(&bank->filters[i], &double_coefs[i], spec->type);
            load_quantized_biquad(&bank->filters[i], spec->type,
                                  &double_coefs[i]);
            mask |= BIT_N(i);
            bank->active[active_count++] = i;
            filter_flush(&bank->filters[i]);
        }
    }

    bank->active[active_count] = MSEB_NUM_FILTERS;
    if (mask != 0)
        bank->precut_cdb = mseb_calculate_precut(double_coefs, mask,
                                                 frequency);
    else
        bank->precut_cdb = 0;

    bank->precut_gain_q24 = mseb_precut_gain_q24(bank->precut_cdb);

    /* The gain and filters become visible as one coherent bank. This avoids
     * exposing boosted coefficients with stale headroom while the relatively
     * expensive response scan is still running. */
    __atomic_store_n(&mseb_data.current_bank, new_index, __ATOMIC_SEQ_CST);
    dsp_proc_activate(dsp, DSP_PROC_MSEB, mask != 0);
}

static void rebuild_filters(struct dsp_config *dsp, unsigned int frequency)
{
    /* Publish a latest-value mailbox before marking work pending. Repeated
     * requests may collapse into one rebuild, but the builder cannot mistake
     * a wrapped generation for an already-processed request. Both fields are
     * naturally atomic on the 32-bit target. */
    __atomic_store_n(&mseb_data.requested_frequency, frequency,
                     __ATOMIC_SEQ_CST);
    __atomic_store_n(&mseb_data.rebuild_pending, 1, __ATOMIC_SEQ_CST);

    for (;;)
    {
        unsigned int expected = 0;
        if (!__atomic_compare_exchange_n(&mseb_data.rebuild_active, &expected,
                                         1, false, __ATOMIC_SEQ_CST,
                                         __ATOMIC_SEQ_CST))
            return;

        while (__atomic_exchange_n(&mseb_data.rebuild_pending, 0,
                                   __ATOMIC_SEQ_CST) != 0)
        {
            frequency = __atomic_load_n(&mseb_data.requested_frequency,
                                        __ATOMIC_SEQ_CST);
            rebuild_filter_bank(dsp, frequency);
        }

        __atomic_store_n(&mseb_data.rebuild_active, 0, __ATOMIC_SEQ_CST);

        /* A request can arrive after the final exchange but before the builder
         * publishes the unlocked state. If it observed a busy builder and
         * returned, reacquire here and drain its pending work. If it acquired
         * the lock itself, our compare-exchange will simply fail. */
        if (__atomic_load_n(&mseb_data.rebuild_pending,
                            __ATOMIC_SEQ_CST) == 0)
            return;
    }
}

void dsp_mseb_set_value(enum mseb_parameter parameter, int value)
{
    if (parameter < 0 || parameter >= MSEB_NUM_PARAMETERS)
        return;

    value = mseb_clamp_value(value);
    if (__atomic_exchange_n(&control_values[parameter], value,
                            __ATOMIC_SEQ_CST) == value)
        return;

    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    if (dsp_proc_enabled(dsp, DSP_PROC_MSEB))
        rebuild_filters(dsp, dsp_get_output_frequency(dsp));
}

void dsp_mseb_enable(bool enable)
{
    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    bool old_enabled = dsp_proc_enabled(dsp, DSP_PROC_MSEB);

    if (enable == old_enabled)
        return;

    dsp_proc_enable(dsp, DSP_PROC_MSEB, enable);
}

void dsp_mseb_apply(const struct mseb_settings *settings)
{
    if (settings == NULL)
        return;

    for (int i = 0; i < MSEB_NUM_PARAMETERS; i++)
        __atomic_store_n(&control_values[i],
                         mseb_clamp_for_range(settings->values[i],
                                              settings->range),
                         __ATOMIC_SEQ_CST);

    struct dsp_config *dsp = dsp_get_config(CODEC_IDX_AUDIO);
    if (dsp_proc_enabled(dsp, DSP_PROC_MSEB))
    {
        if (settings->enabled)
            rebuild_filters(dsp, dsp_get_output_frequency(dsp));
        else
            dsp_mseb_enable(false);
    }
    else if (settings->enabled)
    {
        dsp_mseb_enable(true);
    }
}

int dsp_mseb_get_precut(void)
{
    unsigned int index;
    struct mseb_filter_bank *bank = pin_current_bank(&index);
    int precut_cdb = bank->precut_cdb;
    unpin_bank(index);
    return precut_cdb;
}

static void mseb_process(struct dsp_proc_entry *this,
                         struct dsp_buffer **buf_p)
{
    struct dsp_buffer *buf = *buf_p;
    unsigned int index;

    struct mseb_filter_bank *bank = pin_current_bank(&index);

    if (bank->precut_gain_q24 != (1 << 24))
    {
        for (unsigned int channel = 0; channel < buf->format.num_channels;
             channel++)
        {
            int32_t *samples = buf->p32[channel];
            for (int i = 0; i < buf->remcount; i++)
                samples[i] = FRACMUL_SHL(samples[i],
                                         bank->precut_gain_q24, 7);
        }
    }

    FOR_EACH_ACTIVE_FILTER(bank, f)
        filter_process(&bank->filters[*f], buf->p32, buf->remcount,
                       buf->format.num_channels);

    unpin_bank(index);

    (void)this;
}

static intptr_t mseb_configure(struct dsp_proc_entry *this,
                               struct dsp_config *dsp,
                               unsigned int setting, intptr_t value)
{
    switch (setting)
    {
        case DSP_PROC_INIT:
            this->process = mseb_process;
            rebuild_filters(dsp, dsp_get_output_frequency(dsp));
            break;

        case DSP_FLUSH:
            mseb_flush();
            break;

        case DSP_SET_OUT_FREQUENCY:
            rebuild_filters(dsp, value);
            break;
    }

    return 0;
}

DSP_PROC_DB_ENTRY(MSEB, mseb_configure);
