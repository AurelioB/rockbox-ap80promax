/***************************************************************************
 *             __________               __   ___
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2017 Marcin Bukat
 * Copyright (C) 2020 Solomon Peachy

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
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/input.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#ifdef BUTTON_DEVINPUT_READ_INITIAL_STATE
#include <sys/ioctl.h>
#endif

#include "kernel.h"
#include "sysfs.h"
#include "button.h"
#include "panic.h"

#ifdef HAVE_SCROLLWHEEL
#include "powermgmt.h"
#if defined(HAVE_BACKLIGHT) || defined(HAVE_BUTTON_LIGHT)
#include "backlight.h"
#endif
#endif /* HAVE_SCROLLWHEEL */

/* TODO:  HAVE_SCROLLWHEEL is a hack.  Instead of posting the exact number
   of clicks, instead do it similar to the ipod clickwheel and post
   the wheel angular velocity (degrees per second)

   * Track the relative position  (ie based on click events)
   * Use WHEELCLICKS_PER_ROTATION to convert clicks to angular distance
   * Compute to angular velocity (degrees per second)

 */
#ifndef BUTTON_DEVINPUT_COUNT
#define BUTTON_DEVINPUT_COUNT 5
#endif

#define NR_POLL_DESC BUTTON_DEVINPUT_COUNT

static int num_devices = 0;
static struct pollfd poll_fds[NR_POLL_DESC];
static int button_bitmap = 0;

#ifdef BUTTON_DEVINPUT_READ_INITIAL_STATE
#define DEVINPUT_BITS_PER_LONG (sizeof(unsigned long) * 8)
#define DEVINPUT_KEY_LONGS \
    ((KEY_MAX + DEVINPUT_BITS_PER_LONG) / DEVINPUT_BITS_PER_LONG)

static void button_read_initial_state(int fd)
{
    unsigned long key_bits[DEVINPUT_KEY_LONGS];
    memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(fd, EVIOCGKEY(sizeof(key_bits)), key_bits) < 0)
        return;

    for (int key = 0; key <= KEY_MAX; ++key)
    {
        if (!(key_bits[key / DEVINPUT_BITS_PER_LONG] &
              (1ul << (key % DEVINPUT_BITS_PER_LONG))))
            continue;

        int mapped = button_map(key);
#ifdef HAVE_SCROLLWHEEL
        /*
         * Wheel keys are momentary events. Their release path deliberately
         * does not mutate button_bitmap, so never seed them as held.
         */
        mapped &= ~(BUTTON_SCROLL_BACK | BUTTON_SCROLL_FWD);
#endif
        button_bitmap |= mapped;
    }
}
#endif

void button_add_input_device(int i)
{
    int fd = poll_fds[i].fd;
    if (fd >= 0)
        close(fd);

    char path[32];
    snprintf(path, sizeof(path), "/dev/input/event%d", i);
    fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    poll_fds[i].fd = fd >= 0 ? fd : -1;
    if(fd >= 0)
    {
        poll_fds[i].events = POLLIN;
        poll_fds[i].revents = 0;
        if (num_devices <= i)
            num_devices = i + 1;
#ifdef BUTTON_DEVINPUT_READ_INITIAL_STATE
        button_read_initial_state(fd);
#endif
    }
}

void button_init_device(void)
{
    button_bitmap = 0;
    for(int i = 0; i < NR_POLL_DESC; i++)
    {
        poll_fds[i].fd = -1;
        button_add_input_device(i);
    }
}

void button_remove_input_device(int i)
{
    int fd = poll_fds[i].fd;
    if (fd < 0)
        return;

    if (i == num_devices - 1)
        num_devices = i;

    close(fd);
    poll_fds[i].fd = -1;
}

void button_close_device(void)
{
    num_devices = 0;
    /* close descriptors */
    for(int i = 0; i < NR_POLL_DESC; i++)
    {
        button_remove_input_device(i);
    }
}

#ifdef BUTTON_DELAY_RELEASE
static int button_delay_release = 0;
static int delay_tick = 0;
#endif

#ifdef HAVE_TOUCHSCREEN
/* Last known touchscreen coordinates. */
static int _last_x = 0;
static int _last_y = 0;

/* Last known touchscreen state. */
static enum
{
    TOUCHSCREEN_STATE_UNKNOWN = 0,
    TOUCHSCREEN_STATE_UP,
    TOUCHSCREEN_STATE_DOWN
} _last_touch_state = TOUCHSCREEN_STATE_UNKNOWN;

/* Do _NOT_ change this.  Unofficial Linux API */
#define EVENT_VALUE_TOUCHSCREEN_RELEASE  -1
/* This is only used as an arbitrary placeholder */
#define EVENT_VALUE_TOUCHSCREEN_PRESS    1

static int ts_enabled = 1;

#if defined(TOUCHSCREEN_RAW_WIDTH) || defined(TOUCHSCREEN_RAW_HEIGHT)
static int touchscreen_scale_coordinate(int value, int raw_size, int lcd_size)
{
    if (value <= 0)
        return 0;

    value = value * lcd_size / raw_size;
    return value < lcd_size ? value : lcd_size - 1;
}
#endif

void touchscreen_last_state(int *x, int *y, int *state)
{
    *x = _last_x;
    *y = _last_y;
    *state = _last_touch_state;
}

void touchscreen_enable_device(bool en)
{
    ts_enabled = en;
}

static void handle_touchscreen_event(__u16 code, __s32 value)
{
    switch(code)
    {
        case ABS_X:
        case ABS_MT_POSITION_X:
        {
#ifdef TOUCHSCREEN_RAW_WIDTH
            value = touchscreen_scale_coordinate(value, TOUCHSCREEN_RAW_WIDTH,
                                                  LCD_WIDTH);
#endif
            _last_x = value;
            break;
        }

        case ABS_Y:
        case ABS_MT_POSITION_Y:
        {
#ifdef TOUCHSCREEN_RAW_HEIGHT
            value = touchscreen_scale_coordinate(value, TOUCHSCREEN_RAW_HEIGHT,
                                                  LCD_HEIGHT);
#endif
            _last_y = value;
            break;
        }

        case ABS_MT_TRACKING_ID:
        {
            if(value == EVENT_VALUE_TOUCHSCREEN_RELEASE)
            {
                _last_touch_state = TOUCHSCREEN_STATE_UP;
            }
            else
            {
                _last_touch_state = TOUCHSCREEN_STATE_DOWN;
            }
            break;
        }
    }
}
#endif

#ifdef HAVE_BUTTON_DATA
#define BDATA int *data
#else
#define BDATA void
#endif
int button_read_device(BDATA)
{
    struct input_event event;

#if defined(HAVE_BUTTON_DATA) && !defined(HAVE_TOUCHSCREEN)
    (void)data;
#endif

#ifdef HAVE_SCROLLWHEEL
    int wheel_ticks = 0;
#endif

#ifdef BUTTON_DELAY_RELEASE
    /* First de-assert delayed-release buttons */
    if (button_delay_release && current_tick >= delay_tick)
    {
        button_bitmap &= ~button_delay_release;
        button_delay_release = 0;
    }
#endif

    /* check if there are any events pending and process them */
    while(poll(poll_fds, num_devices, 0)) {
        for(int i = 0; i < num_devices; i++) {
            /* read only if non-blocking */
            if(poll_fds[i].revents & POLLIN) {
                int size = read(poll_fds[i].fd, &event, sizeof(event));
                if(size == (int)sizeof(event)) {
                    switch(event.type) {
                    case EV_KEY: {
                        /* map linux event code to rockbox button bitmap */
                        int bmap = button_map(event.code);

                        /* event.value == 0x10000 means press
                         * event.value == 0 means release
                         */
                        if(event.value) {
#ifdef HAVE_SCROLLWHEEL
                            /* Filter out wheel ticks */
                            if (bmap & BUTTON_SCROLL_BACK)
                                wheel_ticks--;
                            else if (bmap & BUTTON_SCROLL_FWD)
                                wheel_ticks++;
                            bmap &= ~(BUTTON_SCROLL_BACK|BUTTON_SCROLL_FWD);
#endif
#ifdef BUTTON_DELAY_RELEASE
                            bmap &= ~BUTTON_DELAY_RELEASE;
#endif
#if defined(HAVE_TOUCHSCREEN) && defined(BUTTON_TOUCH)
                            /* Some touchscreens give us actual touch/untouch as a "key" */
                            if (bmap & BUTTON_TOUCH) {
                                handle_touchscreen_event(ABS_MT_TRACKING_ID, EVENT_VALUE_TOUCHSCREEN_PRESS);
                                bmap &= ~BUTTON_TOUCH;
                            }
#endif
                            button_bitmap |= bmap;
                        } else {
#if defined(HAVE_TOUCHSCREEN) && defined(BUTTON_TOUCH)
                            /* Some touchscreens give us actual touch/untouch as a "key" */
                            if (bmap & BUTTON_TOUCH) {
                                handle_touchscreen_event(ABS_MT_TRACKING_ID, EVENT_VALUE_TOUCHSCREEN_RELEASE);
                                bmap &= ~BUTTON_TOUCH;
                            }
#endif
#ifdef BUTTON_DELAY_RELEASE
                            /* Delay the release of any requested buttons */
                            if (bmap & BUTTON_DELAY_RELEASE) {
                                button_delay_release |= bmap & ~BUTTON_DELAY_RELEASE;
                                delay_tick = current_tick + HZ/20;
                                bmap = 0;
                            }
#endif
#ifdef HAVE_SCROLLWHEEL
                            /* Wheel gives us press+release back to back; ignore the release */
                            bmap &= ~(BUTTON_SCROLL_BACK|BUTTON_SCROLL_FWD);
#endif
                            button_bitmap &= ~bmap;
                        }
                        break;
                    }
#ifdef HAVE_TOUCHSCREEN
                    case EV_ABS: {
                        if (ts_enabled) {
                            handle_touchscreen_event(event.code, event.value);
                        } else {
                             /* If disabled... ignore */
                            _last_touch_state = TOUCHSCREEN_STATE_UNKNOWN;
                        }
                        break;
                    }
#endif
                    default:
                        /* Ignore other event types */
                        break;
                    }
                }
            }
            /* device was removed/disconnected — close it to stop poll returning POLLHUP forever */
            else if (poll_fds[i].revents & (POLLERR | POLLHUP)) {
                button_remove_input_device(i);
            }
        }
    }

#ifdef HAVE_SCROLLWHEEL
    /* Reset backlight and poweroff timers */
    if (wheel_ticks) {
#ifdef HAVE_BACKLIGHT
        backlight_on();
#endif
#ifdef HAVE_BUTTON_LIGHT
        buttonlight_on();
#endif
        reset_poweroff_timer();
    }

    if (wheel_ticks > 0)
    {
        while (wheel_ticks-- > 0)
        {
            button_queue_post(BUTTON_SCROLL_FWD, 0);
        }
    }
    else if (wheel_ticks < 0)
    {
        while (wheel_ticks++ < 0)
        {
            button_queue_post(BUTTON_SCROLL_BACK, 0);
        }
    }
#endif /* HAVE_SCROLLWHEEL */

#ifdef HAVE_TOUCHSCREEN
    int touch = touchscreen_to_pixels(_last_x, _last_y, data);

    if(_last_touch_state == TOUCHSCREEN_STATE_DOWN)
    {
        return button_bitmap | touch;
    }
#endif

    return button_bitmap;
}
