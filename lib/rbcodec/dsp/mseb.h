/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__ /\_ \
 *                     \/            \/     \/    \/          \/
 *
 * Copyright (C) 2026 The Rockbox Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#ifndef MSEB_H
#define MSEB_H

#include <stdbool.h>

enum mseb_parameter
{
    MSEB_TEMPERATURE = 0,
    MSEB_BASS_EXTENSION,
    MSEB_BASS_TEXTURE,
    MSEB_NOTE_THICKNESS,
    MSEB_VOCALS,
    MSEB_FEMALE_OVERTONES,
    MSEB_SIBILANCE_LF,
    MSEB_SIBILANCE_HF,
    MSEB_IMPULSE_RESPONSE,
    MSEB_AIR,
    MSEB_NUM_PARAMETERS,
};

enum mseb_range
{
    MSEB_RANGE_FINE = 0,
    MSEB_RANGE_MIDDLING,
    MSEB_RANGE_EXCESSIVE,
    MSEB_NUM_RANGES,
};

enum mseb_filter_type
{
    MSEB_FILTER_LOW_SHELF = 0,
    MSEB_FILTER_PEAK,
    MSEB_FILTER_HIGH_SHELF,
};

#define MSEB_VALUE_MIN (-100)
#define MSEB_VALUE_MAX 100
#define MSEB_NUM_FILTERS 13
#define MSEB_RATE_MIN 32000
#define MSEB_RATE_MAX 96000

struct mseb_settings
{
    bool enabled;
    int range;
    int values[MSEB_NUM_PARAMETERS];
};

/* Public model data is integer-only so it can also be validated by host
 * tests without bringing in the DSP engine. */
struct mseb_filter_spec
{
    enum mseb_parameter parameter;
    enum mseb_filter_type type;
    int frequency;       /* Hz */
    int q_x10000;        /* Q * 10000 */
    int gain_millidb;    /* dB * 1000 for one positive slider step */
};

const struct mseb_filter_spec *mseb_get_filter_spec(unsigned int index);
int mseb_filter_gain_millidb(unsigned int index,
                            const int values[MSEB_NUM_PARAMETERS]);
int mseb_clamp_value(int value);
int mseb_range_limit(int range);
int mseb_clamp_for_range(int value, int range);
void mseb_normalize_settings(struct mseb_settings *settings);
bool mseb_rate_supported(unsigned int frequency);

void dsp_mseb_enable(bool enable);
void dsp_mseb_set_value(enum mseb_parameter parameter, int value);
void dsp_mseb_apply(const struct mseb_settings *settings);
int dsp_mseb_get_precut(void); /* automatic headroom, hundredths of a dB */

#endif /* MSEB_H */
