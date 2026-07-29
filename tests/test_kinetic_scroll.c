#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kinetic_scroll.h"

#define AP80_INTERVAL_TICKS 2
#define TEST_HZ KINETIC_SCROLL_TICKS_PER_SECOND
#define FP(value) ((value) << KINETIC_SCROLL_FRACBITS)

static struct kinetic_scroll_state new_state(void)
{
    struct kinetic_scroll_state state;
    kinetic_scroll_reset(&state);
    return state;
}

static int step_default(struct kinetic_scroll_state *state)
{
    return kinetic_scroll_step(state, AP80_INTERVAL_TICKS, TEST_HZ,
                               &kinetic_scroll_decel_default,
                               &kinetic_scroll_press_default);
}

static void test_subpixel_motion_accumulates(void)
{
    const struct kinetic_scroll_settings no_force = {0, 0, INT32_MAX};
    struct kinetic_scroll_state state = new_state();
    state.velocity = FP(25);

    const int expected[] = {0, 1, 0, 1};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
    {
        int pixels = kinetic_scroll_step(
            &state, AP80_INTERVAL_TICKS, TEST_HZ, &no_force, &no_force);
        assert(pixels == expected[i]);
        assert(state.distance == (i % 2 == 0 ? 128 : 0));
        kinetic_scroll_report_motion(&state, pixels, pixels);
    }

    assert(kinetic_scroll_active(&state));

    kinetic_scroll_report_motion(&state, 0, 0);
    assert(kinetic_scroll_active(&state));
}

static void test_default_deceleration_stops_monotonically(void)
{
    struct kinetic_scroll_state state = new_state();
    assert(kinetic_scroll_add_swipe(&state, 1800,
                                    &kinetic_scroll_accel_default,
                                    &kinetic_scroll_brake_default));

    int32_t previous = state.velocity;
    int iterations = 0;
    while (kinetic_scroll_active(&state) && iterations++ < 1000)
    {
        step_default(&state);
        assert(state.velocity >= 0);
        assert(state.velocity <= previous);
        previous = state.velocity;
    }

    assert(iterations < 1000);
    assert(!kinetic_scroll_active(&state));
}

static void test_repeated_swipe_accelerates(void)
{
    struct kinetic_scroll_state state = new_state();
    kinetic_scroll_add_swipe(&state, 900,
                             &kinetic_scroll_accel_default,
                             &kinetic_scroll_brake_default);
    int32_t first = state.velocity;

    kinetic_scroll_add_swipe(&state, 900,
                             &kinetic_scroll_accel_default,
                             &kinetic_scroll_brake_default);

    assert(state.velocity > first + FP(900));
}

static void test_reverse_swipe_brakes_and_resets_fraction(void)
{
    struct kinetic_scroll_state state = new_state();
    kinetic_scroll_add_swipe(&state, 2000,
                             &kinetic_scroll_accel_default,
                             &kinetic_scroll_brake_default);
    state.distance = 123;
    state.scroll_duration = 40;

    int32_t before = state.velocity;
    kinetic_scroll_add_swipe(&state, -300,
                             &kinetic_scroll_accel_default,
                             &kinetic_scroll_brake_default);
    assert(state.velocity < before);

    kinetic_scroll_add_swipe(&state, -3000,
                             &kinetic_scroll_accel_default,
                             &kinetic_scroll_brake_default);
    assert(state.velocity < 0);
    assert(state.distance == 0);
    assert(state.scroll_duration == 0);
}

static void test_press_force_observes_grace_period(void)
{
    struct kinetic_scroll_state free_state = new_state();
    struct kinetic_scroll_state pressed_state = new_state();
    free_state.velocity = pressed_state.velocity = FP(1500);
    free_state.scroll_duration = pressed_state.scroll_duration = 40;

    kinetic_scroll_set_press_duration(&pressed_state,
                                      kinetic_scroll_press_default.delay - 1);
    step_default(&free_state);
    step_default(&pressed_state);
    assert(pressed_state.velocity == free_state.velocity);

    free_state.velocity = pressed_state.velocity = FP(1500);
    free_state.distance = pressed_state.distance = 0;
    free_state.scroll_duration = pressed_state.scroll_duration = 40;
    kinetic_scroll_set_press_duration(&pressed_state,
                                      kinetic_scroll_press_default.delay);
    step_default(&free_state);
    step_default(&pressed_state);
    assert(pressed_state.velocity < free_state.velocity);
}

static void test_partial_clamp_stops_motion_but_preserves_suppression(void)
{
    struct kinetic_scroll_state state = new_state();
    state.velocity = FP(1000);
    kinetic_scroll_touch_down(&state);
    assert(kinetic_scroll_selection_suppressed(&state));

    kinetic_scroll_report_motion(&state, 12, 12);
    assert(kinetic_scroll_active(&state));

    kinetic_scroll_report_motion(&state, 12, 3);
    assert(!kinetic_scroll_active(&state));
    assert(kinetic_scroll_selection_suppressed(&state));

    kinetic_scroll_touch_release(&state);
    assert(!kinetic_scroll_selection_suppressed(&state));
}

static void test_natural_stop_keeps_tap_suppressed_until_release(void)
{
    struct kinetic_scroll_state state = new_state();
    state.velocity = FP(4);
    kinetic_scroll_touch_down(&state);
    assert(kinetic_scroll_selection_suppressed(&state));

    step_default(&state);
    assert(!kinetic_scroll_active(&state));
    assert(kinetic_scroll_selection_suppressed(&state));

    kinetic_scroll_touch_release(&state);
    assert(!kinetic_scroll_selection_suppressed(&state));
}

static void test_touch_lifecycle_and_zero_swipe(void)
{
    struct kinetic_scroll_state state = new_state();
    kinetic_scroll_touch_down(&state);
    assert(!kinetic_scroll_selection_suppressed(&state));
    assert(!kinetic_scroll_add_swipe(&state, 0,
                                     &kinetic_scroll_accel_default,
                                     &kinetic_scroll_brake_default));

    state.velocity = -FP(800);
    kinetic_scroll_touch_down(&state);
    assert(kinetic_scroll_selection_suppressed(&state));
    kinetic_scroll_reset(&state);
    assert(!kinetic_scroll_active(&state));
    assert(!kinetic_scroll_selection_suppressed(&state));
    assert(state.press_duration == -1);
}

static void test_velocity_is_saturated(void)
{
    struct kinetic_scroll_state state = new_state();
    for (int i = 0; i < 100; ++i)
        kinetic_scroll_add_swipe(&state, INT32_MAX,
                                 &kinetic_scroll_accel_default,
                                 &kinetic_scroll_brake_default);

    assert(state.velocity > 0);
    assert(state.velocity <= FP(32767));
}

int main(void)
{
    test_subpixel_motion_accumulates();
    test_default_deceleration_stops_monotonically();
    test_repeated_swipe_accelerates();
    test_reverse_swipe_brakes_and_resets_fraction();
    test_press_force_observes_grace_period();
    test_partial_clamp_stops_motion_but_preserves_suppression();
    test_natural_stop_keeps_tap_suppressed_until_release();
    test_touch_lifecycle_and_zero_swipe();
    test_velocity_is_saturated();

    puts("kinetic scroll tests passed");
    return 0;
}
