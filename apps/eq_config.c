/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
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
#include "eq_config.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' ||
           c == '\n' || c == '\f' || c == '\v';
}

static char ascii_upper(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

static void skip_space(const char **cursor)
{
    while (ascii_space(**cursor))
        (*cursor)++;
}

static bool parse_integer(const char **cursor, char separator, int *result)
{
    const char *p = *cursor;
    bool negative = false;
    unsigned long magnitude = 0;
    unsigned long limit;

    skip_space(&p);
    if (*p == '-' || *p == '+')
    {
        negative = *p == '-';
        p++;
    }

    if (*p < '0' || *p > '9')
        return false;

    limit = negative ? (unsigned long)INT_MAX + 1ul :
                       (unsigned long)INT_MAX;
    do
    {
        unsigned int digit = (unsigned int)(*p - '0');
        if (magnitude > (limit - digit) / 10ul)
            return false;
        magnitude = magnitude * 10ul + digit;
        p++;
    }
    while (*p >= '0' && *p <= '9');

    skip_space(&p);
    if (separator != '\0')
    {
        if (*p != separator)
            return false;
        p++;
    }
    else if (*p != '\0')
    {
        return false;
    }

    if (negative)
    {
        if (magnitude == (unsigned long)INT_MAX + 1ul)
            *result = INT_MIN;
        else
            *result = -(int)magnitude;
    }
    else
    {
        *result = (int)magnitude;
    }

    *cursor = p;
    return true;
}

static bool token_equals(const char *token, size_t token_length,
                         const char *expected)
{
    size_t expected_length = strlen(expected);
    if (token_length != expected_length)
        return false;

    for (size_t i = 0; i < token_length; i++)
    {
        if (ascii_upper(token[i]) != expected[i])
            return false;
    }

    return true;
}

static bool parse_filter_type(const char **cursor, enum eq_filter_type *type)
{
    const char *p = *cursor;
    const char *token;

    skip_space(&p);
    token = p;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
           *p == '_')
    {
        p++;
    }

    size_t token_length = (size_t)(p - token);
    skip_space(&p);
    if (token_length == 0 || *p != '\0')
        return false;

    if (token_equals(token, token_length, "LOW_SHELF"))
        *type = EQ_FILTER_LOW_SHELF;
    else if (token_equals(token, token_length, "PEAK"))
        *type = EQ_FILTER_PEAK;
    else if (token_equals(token, token_length, "HIGH_SHELF"))
        *type = EQ_FILTER_HIGH_SHELF;
    else
        return false;

    *cursor = p;
    return true;
}

static bool values_are_valid(const struct eq_band_setting *setting)
{
    return setting->cutoff >= EQ_CUTOFF_MIN &&
           setting->cutoff <= EQ_CUTOFF_MAX &&
           setting->q >= EQ_Q_MIN &&
           setting->q <= EQ_Q_MAX &&
           setting->gain >= EQ_GAIN_MIN &&
           setting->gain <= EQ_GAIN_MAX;
}

bool eq_filter_type_is_valid(enum eq_filter_type type)
{
    return type >= EQ_FILTER_LOW_SHELF && type < EQ_FILTER_TYPE_COUNT;
}

const char *eq_filter_type_config_name(enum eq_filter_type type)
{
    switch (type)
    {
        case EQ_FILTER_LOW_SHELF:
            return "LOW_SHELF";
        case EQ_FILTER_HIGH_SHELF:
            return "HIGH_SHELF";
        case EQ_FILTER_PEAK:
            return "PEAK";
        default:
            return NULL;
    }
}

bool eq_config_parse(const char *value, struct eq_band_setting *setting)
{
    if (value == NULL || setting == NULL)
        return false;

    struct eq_band_setting parsed;
    const char *cursor = value;

    if (!parse_integer(&cursor, ',', &parsed.cutoff) ||
        !parse_integer(&cursor, ',', &parsed.q) ||
        !parse_integer(&cursor, ',', &parsed.gain) ||
        !parse_filter_type(&cursor, &parsed.type) ||
        !values_are_valid(&parsed))
    {
        return false;
    }

    *setting = parsed;
    return true;
}

bool eq_config_parse_legacy(const char *value, enum eq_filter_type type,
                            struct eq_band_setting *setting)
{
    if (value == NULL || setting == NULL || !eq_filter_type_is_valid(type))
        return false;

    struct eq_band_setting parsed = { .type = type };
    const char *cursor = value;

    if (!parse_integer(&cursor, ',', &parsed.cutoff) ||
        !parse_integer(&cursor, ',', &parsed.q) ||
        !parse_integer(&cursor, '\0', &parsed.gain))
    {
        return false;
    }

    /* Historical presets documented Q=0 and the DSP treated it as its
     * minimum nonzero value. Preserve that effective behavior while
     * migrating the stored representation. */
    if (parsed.q == 0)
        parsed.q = EQ_Q_MIN;

    if (!values_are_valid(&parsed))
        return false;

    *setting = parsed;
    return true;
}

bool eq_config_format(const struct eq_band_setting *setting,
                      char *buffer, size_t buffer_size)
{
    if (setting == NULL || buffer == NULL || buffer_size == 0 ||
        !values_are_valid(setting))
        return false;

    const char *type = eq_filter_type_config_name(setting->type);
    if (type == NULL)
        return false;

    int length = snprintf(buffer, buffer_size, "%d, %d, %d, %s",
                          setting->cutoff, setting->q, setting->gain, type);
    return length >= 0 && (size_t)length < buffer_size;
}
