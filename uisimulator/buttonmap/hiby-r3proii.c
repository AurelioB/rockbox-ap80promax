/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2021 by Solomon Peachy
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


#include <SDL.h>
#include "button.h"
#include "buttonmap.h"

int key_to_button(int keyboard_button)
{
    int new_btn = BUTTON_NONE;

    switch (keyboard_button)
    {
        case SDLK_KP_8:
        case SDLK_UP:
        case SDLK_LEFT:
            new_btn = BUTTON_PREV;
            break;

        case SDLK_KP_2:
        case SDLK_DOWN:
        case SDLK_RIGHT:
            new_btn = BUTTON_NEXT;
            break;

        case SDLK_KP_5:
        case SDLK_RETURN:
        case SDLK_SPACE:
            new_btn = BUTTON_PLAY;
            break;

        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            new_btn = BUTTON_POWER;
            break;

        case SDLK_KP_PLUS:
        case SDLK_EQUALS:
            new_btn = BUTTON_VOL_UP;
            break;

        case SDLK_KP_MINUS:
        case SDLK_MINUS:
            new_btn = BUTTON_VOL_DOWN;
            break;
    }

    return new_btn;
}

struct button_map bm[] = {
    { SDLK_UP,        0, 0, 0, "Previous / up" },
    { SDLK_DOWN,      0, 0, 0, "Next / down" },
    { SDLK_SPACE,     0, 0, 0, "Play / select" },
    { SDLK_ESCAPE,    0, 0, 0, "Power / back" },
    { SDLK_KP_PLUS,   0, 0, 0, "Volume +" },
    { SDLK_KP_MINUS,  0, 0, 0, "Volume -" },
    { 0, 0, 0, 0, "None" }
};
