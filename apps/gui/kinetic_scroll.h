/***************************************************************************
 *             __________               __________
 *   Open      \______   \ ____   ____ |  | _\____   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/
 *
 * Copyright (C) 2022 Aidan MacDonald
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

#ifndef KINETIC_SCROLL_H
#define KINETIC_SCROLL_H

#include <stdbool.h>
#include <stdint.h>

#define KINETIC_SCROLL_FRACBITS 8
#define KINETIC_SCROLL_TICKS_PER_SECOND 100

struct kinetic_scroll_settings
{
    /* Coefficients for a1*x + a0, in Q8 fixed point. */
    int32_t a0;
    int32_t a1;

    /* Grace period, in Rockbox ticks, before the force is applied. */
    int32_t delay;
};

struct kinetic_scroll_state
{
    /* Signed velocity in Q8 pixels/second. */
    int32_t velocity;

    /* Positive Q8 fractional-pixel remainder. */
    int32_t distance;

    int32_t scroll_duration;
    int32_t press_duration;

    /*
     * Once a finger goes down during motion, that entire touch gesture is
     * non-selecting even if braking or an edge stops motion before release.
     */
    bool suppress_selection;
};

extern const struct kinetic_scroll_settings kinetic_scroll_accel_default;
extern const struct kinetic_scroll_settings kinetic_scroll_brake_default;
extern const struct kinetic_scroll_settings kinetic_scroll_decel_default;
extern const struct kinetic_scroll_settings kinetic_scroll_press_default;

void kinetic_scroll_reset(struct kinetic_scroll_state *state);
void kinetic_scroll_set_press_duration(struct kinetic_scroll_state *state,
                                       int32_t duration);
void kinetic_scroll_touch_down(struct kinetic_scroll_state *state);
void kinetic_scroll_touch_release(struct kinetic_scroll_state *state);
bool kinetic_scroll_selection_suppressed(
    const struct kinetic_scroll_state *state);
bool kinetic_scroll_active(const struct kinetic_scroll_state *state);

/*
 * Add a release velocity in integer pixels/second. Repeated swipes receive an
 * extra impulse; a swipe against the current direction receives braking.
 */
bool kinetic_scroll_add_swipe(
    struct kinetic_scroll_state *state,
    int32_t velocity,
    const struct kinetic_scroll_settings *accel,
    const struct kinetic_scroll_settings *brake);

/*
 * Advance by interval_ticks and return the signed number of whole pixels to
 * scroll. Fractional pixels remain accumulated in state.
 */
int kinetic_scroll_step(
    struct kinetic_scroll_state *state,
    int32_t interval_ticks,
    int32_t ticks_per_second,
    const struct kinetic_scroll_settings *decel,
    const struct kinetic_scroll_settings *press);

/*
 * Stop motion if the list clamped any non-zero requested distance. This is
 * deliberately separate from reset so touch-selection suppression survives
 * until the physical release.
 */
void kinetic_scroll_report_motion(struct kinetic_scroll_state *state,
                                  int requested_pixels, int actual_pixels);

#endif /* KINETIC_SCROLL_H */
