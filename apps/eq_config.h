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
#ifndef _EQ_CONFIG_H
#define _EQ_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include "eq.h"

/* Parse the canonical "cutoff, q, gain, TYPE" representation. The destination
 * is changed only when the complete value is valid. */
bool eq_config_parse(const char *value, struct eq_band_setting *setting);

/* Parse Rockbox's historical three-number representation and attach the
 * filter type implied by the legacy setting name. */
bool eq_config_parse_legacy(const char *value, enum eq_filter_type type,
                            struct eq_band_setting *setting);

/* Format the canonical representation. Returns false if the destination is
 * too small or the arguments are invalid. */
bool eq_config_format(const struct eq_band_setting *setting,
                      char *buffer, size_t buffer_size);

bool eq_filter_type_is_valid(enum eq_filter_type type);
const char *eq_filter_type_config_name(enum eq_filter_type type);

#endif /* _EQ_CONFIG_H */
