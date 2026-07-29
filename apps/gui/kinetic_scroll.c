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

#ifndef KINETIC_SCROLL_HOST_TEST
#include "config.h"
#endif

#include "kinetic_scroll.h"

#include <limits.h>

#define KINETIC_SCROLL_ONE (1 << KINETIC_SCROLL_FRACBITS)
#define KINETIC_SCROLL_MAX_VELOCITY (32767 << KINETIC_SCROLL_FRACBITS)

#ifndef KINETIC_SCROLL_ACCEL_A0_Q8
#define KINETIC_SCROLL_ACCEL_A0_Q8 (200 << KINETIC_SCROLL_FRACBITS)
#endif
#ifndef KINETIC_SCROLL_BRAKE_A0_Q8
#define KINETIC_SCROLL_BRAKE_A0_Q8 (200 << KINETIC_SCROLL_FRACBITS)
#endif
#ifndef KINETIC_SCROLL_DECEL_A0_Q8
#define KINETIC_SCROLL_DECEL_A0_Q8 (2000 << KINETIC_SCROLL_FRACBITS)
#endif
#ifndef KINETIC_SCROLL_PRESS_A0_Q8
#define KINETIC_SCROLL_PRESS_A0_Q8 (35000 << KINETIC_SCROLL_FRACBITS)
#endif
#ifndef KINETIC_SCROLL_STOP_VELOCITY_Q8
#define KINETIC_SCROLL_STOP_VELOCITY_Q8 (3 << KINETIC_SCROLL_FRACBITS)
#endif

/*
 * These coefficients originate in kinetic scrolling v3. The forces are
 * expressed in pixels/second and therefore remain valid at the AP80's 50 Hz
 * update rate; the integration interval is supplied separately.
 */
const struct kinetic_scroll_settings kinetic_scroll_accel_default = {
    .a0 = KINETIC_SCROLL_ACCEL_A0_Q8,
    .a1 = (6 << KINETIC_SCROLL_FRACBITS) / 5,
    .delay = 0,
};

const struct kinetic_scroll_settings kinetic_scroll_brake_default = {
    .a0 = KINETIC_SCROLL_BRAKE_A0_Q8,
    .a1 = (1 << KINETIC_SCROLL_FRACBITS) / 2,
    .delay = 0,
};

const struct kinetic_scroll_settings kinetic_scroll_decel_default = {
    .a0 = KINETIC_SCROLL_DECEL_A0_Q8,
    .a1 = (1 << KINETIC_SCROLL_FRACBITS) / 2,
    .delay = 125 * KINETIC_SCROLL_TICKS_PER_SECOND / 1000,
};

const struct kinetic_scroll_settings kinetic_scroll_press_default = {
    .a0 = KINETIC_SCROLL_PRESS_A0_Q8,
    .a1 = 4 << KINETIC_SCROLL_FRACBITS,
    .delay = 250 * KINETIC_SCROLL_TICKS_PER_SECOND / 1000,
};

static int32_t clamp_i64(int64_t value, int32_t lower, int32_t upper)
{
    if (value < lower)
        return lower;
    if (value > upper)
        return upper;
    return (int32_t)value;
}

static int32_t saturating_add(int32_t a, int32_t b)
{
    return clamp_i64((int64_t)a + b, INT32_MIN, INT32_MAX);
}

static int32_t abs_velocity(int32_t value)
{
    /* All externally supplied velocities are clamped before reaching here. */
    return value < 0 ? -value : value;
}

static int direction(int32_t value)
{
    return value < 0 ? -1 : 1;
}

static int32_t fixed_mul(int32_t a, int32_t b)
{
    return clamp_i64(((int64_t)a * b) >> KINETIC_SCROLL_FRACBITS,
                     INT32_MIN, INT32_MAX);
}

static int32_t scale_by_interval(int32_t value, int32_t interval_ticks,
                                 int32_t ticks_per_second)
{
    if (interval_ticks <= 0 || ticks_per_second <= 0)
        return 0;

    return clamp_i64((int64_t)value * interval_ticks / ticks_per_second,
                     INT32_MIN, INT32_MAX);
}

static int32_t calculate_force(
    int32_t input,
    int32_t duration,
    const struct kinetic_scroll_settings *settings)
{
    if (duration < settings->delay)
        return 0;

    int32_t magnitude = abs_velocity(input);
    int32_t force = saturating_add(fixed_mul(settings->a1, magnitude),
                                   settings->a0);
    force = saturating_add(force, KINETIC_SCROLL_ONE / 2);

    return direction(input) < 0 ? -force : force;
}

static void stop_motion(struct kinetic_scroll_state *state)
{
    state->velocity = 0;
    state->distance = 0;
    state->scroll_duration = 0;
}

void kinetic_scroll_reset(struct kinetic_scroll_state *state)
{
    stop_motion(state);
    state->press_duration = -1;
    state->suppress_selection = false;
}

void kinetic_scroll_set_press_duration(struct kinetic_scroll_state *state,
                                       int32_t duration)
{
    state->press_duration = duration;
}

void kinetic_scroll_touch_down(struct kinetic_scroll_state *state)
{
    if (kinetic_scroll_active(state))
        state->suppress_selection = true;
}

void kinetic_scroll_touch_release(struct kinetic_scroll_state *state)
{
    state->press_duration = -1;
    state->suppress_selection = false;
}

bool kinetic_scroll_selection_suppressed(
    const struct kinetic_scroll_state *state)
{
    return state->suppress_selection;
}

bool kinetic_scroll_active(const struct kinetic_scroll_state *state)
{
    return state->velocity != 0;
}

bool kinetic_scroll_add_swipe(
    struct kinetic_scroll_state *state,
    int32_t velocity,
    const struct kinetic_scroll_settings *accel,
    const struct kinetic_scroll_settings *brake)
{
    if (velocity == 0)
        return false;

    velocity = clamp_i64(velocity, -32767, 32767);
    int32_t swipe_velocity = velocity * KINETIC_SCROLL_ONE;
    int old_direction = direction(state->velocity);
    bool was_active = kinetic_scroll_active(state);

    if (was_active)
    {
        int32_t impulse;
        if (direction(velocity) == old_direction)
            impulse = calculate_force(state->velocity, INT32_MAX, accel);
        else
            impulse = -calculate_force(state->velocity, INT32_MAX, brake);

        swipe_velocity = saturating_add(swipe_velocity, impulse);
    }

    state->velocity = clamp_i64((int64_t)state->velocity + swipe_velocity,
                                -KINETIC_SCROLL_MAX_VELOCITY,
                                KINETIC_SCROLL_MAX_VELOCITY);

    if (!was_active || old_direction != direction(state->velocity))
    {
        state->distance = 0;
        state->scroll_duration = 0;
    }

    return kinetic_scroll_active(state);
}

int kinetic_scroll_step(
    struct kinetic_scroll_state *state,
    int32_t interval_ticks,
    int32_t ticks_per_second,
    const struct kinetic_scroll_settings *decel,
    const struct kinetic_scroll_settings *press)
{
    if (!kinetic_scroll_active(state) ||
        interval_ticks <= 0 || ticks_per_second <= 0)
        return 0;

    int velocity_direction = direction(state->velocity);
    int32_t magnitude = abs_velocity(state->velocity);

    state->distance = saturating_add(
        state->distance,
        scale_by_interval(magnitude, interval_ticks, ticks_per_second));
    state->scroll_duration =
        saturating_add(state->scroll_duration, interval_ticks);

    int32_t pixel_diff = state->distance >> KINETIC_SCROLL_FRACBITS;
    state->distance -= pixel_diff << KINETIC_SCROLL_FRACBITS;

    int32_t force = calculate_force(magnitude, state->scroll_duration, decel);
    force = saturating_add(
        force, calculate_force(magnitude, state->press_duration, press));
    int32_t velocity_delta =
        scale_by_interval(force, interval_ticks, ticks_per_second);

    if (magnitude < KINETIC_SCROLL_STOP_VELOCITY_Q8)
        stop_motion(state);
    else if (velocity_delta > 0 && magnitude < velocity_delta)
        state->velocity /= 3;
    else
        state->velocity = saturating_add(
            state->velocity, -velocity_direction * velocity_delta);

    return velocity_direction * pixel_diff;
}

void kinetic_scroll_report_motion(struct kinetic_scroll_state *state,
                                  int requested_pixels, int actual_pixels)
{
    if (requested_pixels != 0 && requested_pixels != actual_pixels)
        stop_motion(state);
}
