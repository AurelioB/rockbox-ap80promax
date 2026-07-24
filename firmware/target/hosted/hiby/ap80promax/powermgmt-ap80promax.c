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
 * Initial battery calibration for the Hidizs AP80 Pro Max.
 *
 * The firmware uses a CW2015 fuel gauge and an AXP2101 charger capped at
 * 4.35 V. These curves are conservative bring-up values and should be
 * refined from on-device discharge logs.
 ****************************************************************************/
#include "powermgmt.h"
#include "power.h"

unsigned short battery_level_disksafe = 3470;
unsigned short battery_level_shutoff = 3400;

unsigned short percent_to_volt_discharge[11] =
{
    3400, 3675, 3715, 3750, 3775, 3810, 3850, 4028, 4125, 4237, 4350
};

unsigned short percent_to_volt_charge[11] =
{
    3485, 3780, 3836, 3857, 3890, 3930, 3986, 4028, 4125, 4237, 4350
};
