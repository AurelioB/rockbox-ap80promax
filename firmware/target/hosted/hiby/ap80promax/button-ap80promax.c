/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2017 Marcin Bukat
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
 * Hosted Linux input mapping for the Hidizs AP80 Pro Max.
 ****************************************************************************/
#include <linux/input.h>

#include "button.h"
#include "button-target.h"
#include "hibylinux_codec.h"
#include "touchscreen.h"
#ifdef HAVE_BACKLIGHT
#include "backlight.h"
#endif

/*
 * Vendor module configuration:
 *   gpio keyboard: KEY_POWER, KEY_NEXTSONG
 *   ADC keyboard:  KEY_PLAYPAUSE, KEY_PREVIOUSSONG
 *   ring encoder:  counter-clockwise KEY_LEFT, clockwise KEY_RIGHT
 *   CST8xx touch:  BTN_TOUCH plus absolute coordinates
 */
int button_map(int keycode)
{
    switch (keycode)
    {
        case KEY_LEFT:          /* counter-clockwise encoder rotation */
            return BUTTON_SCROLL_BACK;
        case KEY_RIGHT:         /* clockwise encoder rotation */
            return BUTTON_SCROLL_FWD;
        case KEY_VOLUMEUP:
            return BUTTON_VOL_UP;
        case KEY_VOLUMEDOWN:
            return BUTTON_VOL_DOWN;
        case KEY_PLAYPAUSE:
            return BUTTON_PLAY;
        /*
         * The AP80 kernel assigns the two transport GPIOs names opposite to
         * the legends on the enclosure.  Keep Rockbox's logical PREV/NEXT
         * meanings aligned with the physical buttons; this also fixes menu
         * direction in the bootloader, which uses the same target mapper.
         */
        case KEY_NEXTSONG:
            return BUTTON_PREV;
        case KEY_PREVIOUSSONG:
            return BUTTON_NEXT;
        case KEY_POWER:
            return BUTTON_POWER;
        case BTN_TOUCH:
#ifdef HAVE_BACKLIGHT
            return is_backlight_on(true) ? BUTTON_TOUCH : 0;
#else
            return BUTTON_TOUCH;
#endif
        default:
            return 0;
    }
}

bool headphones_inserted(void)
{
#ifdef BOOTLOADER
    int output = 0;
#else
    int output = hiby_get_outputs();
#endif

    /* 2 = 3.5 mm single-ended; 3 = 4.4 mm balanced. */
    return output == 2 || output == 3;
}
