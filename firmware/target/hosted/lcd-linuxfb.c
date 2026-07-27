/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2017 Marcin Bukat
 * Copyright (C) 2016 Amaury Pouly
 * Copyright (C) 2019 Roman Stolyarov
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "lcd.h"
#include "lcd-target.h"
#include "backlight-target.h"
#include "sysfs.h"
#include "panic.h"

//#define FB_DOUBLEBUF

static int fd = -1;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
fb_data *framebuffer = NULL; /* global variable, see lcd-target.h */
unsigned int lcd_framebuffer_stride = LCD_WIDTH;

#ifndef LCD_FRAMEBUFFER_STRIDE
#define LCD_FRAMEBUFFER_STRIDE LCD_WIDTH
#endif
#ifndef LCD_FBDEV_XOFFSET
#define LCD_FBDEV_XOFFSET 0
#endif

#ifdef FB_DOUBLEBUF
static int fb_plane;
int doublebuf;
#else
#define fb_plane 0
#endif

static void redraw(void)
{
#ifdef FB_DOUBLEBUF
    if (doublebuf) {
        vinfo.yoffset = fb_plane * LCD_HEIGHT;
        fb_plane = !fb_plane;
        vinfo.activate = FB_ACTIVATE_VBL;
    }
#endif

    ioctl(fd, FBIOPAN_DISPLAY, &vinfo);
}

void lcd_init_device(void)
{
    const char * const fb_dev = "/dev/fb0";
    fd = open(fb_dev, O_RDWR | O_CLOEXEC);
    if(fd < 0)
    {
        panicf("Cannot open framebuffer: %s\n", fb_dev);
    }

    /* get fixed information */
    if(ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0)
    {
        panicf("Cannot read framebuffer fixed information");
    }

    if (finfo.smem_len < FRAMEBUFFER_SIZE)
        panicf("FRAMEBUFFER_SIZE too large for hardware? (%u vs %u)", FRAMEBUFFER_SIZE, finfo.smem_len);
#ifdef FB_DOUBLEBUF
    if (finfo.smem_len >= 2*FRAMEBUFFER_SIZE)
        doublebuf = 1;
    fb_plane = 0;
#endif

    /* get variable information */
    if(ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
    {
        panicf("Cannot read framebuffer variable information");
    }

#ifdef FB_DOUBLEBUF
    if (doublebuf) {
        vinfo.yres_virtual = vinfo.yres * 2;
        if(ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
            panicf("Cannot write framebuffer variable information");
        }
    }

#if 0
    /* check resolution and framebuffer size */
    if(vinfo.xres != LCD_WIDTH || vinfo.yres != LCD_HEIGHT || vinfo.bits_per_pixel != LCD_DEPTH)
    {
        panicf("Unexpected framebuffer resolution: %dx%dx%d\n", vinfo.xres,
            vinfo.yres, vinfo.bits_per_pixel);
    }
#endif
#else
#if defined(LCD_FBDEV_WIDTH) && defined(LCD_FBDEV_HEIGHT)
    /*
     * Some hosted targets expose a scanout wider than their logical panel.
     * Do not attempt to reprogram that mode: the AP80 display driver can
     * block inside FBIOPUT_VSCREENINFO when asked for its logical width.
     */
    if (vinfo.bits_per_pixel != LCD_DEPTH ||
        vinfo.xres != LCD_FBDEV_WIDTH ||
        vinfo.yres != LCD_FBDEV_HEIGHT)
    {
        panicf("Unexpected framebuffer mode: %dx%dx%d",
               vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
    }
#else
    /* Make sure we match our desired geometry and bitdepth. */
    if (vinfo.bits_per_pixel != LCD_DEPTH || vinfo.xres != LCD_WIDTH || vinfo.yres != LCD_HEIGHT) {
        vinfo.bits_per_pixel = LCD_DEPTH;
        vinfo.xres = LCD_WIDTH;
        vinfo.yres = LCD_HEIGHT;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &vinfo)) {
            panicf("Cannot set framebuffer to %dx%dx%d",
               vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);
        }
    }
#endif
#endif

    if (finfo.line_length % sizeof(fb_data) != 0)
        panicf("Framebuffer stride is not pixel aligned");

    lcd_framebuffer_stride = finfo.line_length / sizeof(fb_data);
    if (lcd_framebuffer_stride < LCD_FBDEV_XOFFSET + LCD_WIDTH)
        panicf("Framebuffer stride is too small: %u", lcd_framebuffer_stride);

    /* map framebuffer */
    framebuffer = mmap(NULL, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if((void *)framebuffer == MAP_FAILED)
    {
        panicf("Cannot map framebuffer");
    }

#if !defined(HIDIZS_AP80_PRO_MAX) || defined(BOOTLOADER)
    memset(framebuffer, 0, finfo.smem_len);
#endif

#ifdef HAVE_LCD_ENABLE
    lcd_set_active(true);
#endif
}

#ifdef HAVE_LCD_SHUTDOWN
void lcd_shutdown(void)
{
    munmap(framebuffer, finfo.smem_len);
    framebuffer = NULL;
    close(fd);
    fd = -1;
}
#endif

#ifdef HAVE_LCD_ENABLE
void lcd_enable(bool on)
{
    if (fd < 0) return;

    if (lcd_active() == on)
        return;

    lcd_set_active(on);

    if (on) {
        send_event(LCD_EVENT_ACTIVATION, NULL);
        ioctl(fd, FB_BLANK_UNBLANK);
    } else {
        memset(framebuffer, 0, finfo.smem_len);
        redraw();
        ioctl(fd, FB_BLANK_POWERDOWN);
    }
}
#endif

extern void lcd_copy_buffer_rect(fb_data *dst, const fb_data *src,
                                 int width, int height);

static void copy_to_framebuffer(fb_data *dst, const fb_data *src,
                                int width, int height)
{
    if (LCD_FRAMEBUFFER_STRIDE == LCD_WIDTH && width == LCD_WIDTH)
    {
        lcd_copy_buffer_rect(dst, src, width * height, 1);
        return;
    }

    while (height-- > 0)
    {
        lcd_copy_buffer_rect(dst, src, width, 1);
        dst += LCD_FRAMEBUFFER_STRIDE;
        src += LCD_WIDTH;
    }
}

void lcd_update(void)
{
    if (fd < 0) return;

#ifdef HAVE_LCD_ENABLE
    if (lcd_active())
#endif
    {
        /* Copy the Rockbox framebuffer to the second framebuffer */
        fb_data *dst = LCD_FRAMEBUF_ADDR(0, 0) + (fb_plane * FRAMEBUFFER_SIZE);
        copy_to_framebuffer(dst, FBADDR(0,0), LCD_WIDTH, LCD_HEIGHT);
        redraw();
    }
}

void lcd_update_rect(int x, int y, int width, int height)
{
    if (fd < 0) return;

#ifdef HAVE_LCD_ENABLE
    if (lcd_active())
#endif
    {
        fb_data *dst = LCD_FRAMEBUF_ADDR(x, y) + (fb_plane * FRAMEBUFFER_SIZE);
        fb_data *src = FBADDR(x,y);

        copy_to_framebuffer(dst, src, width, height);
        redraw();
    }
}
