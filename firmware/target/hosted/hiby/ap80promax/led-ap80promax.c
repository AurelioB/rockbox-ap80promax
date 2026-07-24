/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2025 Melissa Autumn
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SGM31324 indicator control for the Hidizs AP80 Pro Max.
 ****************************************************************************/
#include <stdbool.h>

#include "config.h"
#include "power.h"
#include "settings.h"
#include "sysfs.h"

static const char * const sysfs_led_pattern =
    "/sys/class/leds/sgm31324-leds/led_pattern";

enum ap80_led_pattern
{
    LED_PATTERN_INVALID = -1,
    LED_PATTERN_OFF = 0,
    LED_PATTERN_LIGHT_BLUE,
    LED_PATTERN_YELLOW,
    LED_PATTERN_CYAN,
    LED_PATTERN_ORANGE,
    LED_PATTERN_UNUSED,
    LED_PATTERN_NORMAL_RED_BLINK,
    LED_PATTERN_FAST_RED_BLINK,
    LED_PATTERN_GREEN,
    LED_PATTERN_BLUE,
    LED_PATTERN_PINK,
    LED_PATTERN_WHITE,
};

static int last_pattern = LED_PATTERN_INVALID;

static void set_pattern(enum ap80_led_pattern pattern)
{
    if (last_pattern != pattern)
    {
        last_pattern = pattern;
        sysfs_set_int(sysfs_led_pattern, pattern);
    }
}

void led_hw_brightness(int brightness)
{
    (void)brightness;
}

void led_hw_charged(void)
{
    if (global_settings.use_led_indicators)
        set_pattern(LED_PATTERN_GREEN);
}

void led_hw_charging(void)
{
    set_pattern(LED_PATTERN_NORMAL_RED_BLINK);
}

void led_hw_off(void)
{
    if (!charging_state())
        set_pattern(LED_PATTERN_OFF);
}

void led_hw_on(void)
{
    if (charging_state())
        led_hw_charging();
    else if (global_settings.use_led_indicators)
        set_pattern(LED_PATTERN_LIGHT_BLUE);
    else
        led_hw_off();
}

void led_hw_set_mode(bool mode)
{
    if (mode)
        led_hw_on();
    else
        led_hw_off();
}
