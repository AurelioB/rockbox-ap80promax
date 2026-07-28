/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2002 by Björn Stenberg
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
#ifndef _KEYBOARD_H
#define _KEYBOARD_H

/* '*kbd', same format as https://www.rockbox.org/wiki/LoadableKeyboardLayouts */

int kbd_input(char* buffer, int buflen, ucschar_t *kbd);

enum keyboard_input_flags
{
    /* Mask the edit line and disable all keyboard speech/audio feedback. */
    KBD_INPUT_SECURE = 0x1,
};

/* Core-only extended entry point. The plugin API intentionally continues to
 * expose kbd_input() so its ABI does not change. */
int kbd_input_ex(char* buffer, int buflen, ucschar_t *kbd,
                 unsigned int flags);

int load_kbd(unsigned char* filename);

#endif
