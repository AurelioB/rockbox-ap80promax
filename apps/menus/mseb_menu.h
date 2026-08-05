/***************************************************************************
 * MSEB settings menu
 *
 * Copyright (C) 2026 The Rockbox Developers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
#ifndef _MSEB_MENU_H
#define _MSEB_MENU_H

#include "menu.h"
#include <stdbool.h>

extern const struct menu_item_ex mseb_menu;

void mseb_enabled_option_callback(bool enabled);
void mseb_range_option_callback(int range);
void mseb_temperature_option_callback(int value);
void mseb_bass_extension_option_callback(int value);
void mseb_bass_texture_option_callback(int value);
void mseb_note_thickness_option_callback(int value);
void mseb_vocals_option_callback(int value);
void mseb_female_overtones_option_callback(int value);
void mseb_sibilance_lf_option_callback(int value);
void mseb_sibilance_hf_option_callback(int value);
void mseb_impulse_response_option_callback(int value);
void mseb_air_option_callback(int value);

#endif /* _MSEB_MENU_H */
