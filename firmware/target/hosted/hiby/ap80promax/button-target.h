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
 * Button definitions for the Hidizs AP80 Pro Max.
 ****************************************************************************/
#ifndef _BUTTON_TARGET_H_
#define _BUTTON_TARGET_H_

#include <stdbool.h>
#include "config.h"

#define BUTTON_POWER       0x00000001
#define BUTTON_PREV        0x00000002
#define BUTTON_NEXT        0x00000004
#define BUTTON_PLAY        0x00000008
#define BUTTON_VOL_UP      0x00000010
#define BUTTON_VOL_DOWN    0x00000020
#define BUTTON_TOUCH       0x00000040
#define BUTTON_SCROLL_FWD  0x00000080
#define BUTTON_SCROLL_BACK 0x00000100
#define BUTTON_SCREEN      0x00200000
#define BUTTON_MAIN        0x002001bf

#define BUTTON_TOPLEFT      0x00001000
#define BUTTON_TOPMIDDLE    0x00002000
#define BUTTON_TOPRIGHT     0x00004000
#define BUTTON_MIDLEFT      0x00008000
#define BUTTON_CENTER       0x00010000
#define BUTTON_MIDRIGHT     0x00020000
#define BUTTON_BOTTOMLEFT   0x00040000
#define BUTTON_BOTTOMMIDDLE 0x00080000
#define BUTTON_BOTTOMRIGHT  0x00100000

#ifdef BOOTLOADER
#define POWEROFF_BUTTON BUTTON_POWER
#else
#define POWEROFF_BUTTON BUTTON_SCREEN
#endif
#define POWEROFF_COUNT 25

/*
 * A knob press toggles the display without activating a context-dependent
 * action. A held press still reaches the raw software power-off path.
 */
#define BACKLIGHT_FILTER_FIRST_KEYPRESS_BUTTONS POWEROFF_BUTTON

#ifndef BOOTLOADER
#define BUTTON_DEVINPUT_EVENT_HOOK
#define BUTTON_LEFT  BUTTON_NEXT
#define BUTTON_RIGHT BUTTON_PREV
bool button_handle_event(int keycode, int value);
#endif

int button_map(int keycode);

#endif /* _BUTTON_TARGET_H_ */
