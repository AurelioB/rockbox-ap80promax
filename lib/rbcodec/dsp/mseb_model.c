/***************************************************************************
 * HiBy-compatible MSEB filter model
 *
 * Copyright (C) 2026 The Rockbox Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#include "mseb.h"
#include <stddef.h>

/* MSEB's author described the public topology at:
 * https://www.audiosciencereview.com/forum/index.php?posts/2453731
 *
 * The more precise Q values and per-step gain laws below preserve the tuning
 * used on the AP80 Pro Max. Temperature is a cascade of four broad high
 * shelves, giving a smooth logarithmic tilt instead of a single hinge.
 */
static const struct mseb_filter_spec filter_specs[MSEB_NUM_FILTERS] =
{
    { MSEB_TEMPERATURE,       MSEB_FILTER_HIGH_SHELF,    33, 4425,  -60 },
    { MSEB_TEMPERATURE,       MSEB_FILTER_HIGH_SHELF,   231, 4425,  -60 },
    { MSEB_TEMPERATURE,       MSEB_FILTER_HIGH_SHELF,  1617, 4425,  -60 },
    { MSEB_TEMPERATURE,       MSEB_FILTER_HIGH_SHELF, 11319, 4425,  -60 },
    { MSEB_BASS_EXTENSION,    MSEB_FILTER_LOW_SHELF,     70, 7289,  250 },
    { MSEB_BASS_TEXTURE,      MSEB_FILTER_PEAK,          100, 8571,  200 },
    { MSEB_NOTE_THICKNESS,    MSEB_FILTER_PEAK,          200, 6667,  120 },
    { MSEB_VOCALS,            MSEB_FILTER_PEAK,          650, 4041,  125 },
    { MSEB_FEMALE_OVERTONES,  MSEB_FILTER_PEAK,         3000, 14140, 100 },
    { MSEB_SIBILANCE_LF,      MSEB_FILTER_PEAK,         5800, 9991,  100 },
    { MSEB_SIBILANCE_HF,      MSEB_FILTER_PEAK,         9200, 9991,  100 },
    { MSEB_IMPULSE_RESPONSE,  MSEB_FILTER_PEAK,         7500, 4041,  100 },
    { MSEB_AIR,               MSEB_FILTER_HIGH_SHELF,  10000, 7289,  250 },
};

const struct mseb_filter_spec *mseb_get_filter_spec(unsigned int index)
{
    return index < MSEB_NUM_FILTERS ? &filter_specs[index] : NULL;
}

int mseb_clamp_value(int value)
{
    if (value < MSEB_VALUE_MIN)
        return MSEB_VALUE_MIN;
    if (value > MSEB_VALUE_MAX)
        return MSEB_VALUE_MAX;
    return value;
}

int mseb_filter_gain_millidb(unsigned int index,
                            const int values[MSEB_NUM_PARAMETERS])
{
    const struct mseb_filter_spec *spec = mseb_get_filter_spec(index);

    if (spec == NULL || values == NULL)
        return 0;

    return spec->gain_millidb * mseb_clamp_value(values[spec->parameter]);
}

int mseb_range_limit(int range)
{
    static const unsigned char limits[MSEB_NUM_RANGES] = { 20, 40, 100 };

    if (range < 0 || range >= MSEB_NUM_RANGES)
        range = MSEB_RANGE_FINE;

    return limits[range];
}

int mseb_clamp_for_range(int value, int range)
{
    int limit = mseb_range_limit(range);

    value = mseb_clamp_value(value);
    if (value < -limit)
        return -limit;
    if (value > limit)
        return limit;
    return value;
}

void mseb_normalize_settings(struct mseb_settings *settings)
{
    if (settings == NULL)
        return;

    if (settings->range < 0 || settings->range >= MSEB_NUM_RANGES)
        settings->range = MSEB_RANGE_FINE;

    for (int i = 0; i < MSEB_NUM_PARAMETERS; i++)
        settings->values[i] = mseb_clamp_for_range(settings->values[i],
                                                   settings->range);
}

bool mseb_rate_supported(unsigned int frequency)
{
    return frequency >= MSEB_RATE_MIN && frequency <= MSEB_RATE_MAX;
}
