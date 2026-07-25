/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 *
 * Copyright (c) 2018 Marcin Bukat
 * Copyright (c) 2025 Solomon Peachy
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

//#define LOGF_ENABLE

#include "config.h"
#include "audio.h"
#include "audiohw.h"
#include "button.h"
#include "system.h"
#include "kernel.h"
#include "panic.h"
#include "sysfs.h"
#include "alsa-controls.h"
#include "pcm-alsa.h"

#include "logf.h"

int hiby_has_valid_output(void);

static int hw_init = 0;

static long int vol_l_hw = 255;
static long int vol_r_hw = 255;
static long int last_ps = -1;

static int muted = -1;
#ifdef HAVE_HIBY_OUTPUT_SELECT
static int output_mode = HIBY_OUTPUT_AUTO;
#endif

void audiohw_mute(int mute)
{
    if (hw_init < 0 || muted == mute)
        return;

    muted = mute;

    alsa_controls_set_bool("Mute Output", !!mute);
}

int hiby_has_valid_output(void) {
    long int ps = 0; // Muted, if nothing is plugged in!

    int status = 0;

    if (!hw_init) return ps;

    const char * const sysfs_hs_switch = "/sys/class/switch/headset/state";
    const char * const sysfs_bal_switch = "/sys/class/switch/balance/state";

    if (sysfs_get_int(sysfs_hs_switch, &status) && status)
        ps = 2; // headset

    if (sysfs_get_int(sysfs_bal_switch, &status) && status)
        ps = 3; // balanced output

    return ps;
}

int hiby_get_outputs(void){
    long int ps = hiby_has_valid_output();

    hiby_set_output(ps);

    return ps;
}

static void set_output_port(int ps)
{
    if (last_ps == ps)
        return;

    logf("set out %d/%d", ps, last_ps);
    last_ps = ps;
    alsa_controls_set_ints("Output Port Switch", 1, &last_ps);
}

void hiby_set_output(int ps)
{
    if (!hw_init || muted) return;

#ifdef HAVE_HIBY_OUTPUT_SELECT
    /* Jack events must not override an explicitly selected fixed route. */
    if (output_mode != HIBY_OUTPUT_AUTO)
        return;
#endif

    // Default to headset if nothing was ever inserted; otherwise, R3 Pro II crashes on playback
    if (ps == 0)
    {
        ps = last_ps > 0 ? last_ps : 2;
    }

    if (last_ps != ps)
    {
        set_output_port(ps);
        audiohw_set_volume(vol_l_hw, vol_r_hw);
    }
}

#ifdef HAVE_HIBY_OUTPUT_SELECT
int audiohw_set_output_mode(int mode)
{
    const char *device;
    int old_mode = output_mode;
    int old_port = last_ps;
    int port;

    if (mode < HIBY_OUTPUT_AUTO || mode >= HIBY_OUTPUT_NUM_MODES)
        return old_mode;
    if (!hw_init)
    {
        output_mode = mode;
        return mode;
    }
    if (mode == old_mode)
        return mode;

    /*
     * The stock player maps line-out/headset/balanced/S/PDIF to values
     * 1/2/3/4. Mute around both the route and ALSA endpoint transition.
     */
    audiohw_mute(true);
    switch (mode)
    {
        case HIBY_OUTPUT_LINEOUT:
            port = 1;
            device = HIBY_ANALOG_PLAYBACK_DEVICE;
            break;
        case HIBY_OUTPUT_SPDIF:
            port = 4;
            device = HIBY_SPDIF_PLAYBACK_DEVICE;
            break;
        case HIBY_OUTPUT_AUTO:
        default:
            port = hiby_has_valid_output();
            if (port == 0)
                port = 2;
            device = HIBY_ANALOG_PLAYBACK_DEVICE;
            break;
    }

    /*
     * Select the matching mixer route before the backend restarts playback.
     * If opening/configuring the new endpoint fails, restore the old route
     * and leave the setting on the last working mode.
     */
    set_output_port(port);
    if (!pcm_alsa_switch_playback_device(device))
    {
        set_output_port(old_port > 0 ? old_port : 2);
        audiohw_mute(false);
        return old_mode;
    }

    output_mode = mode;
    audiohw_set_volume(vol_l_hw, vol_r_hw);
    audiohw_mute(false);
    return mode;
}
#endif

void audiohw_preinit(void)
{
    logf("hw preinit");
    alsa_controls_init("default");
    hw_init = 1;

    audiohw_mute(false);  /* No need ? */
    alsa_controls_set_bool("DOP_EN", 0); //isDSD
#if defined(HAVE_ALSA_32BIT)
    /* S16 samples are expanded into S32 ALSA slots; zero dB is unity. */
    pcm_set_mixer_volume(0, 0);
#endif
}

void audiohw_postinit(void)
{
    logf("hw postinit");
}

#ifdef HAVE_DSD
void audiohw_set_dsd_mode(bool dop)
{
    if (hw_init > 0)
        alsa_controls_set_bool("DOP_EN", dop);
}
#endif

void audiohw_close(void)
{
    logf("hw close");
    hw_init = 0;
    alsa_controls_close();
}

void audiohw_set_frequency(int fsel)
{
    (void)fsel;
}

void audiohw_set_volume(int vol_l, int vol_r)
{
    logf("hw vol %d %d", vol_l, vol_r);

    long l,r;

    vol_l_hw = vol_l;
    vol_r_hw = vol_r;

#ifdef HAVE_HIBY_OUTPUT_SELECT
    if (output_mode == HIBY_OUTPUT_LINEOUT)
    {
        /* Stock LO_VOLUME_INDEX is 100, corresponding to fixed 0 dB. */
        l = 0;
        r = 0;
    }
    else
#endif
    {
        l = -vol_l/5;
        r = -vol_r/5;
    }

    if (!hw_init)
        return;

    alsa_controls_set_ints("Left Playback Volume", 1, &l);
    alsa_controls_set_ints("Right Playback Volume", 1, &r);
}

void audiohw_set_filter_roll_off(int value)
{
    logf("rolloff %d", value);
    /* 0 = Sharp;
     *       1 = Slow;
     *       2 = Short Sharp
     *       3 = Short Slow
     *       4 = Super Slow */
    long int value_hw = value;
    alsa_controls_set_ints("Digital Filter", 1, &value_hw);

}

#ifdef HAVE_RECORDING
void audiohw_set_recvol(int left, int right, int type)
{
    /*
     * The AP80 stock recorder does not expose a microphone gain ALSA control.
     * Capture gain is owned by the vendor digital-microphone path, so do not
     * invent a mixer control or write an unverified codec register.
     */
    (void)left;
    (void)right;
    (void)type;
}
#endif
