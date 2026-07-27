/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Copyright (C) 2010 Thomas Martitz
 * Copyright (c) 2020 Solomon Peachy
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

/*
 * Based, but heavily modified, on the example given at
 * http://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2pcm_8c-example.html
 *
 * This driver uses the so-called unsafe async callback method.
 *
 * To make the async callback safer, an alternative stack is installed, since
 * it's run from a signal hanlder (which otherwise uses the user stack).
 *
 * TODO: Rewrite this to properly use multithreading and/or direct mmap()
 */

#include "autoconf.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <alsa/asoundlib.h>

//#define LOGF_ENABLE

#include "system.h"
#include "debug.h"
#include "kernel.h"
#include "panic.h"

#include "pcm.h"
#include "pcm-internal.h"
#include "pcm_mixer.h"
#include "pcm_sampr.h"
#include "pcm_sink.h"
#include "audiohw.h"
#include "pcm-alsa.h"
#include "fixedpoint.h"

#include "logf.h"

#ifdef HAVE_DSD
#include "dsd-ap80promax.h"
#endif

#include <pthread.h>
#include <signal.h>

/* plughw:0,0 works with both, however "default" is recommended.
 * default doesnt seem to work with async callback but doesn't break
 * with multple applications running */
#ifndef DEFAULT_PLAYBACK_DEVICE
#define DEFAULT_PLAYBACK_DEVICE "plughw:0,0"
#endif
#ifndef DEFAULT_CAPTURE_DEVICE
#define DEFAULT_CAPTURE_DEVICE "default"
#endif

#if MIX_FRAME_SAMPLES < 512
#error "MIX_FRAME_SAMPLES needs to be at least 512!"
#elif MIX_FRAME_SAMPLES < 1024
#warning "MIX_FRAME_SAMPLES <1024 may cause dropouts!"
#endif

/* PCM_DC_OFFSET_VALUE is a workaround for eros q hardware quirk */
#if !defined(PCM_DC_OFFSET_VALUE)
# define PCM_DC_OFFSET_VALUE 0
#endif

static const snd_pcm_access_t access_ = SND_PCM_ACCESS_RW_INTERLEAVED; /* access mode */
#if defined(HAVE_ALSA_24BIT)
static const snd_pcm_format_t format = SND_PCM_FORMAT_S24_LE;    /* 24 valid bits in a 32-bit slot */
typedef int32_t sample_t;
#elif defined(HAVE_ALSA_32BIT)
static const snd_pcm_format_t format = SND_PCM_FORMAT_S32_LE;    /* sample format */
typedef int32_t sample_t;
#else
static const snd_pcm_format_t format = SND_PCM_FORMAT_S16;    /* sample format */
typedef int16_t sample_t;
#endif
static const int channels = 2;                                /* count of channels */
static unsigned int real_sample_rate;
static unsigned int last_sample_rate;

static snd_pcm_t *handle = NULL;
static snd_pcm_sframes_t buffer_size;
static snd_pcm_sframes_t period_size;
static void *frames = NULL;

static const void  *pcm_data = 0;
static size_t       pcm_size = 0;

static unsigned int xruns = 0;

static pthread_mutex_t pcm_mtx;
static bool pcm_mtx_initialized;
static bool cleanup_registered;

#ifdef HAVE_ALSA_POLLING
static pthread_t pcm_poll_thread;
static volatile bool pcm_poll_running;
#else
static snd_async_handler_t *ahandler = NULL;
static long signal_stack[SIGSTKSZ/sizeof(long)];
#endif

static const char *playback_dev = DEFAULT_PLAYBACK_DEVICE;

#ifdef HAVE_DSD
#define DSD_INPUT_BUFFER_SIZE 8192u
#define DSD_OUTPUT_BUFFER_SIZE (DSD_INPUT_BUFFER_SIZE * 2)
#define DSD_DOP256_TEST_MARKER "/tmp/rockbox-dop256-test"
#define DSD_NATIVE_TEST_MARKER "/tmp/rockbox-native-dsd-test"

static bool dsd_is_active;
static enum dsd_transport dsd_transport;
static uint8_t dsd_marker = 0x05;
static unsigned int dsd_restore_rate;
static uint8_t dsd_output[DSD_OUTPUT_BUFFER_SIZE] MEM_ALIGN_ATTR;
#endif

#ifdef HAVE_RECORDING
static void *pcm_data_rec = DEFAULT_CAPTURE_DEVICE;
static const char *capture_dev = NULL;
static snd_pcm_stream_t current_alsa_mode;  /* SND_PCM_STREAM_PLAYBACK / _CAPTURE */
static unsigned int recording_restore_rate;
#endif

static const char *current_alsa_device;

static void sink_dma_start(const void *addr, size_t size);

void pcm_alsa_set_playback_device(const char *device)
{
    playback_dev = device;
}

#ifdef HAVE_RECORDING
void pcm_alsa_set_capture_device(const char *device)
{
    capture_dev = device;
}
#endif

static int set_hwparams(snd_pcm_t *handle, unsigned long sampr)
{
    int err;
    unsigned int srate;
    snd_pcm_format_t stream_format = format;
    size_t stream_sample_size = sizeof(sample_t);
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_malloc(&params);

#ifdef HAVE_RECORDING
    if (current_alsa_mode == SND_PCM_STREAM_CAPTURE)
    {
        stream_format = SND_PCM_FORMAT_S16_LE;
        stream_sample_size = sizeof(int16_t);
    }
#endif

    /* Size playback buffers based on sample rate.

       Buffer size must be at least 4x period size!

       Note these are in FRAMES, and are sized to be about 8.5ms
       for the buffer and 2.1ms for the period
     */
    if (sampr > SAMPR_96) {
        buffer_size = MIX_FRAME_SAMPLES * 4 * 4;
        period_size = MIX_FRAME_SAMPLES * 4;
    } else if (sampr > SAMPR_48) {
        buffer_size = MIX_FRAME_SAMPLES * 2 * 4;
        period_size = MIX_FRAME_SAMPLES * 2;
    } else {
        buffer_size = MIX_FRAME_SAMPLES * 4;
        period_size = MIX_FRAME_SAMPLES;
    }

    /* choose all parameters */
    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0)
    {
        panicf("Broken configuration for playback: no configurations available: %s", snd_strerror(err));
        goto error;
    }
    /* set the interleaved read/write format */
    err = snd_pcm_hw_params_set_access(handle, params, access_);
    if (err < 0)
    {
        panicf("Access type not available for playback: %s", snd_strerror(err));
        goto error;
    }
    /* set the sample format */
    err = snd_pcm_hw_params_set_format(handle, params, stream_format);
    if (err < 0)
    {
        logf("Sample format not available for playback: %s", snd_strerror(err));
        goto error;
    }
    /* set the count of channels */
    err = snd_pcm_hw_params_set_channels(handle, params, channels);
    if (err < 0)
    {
        logf("Channels count (%i) not available for playbacks: %s", channels, snd_strerror(err));
        goto error;
    }
    /* set the stream rate */
    srate = sampr;
    err = snd_pcm_hw_params_set_rate_near(handle, params, &srate, 0);
    if (err < 0)
    {
        logf("Rate %luHz not available for playback: %s", sampr, snd_strerror(err));
        goto error;
    }
    real_sample_rate = srate;
    if (real_sample_rate != sampr)
    {
        logf("Rate doesn't match (requested %luHz, get %dHz)", sampr, real_sample_rate);
        err = -EINVAL;
        goto error;
    }

    /* set the buffer size */
    err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size);
    if (err < 0)
    {
        logf("Unable to set buffer size %ld for playback: %s", buffer_size, snd_strerror(err));
        goto error;
    }

    /* set the period size */
    err = snd_pcm_hw_params_set_period_size_near (handle, params, &period_size, NULL);
    if (err < 0)
    {
        logf("Unable to set period size %ld for playback: %s", period_size, snd_strerror(err));
        goto error;
    }

    if (frames) free(frames);
    frames = calloc(1, period_size * channels * stream_sample_size);

    /* write the parameters to device */
    err = snd_pcm_hw_params(handle, params);
    if (err < 0)
    {
        logf("Unable to set hw params for playback: %s", snd_strerror(err));
        goto error;
    }

    err = 0; /* success */
error:
    snd_pcm_hw_params_free(params);
    return err;
}

/* Set sw params: playback start threshold and low buffer watermark */
static int set_swparams(snd_pcm_t *handle)
{
    int err;

    snd_pcm_sw_params_t *swparams;
    snd_pcm_sw_params_malloc(&swparams);

    /* get the current swparams */
    err = snd_pcm_sw_params_current(handle, swparams);
    if (err < 0)
    {
        logf("Unable to determine current swparams for playback: %s", snd_strerror(err));
        goto error;
    }
    /* start the transfer when the buffer is half full */
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams, buffer_size / 2);
    if (err < 0)
    {
        logf("Unable to set start threshold mode for playback: %s", snd_strerror(err));
        goto error;
    }
    /* allow the transfer when at least period_size samples can be processed */
    err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_size);
    if (err < 0)
    {
        logf("Unable to set avail min for playback: %s", snd_strerror(err));
        goto error;
    }
    /* write the parameters to the playback device */
    err = snd_pcm_sw_params(handle, swparams);
    if (err < 0)
    {
        logf("Unable to set sw params for playback: %s", snd_strerror(err));
        goto error;
    }

    err = 0; /* success */
error:
    snd_pcm_sw_params_free(swparams);
    return err;
}

#if defined(HAVE_ALSA_32BIT) || defined(HAVE_ALSA_24BIT)
/* Multiplicative factors applied to each sample */
static int32_t dig_vol_mult_l = 0;
static int32_t dig_vol_mult_r = 0;

void pcm_set_mixer_volume(int vol_db_l, int vol_db_r)
{
    dig_vol_mult_l = fp_factor(fp_div(vol_db_l, 10, 16), 16);
    dig_vol_mult_r = fp_factor(fp_div(vol_db_r, 10, 16), 16);
#if defined(HAVE_ALSA_24BIT)
    /* S24_LE is stored in four bytes but has eight fewer valid bits. */
    dig_vol_mult_l >>= 8;
    dig_vol_mult_r >>= 8;
#endif
}
#endif

/* copy pcm samples to a spare buffer, suitable for snd_pcm_writei() */
static bool copy_frames(bool first)
{
    ssize_t nframes, frames_left = period_size;
    bool new_buffer = false;

    while (frames_left > 0)
    {
        if (!pcm_size)
        {
            new_buffer = true;
#ifdef HAVE_RECORDING
            switch (current_alsa_mode)
            {
            case SND_PCM_STREAM_PLAYBACK:
#endif
                if (!pcm_play_dma_complete_callback(PCM_DMAST_OK, &pcm_data, &pcm_size))
                {
                    return false;
                }
#ifdef HAVE_RECORDING
                break;
            case SND_PCM_STREAM_CAPTURE:
                if (!pcm_rec_dma_complete_callback(PCM_DMAST_OK,
                                                   &pcm_data_rec, &pcm_size))
                {
                    return false;
                }
                break;
            default:
                break;
            }
#endif
        }

        /* Note:  This assumes stereo 16-bit */
        if (pcm_size % 4)
            panicf("Wrong pcm_size");
        /* the compiler will optimize this test away */
        nframes = MIN((ssize_t)pcm_size/4, frames_left);

#ifdef HAVE_RECORDING
        switch (current_alsa_mode)
        {
        case SND_PCM_STREAM_PLAYBACK:
#endif
#if defined(HAVE_ALSA_32BIT) || defined(HAVE_ALSA_24BIT)
            if (format == SND_PCM_FORMAT_S32_LE ||
                format == SND_PCM_FORMAT_S24_LE)
            {
                /* We have to convert 16-bit to 32-bit, the need to multiply the
                 * sample by some value so the sound is not too low */
                const int16_t *pcm_ptr = pcm_data;
                sample_t *sample_ptr = frames;
                sample_ptr += 2 * (period_size - frames_left);
                for (int i = 0; i < nframes; i++)
                {
                    *sample_ptr++ = (*pcm_ptr++ * dig_vol_mult_l) + PCM_DC_OFFSET_VALUE;
                    *sample_ptr++ = (*pcm_ptr++ * dig_vol_mult_r) + PCM_DC_OFFSET_VALUE;
                }
            }
            else
#endif
            {
                /* Rockbox and PCM have same format: memcopy */
                int16_t *frame_ptr = frames;
                frame_ptr += 2 * (period_size - frames_left);
                memcpy(frame_ptr, pcm_data, nframes * 4);
            }
#ifdef HAVE_RECORDING
            break;
        case SND_PCM_STREAM_CAPTURE:
        {
            int16_t *frame_ptr = frames;
            frame_ptr += 2 * (period_size - frames_left);
            memcpy(pcm_data_rec, frame_ptr, nframes * 4);
            break;
        }
        default:
            break;
        }
#endif
#ifdef HAVE_RECORDING
        if (current_alsa_mode == SND_PCM_STREAM_CAPTURE)
            pcm_data_rec = (uint8_t *)pcm_data_rec + nframes * 4;
        else
#endif
            pcm_data = (const uint8_t *)pcm_data + nframes * 4;
        pcm_size -= nframes*4;
        frames_left -= nframes;

        if (new_buffer && !first)
        {
            new_buffer = false;
#ifdef HAVE_RECORDING
            switch (current_alsa_mode)
            {
            case SND_PCM_STREAM_PLAYBACK:
#endif
                pcm_play_dma_status_callback(PCM_DMAST_STARTED);
#ifdef HAVE_RECORDING
                break;
            case SND_PCM_STREAM_CAPTURE:
                pcm_rec_dma_status_callback(PCM_DMAST_STARTED);
                break;
            default:
                break;
            }
#endif
        }
    }

    return true;
}

static void pcm_service(snd_pcm_t *handle)
{
    int err;

#ifdef HAVE_DSD
    /* Direct DSD writes own the ALSA handle while this flag is set. */
    if (dsd_is_active)
        return;
#endif

    if (!handle) return;

    if (pthread_mutex_trylock(&pcm_mtx) != 0)
        return;

    if (period_size <= 0 || !frames)
        goto abort;

    snd_pcm_state_t state = snd_pcm_state(handle);

    if (state == SND_PCM_STATE_XRUN)
    {
        xruns++;
        logf("initial underrun!");
        err = snd_pcm_recover(handle, -EPIPE, 0);
        if (err < 0) {
            logf("XRUN Recovery error: %s", snd_strerror(err));
            goto abort;
        }
    }
    else if (state == SND_PCM_STATE_DRAINING)
    {
        logf("draining...");
        goto abort;
    }
    else if (state == SND_PCM_STATE_SETUP)
    {
        goto abort;
    }

#ifdef HAVE_RECORDING
    if (current_alsa_mode == SND_PCM_STREAM_PLAYBACK)
    {
#endif
        while (snd_pcm_avail_update(handle) >= period_size)
        {
            if (copy_frames(false))
            {
            retry:
                err = snd_pcm_writei(handle, frames, period_size);
                if (err == -EPIPE)
                {
                    logf("mid underrun!");
                    xruns++;
                    err = snd_pcm_recover(handle, -EPIPE, 0);
                    if (err < 0) {
                       logf("XRUN Recovery error: %s", snd_strerror(err));
                       goto abort;
                    }
                    goto retry;
                }
                else if (err != period_size)
                {
                    logf("Write error: written %i expected %li", err, period_size);
                    break;
                }
            }
            else
            {
                logf("%s: No Data (%d).", __func__, state);
                break;
            }
        }
#ifdef HAVE_RECORDING
    }
    else if (current_alsa_mode == SND_PCM_STREAM_CAPTURE)
    {
        while (snd_pcm_avail_update(handle) >= period_size)
        {
            int err = snd_pcm_readi(handle, frames, period_size);
            if (err == -EPIPE)
            {
                logf("rec mid underrun!");
                xruns++;
                err = snd_pcm_recover(handle, -EPIPE, 0);
                if (err < 0) {
                   logf("XRUN Recovery error: %s", snd_strerror(err));
                   goto abort;
                }
		continue;  /* buffer contents trashed, no sense in trying to copy */
            }
            else if (err != period_size)
            {
                logf("Read error: read %i expected %li", err, period_size);
                break;
            }

            /* start the fake DMA transfer */
            if (!copy_frames(false))
            {
                /* do not spam logf */
                /* logf("%s: No Data.", __func__); */
                break;
            }
        }
    }
#endif

    if (snd_pcm_state(handle) == SND_PCM_STATE_PREPARED)
    {
        err = snd_pcm_start(handle);
        if (err < 0) {
            logf("cb start error: %s", snd_strerror(err));
            /* Depending on the error we might be SOL */
        }
    }

abort:
    pthread_mutex_unlock(&pcm_mtx);
}

#ifdef HAVE_ALSA_POLLING
static void *pcm_poll_worker(void *arg)
{
    (void)arg;

    while (pcm_poll_running)
    {
        pcm_service(handle);
        usleep(2000);
    }

    return NULL;
}
#else
static void async_callback(snd_async_handler_t *ahandler)
{
    if (ahandler)
        pcm_service(snd_async_handler_get_pcm(ahandler));
}
#endif

static void close_hwdev(bool drain)
{
    logf("closedev (%p)", handle);

    if (handle) {
#ifdef HAVE_DSD
        (void)drain;
        /*
         * The AP80 endpoint can remain RUNNING without advancing its
         * hardware pointer, so even a state-qualified drain may never
         * return. Closing or reconfiguring this target must be immediate.
         */
        snd_pcm_drop(handle);
#else
        /* A PREPARED stream has no running DMA to drain. */
        if (drain && snd_pcm_state(handle) == SND_PCM_STATE_RUNNING) {
            if (snd_pcm_drain(handle) < 0)
                snd_pcm_drop(handle);
        } else {
            snd_pcm_drop(handle);
        }
#endif
#ifdef AUDIOHW_MUTE_ON_STOP
        audiohw_mute(true);
#endif
#ifndef HAVE_ALSA_POLLING
        if (ahandler) {
            snd_async_del_handler(ahandler);
            ahandler = NULL;
        }
#endif
        snd_pcm_close(handle);

        handle = NULL;
    }
    current_alsa_device = NULL;

#ifdef HAVE_RECORDING
    pcm_data_rec = NULL;
#endif
}

static void alsadev_cleanup(void)
{
#ifdef HAVE_ALSA_POLLING
    if (pcm_poll_running)
    {
        pcm_poll_running = false;
        pthread_join(pcm_poll_thread, NULL);
    }
#endif
    free(frames);
    frames = NULL;
    close_hwdev(true);
}

static int open_hwdev_try(const char *device, snd_pcm_stream_t mode)
{
    int err;

    logf("opendev %s (%p)", device, handle);

    if (handle && current_alsa_device &&
        !strcmp(device, current_alsa_device)
#ifdef HAVE_RECORDING
        && current_alsa_mode == mode
#endif
        )
    {
        return 0;
    }

    /* Close old handle */
    close_hwdev(false);

    if ((err = snd_pcm_open(&handle, device, mode, 0)) < 0)
    {
        handle = NULL;
        logf("%s(): Cannot open device %s: %s", __func__, device,
             snd_strerror(err));
        return err;
    }
    last_sample_rate = 0;

#ifndef HAVE_ALSA_POLLING
    /* assign alternative stack for the signal handlers */
    stack_t ss = {
        .ss_sp = signal_stack,
        .ss_size = sizeof(signal_stack),
        .ss_flags = 0
    };
    struct sigaction sa;

    err = sigaltstack(&ss, NULL);
    if (err < 0)
    {
        logf("Unable to install alternative signal stack: %s", strerror(err));
        goto error;
    }

    err = snd_async_add_pcm_handler(&ahandler, handle, async_callback, NULL);
    if (err < 0)
    {
        logf("Unable to register async handler: %s", snd_strerror(err));
        goto error;
    }

    /* only modify the stack the handler runs on */
    sigaction(SIGIO, NULL, &sa);
    sa.sa_flags |= SA_ONSTACK;
    err = sigaction(SIGIO, &sa, NULL);
    if (err < 0)
    {
        logf("Unable to install alternative signal stack: %s", strerror(err));
        goto error;
    }
#endif

#ifdef HAVE_RECORDING
    current_alsa_mode = mode;
#else
    (void)mode;
#endif
    current_alsa_device = device;

    if (!cleanup_registered)
    {
        atexit(alsadev_cleanup);
        cleanup_registered = true;
    }

    return 0;

#ifndef HAVE_ALSA_POLLING
error:
    close_hwdev(false);
    return err;
#endif
}

static void open_hwdev(const char *device, snd_pcm_stream_t mode)
{
    int err = open_hwdev_try(device, mode);

    if (err < 0)
        panicf("%s(): Cannot open device %s: %s", __func__, device,
               snd_strerror(err));
}

static void sink_dma_init(void)
{
    logf("PCM DMA Init");

    if (!pcm_mtx_initialized)
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&pcm_mtx, &attr);
        pthread_mutexattr_destroy(&attr);
        pcm_mtx_initialized = true;
    }

    audiohw_preinit();

    open_hwdev(playback_dev, SND_PCM_STREAM_PLAYBACK);

#ifdef HAVE_ALSA_POLLING
    if (!pcm_poll_running)
    {
        int err;

        pcm_poll_running = true;
        err = pthread_create(&pcm_poll_thread, NULL, pcm_poll_worker, NULL);
        if (err != 0)
        {
            pcm_poll_running = false;
            panicf("Cannot start ALSA worker: %s", strerror(err));
        }
    }
#endif

    return;
}

bool pcm_alsa_switch_playback_device(const char *device)
{
    const char *old_device;
    unsigned int restore_rate;
    bool was_playing;
    int err;

    if (!device || !handle)
        return false;

    pthread_mutex_lock(&pcm_mtx);

#ifdef HAVE_DSD
    if (dsd_is_active)
    {
        pthread_mutex_unlock(&pcm_mtx);
        return false;
    }
#endif
#ifdef HAVE_RECORDING
    if (current_alsa_mode != SND_PCM_STREAM_PLAYBACK)
    {
        pthread_mutex_unlock(&pcm_mtx);
        return false;
    }
#endif

    if (current_alsa_device && !strcmp(device, current_alsa_device))
    {
        playback_dev = device;
        pthread_mutex_unlock(&pcm_mtx);
        return true;
    }

    old_device = playback_dev;
    restore_rate = last_sample_rate ?
                   last_sample_rate : hw_freq_sampr[HW_FREQ_DEFAULT];
    was_playing = pcm_is_playing();

    err = open_hwdev_try(device, SND_PCM_STREAM_PLAYBACK);
    if (err >= 0)
        err = set_hwparams(handle, restore_rate);
    if (err >= 0)
        err = set_swparams(handle);

    if (err < 0)
    {
        logf("Playback endpoint %s failed; restoring %s", device, old_device);
        if (open_hwdev_try(old_device, SND_PCM_STREAM_PLAYBACK) >= 0 &&
            set_hwparams(handle, restore_rate) >= 0)
        {
            set_swparams(handle);
            last_sample_rate = restore_rate;
            real_sample_rate = restore_rate;
            if (was_playing)
                sink_dma_start(pcm_data, pcm_size);
        }
        pthread_mutex_unlock(&pcm_mtx);
        return false;
    }

    playback_dev = device;
    last_sample_rate = restore_rate;
    real_sample_rate = restore_rate;
    if (was_playing)
        sink_dma_start(pcm_data, pcm_size);

    logf("Playback endpoint switched to %s", device);
    pthread_mutex_unlock(&pcm_mtx);
    return true;
}

static void sink_lock(void)
{
    pthread_mutex_lock(&pcm_mtx);
}

static void sink_unlock(void)
{
    pthread_mutex_unlock(&pcm_mtx);
}

static void sink_set_freq_nolock(uint16_t freq)
{
    unsigned int sampr = hw_freq_sampr[freq];

    logf("PCM DMA Settings %d %lu", last_sample_rate, sampr);

#ifdef HAVE_DSD
    if (dsd_is_active)
        return;
#endif

    if (last_sample_rate != sampr)
    {
        last_sample_rate = sampr;

#ifdef AUDIOHW_MUTE_ON_SRATE_CHANGE
        audiohw_mute(true);
#endif
        snd_pcm_drop(handle);

        set_hwparams(handle, sampr); // FIXME: check return code?
        set_swparams(handle); // FIXME: check return code?

#if defined(HAVE_NWZ_LINUX_CODEC)
        /* Sony NWZ linux driver uses a nonstandard mecanism to set the sampling rate */
        audiohw_set_frequency(sampr);
#endif
        /* (Will be unmuted by pcm resuming) */
    }
}

static void sink_set_freq(uint16_t freq)
{
    sink_lock();
    sink_set_freq_nolock(freq);
    sink_unlock();
}

static void sink_dma_stop(void)
{
#ifdef HAVE_DSD
    logf("PCM DMA stop (%d)", snd_pcm_state(handle));

    if (dsd_is_active)
        return;

    /*
     * A stopped AP80 PCM endpoint can still report RUNNING while its
     * hardware pointer is stationary. Draining it blocks the cooperative
     * Rockbox thread before DSD setup; dropping it is the required stop
     * operation on this target.
     */
    snd_pcm_drop(handle);
#else
    snd_pcm_state_t state = snd_pcm_state(handle);
    logf("PCM DMA stop (%d)", state);

    if (state == SND_PCM_STATE_RUNNING)
    {
        int err = snd_pcm_drain(handle);
        if (err < 0)
        {
            logf("Drain failed: %s", snd_strerror(err));
            snd_pcm_drop(handle);
        }
    }
    else
    {
        snd_pcm_drop(handle);
    }
#endif
#ifdef AUDIOHW_MUTE_ON_STOP
    audiohw_mute(true);
#endif
}

static void sink_dma_start(const void *addr, size_t size)
{
    logf("PCM DMA start (%p %d)", addr, size);

#ifdef HAVE_DSD
    /* Voice/beep channels must not be mixed into an active DSD stream. */
    if (dsd_is_active)
        return;
#endif

    pcm_data = addr;
    pcm_size = size;

#if !defined(AUDIOHW_MUTE_ON_STOP) && defined(AUDIOHW_MUTE_ON_SRATE_CHANGE)
    audiohw_mute(false);
#endif

    while (1)
    {
        snd_pcm_state_t state = snd_pcm_state(handle);
        logf("PCM State %d", state);

        switch (state)
        {
            case SND_PCM_STATE_RUNNING:
#if defined(AUDIOHW_MUTE_ON_STOP)
                audiohw_mute(false);
#endif
                return;
            case SND_PCM_STATE_XRUN:
            {
                logf("Trying to recover from underrun");
                int err = snd_pcm_recover(handle, -EPIPE, 0);
                if (err < 0)
                    logf("Recovery failed: %s", snd_strerror(err));
                continue;
            }
            case SND_PCM_STATE_SETUP:
            {
                int err = snd_pcm_prepare(handle);
                if (err < 0)
                    logf("Prepare error: %s", snd_strerror(err));
            }
                /* fall through */
            case SND_PCM_STATE_PREPARED:
            {
                int err;
#if 0
                /* fill buffer with silence to initiate playback without noisy click */
                snd_pcm_sframes_t sample_size = buffer_size;
                sample_t *samples = calloc(1, sample_size * channels * sizeof(sample_t));

                snd_pcm_format_set_silence(format, samples, sample_size);
                err = snd_pcm_writei(handle, samples, sample_size);
                free(samples);

                if (err != (ssize_t)sample_size)
                {
                    logf("Initial write error: written %i expected %li", err, sample_size);
                    return;
                }
#else
                /* Fill buffer with proper sample data */
                while (snd_pcm_avail_update(handle) >= period_size)
                {
                    if (copy_frames(true))
                    {
                        err = snd_pcm_writei(handle, frames, period_size);
                        if (err < 0 && err != period_size && err != -EAGAIN)
                        {
                            logf("Write error: written %i expected %li", err, period_size);
                            break;
                        }
                    }
                }
#endif
                err = snd_pcm_start(handle);
                if (err < 0) {
                    logf("start error: %s", snd_strerror(err));
                    /* We will recover on the next iteration */
                }

                break;
            }
            case SND_PCM_STATE_DRAINING:
                /* run until drained */
                continue;
            default:
                logf("Unhandled state: %s", snd_pcm_state_name(state));
                return;
        }
    }
}

static void sink_dma_postinit(void)
{
    audiohw_postinit();

#ifdef AUDIOHW_NEEDS_INITIAL_UNMUTE
    audiohw_mute(false);
#endif
}

unsigned int pcm_alsa_get_rate(void)
{
    return real_sample_rate;
}

unsigned int pcm_alsa_get_xruns(void)
{
    return xruns;
}

#ifdef HAVE_DSD
static int set_dsd_hwparams(unsigned int rate, enum dsd_transport transport)
{
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t dsd_format;
    snd_pcm_uframes_t dsd_period_size =
        ap80promax_dsd_period_frames(rate);
    snd_pcm_uframes_t dsd_buffer_size = dsd_period_size * 4;
    unsigned int exact_rate = rate;
    int err;

    dsd_format = transport == DSD_TRANSPORT_DOP ?
#if defined(HAVE_ALSA_24BIT)
                 SND_PCM_FORMAT_S24_LE :
#else
                 SND_PCM_FORMAT_S32_LE :
#endif
                 SND_PCM_FORMAT_S16_LE;

    snd_pcm_hw_params_malloc(&params);
    err = snd_pcm_hw_params_any(handle, params);
    if (err < 0)
        goto out;

    err = snd_pcm_hw_params_set_access(handle, params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
        goto out;

    err = snd_pcm_hw_params_set_format(handle, params, dsd_format);
    if (err < 0)
        goto out;

    err = snd_pcm_hw_params_set_channels(handle, params, 2);
    if (err < 0)
        goto out;

    err = snd_pcm_hw_params_set_rate_near(handle, params, &exact_rate, 0);
    if (err < 0 || exact_rate != rate)
    {
        if (err >= 0)
            err = -EINVAL;
        goto out;
    }

    snd_pcm_hw_params_set_buffer_size_near(handle, params, &dsd_buffer_size);
    snd_pcm_hw_params_set_period_size_near(handle, params, &dsd_period_size,
                                            NULL);
    err = snd_pcm_hw_params(handle, params);
    if (err >= 0)
    {
        /*
         * Reuse the normal hosted start policy with the values ALSA
         * actually negotiated. Starting after one small DSF block causes
         * an immediate AP80 underrun; half a buffer gives the DAC enough
         * queued carrier frames to run continuously.
         */
        buffer_size = dsd_buffer_size;
        period_size = dsd_period_size;
        err = set_swparams(handle);
    }

out:
    if (err < 0)
        logf("DSD hwparams failed: %s", snd_strerror(err));
    snd_pcm_hw_params_free(params);
    return err;
}

enum dsd_transport pcm_alsa_dsd_open(unsigned int dsd_rate,
                                     unsigned int channels)
{
    enum dsd_transport transport;
    unsigned int carrier_rate;
    int err;

    if (!handle || channels != 2)
        return DSD_TRANSPORT_ERROR;

    transport = ap80promax_dsd_select_transport(dsd_rate);
    if (transport == DSD_TRANSPORT_ERROR &&
        access(DSD_DOP256_TEST_MARKER, F_OK) == 0)
    {
        transport = ap80promax_dsd_select_diagnostic_dop(dsd_rate);
        if (transport == DSD_TRANSPORT_DOP)
            logf("WARNING: diagnostic high-rate DoP transport enabled");
    }
    if (transport == DSD_TRANSPORT_ERROR &&
        access(DSD_NATIVE_TEST_MARKER, F_OK) == 0 &&
        dsd_rate / 32 <= AP80PROMAX_DSD_MAX_ALSA_RATE)
    {
        transport = DSD_TRANSPORT_NATIVE;
        logf("WARNING: diagnostic native DSD transport enabled");
    }
    carrier_rate = ap80promax_dsd_carrier_rate(dsd_rate, transport);
    if (transport == DSD_TRANSPORT_ERROR || carrier_rate == 0)
        return DSD_TRANSPORT_ERROR;

    pthread_mutex_lock(&pcm_mtx);

    if (dsd_is_active)
    {
        snd_pcm_drop(handle);
        dsd_is_active = false;
    }

    dsd_restore_rate = last_sample_rate ?
                       last_sample_rate : hw_freq_sampr[HW_FREQ_DEFAULT];
    snd_pcm_drop(handle);

    dsd_transport = transport;
    dsd_marker = 0x05;
    dsd_is_active = true; /* Keep the async callback away from the handle. */
    audiohw_set_dsd_mode(true);

    err = set_dsd_hwparams(carrier_rate, transport);
    if (err >= 0)
        err = snd_pcm_prepare(handle);

    if (err < 0)
    {
        logf("DSD open failed: %s", snd_strerror(err));
        audiohw_set_dsd_mode(false);
        dsd_is_active = false;
        dsd_transport = DSD_TRANSPORT_ERROR;
        set_hwparams(handle, dsd_restore_rate);
        set_swparams(handle);
        pthread_mutex_unlock(&pcm_mtx);
        return DSD_TRANSPORT_ERROR;
    }

    real_sample_rate = carrier_rate;
    logf("DSD open: rate=%u carrier=%u mode=%s", dsd_rate, carrier_rate,
         transport == DSD_TRANSPORT_DOP ? "dop" : "native");
    pthread_mutex_unlock(&pcm_mtx);
    return transport;
}

ssize_t pcm_alsa_dsd_write(const void *data, size_t size)
{
    const uint8_t *src = data;
    size_t packed_size;
    size_t frame_bytes;
    size_t frames_left;
    uint8_t *out;

    if (!data || size == 0 || (size & 3) ||
        size > DSD_INPUT_BUFFER_SIZE)
        return -EINVAL;

    pthread_mutex_lock(&pcm_mtx);
    if (!dsd_is_active)
    {
        pthread_mutex_unlock(&pcm_mtx);
        return -ENODEV;
    }

    if (dsd_transport == DSD_TRANSPORT_DOP)
    {
        packed_size = ap80promax_dsd_pack_dop(
                dsd_output, sizeof(dsd_output), src, size, &dsd_marker);
        frame_bytes = 8; /* S24_LE/S32_LE stereo, both use 32-bit slots */
    }
    else
    {
        packed_size = ap80promax_dsd_pack_native(
                dsd_output, sizeof(dsd_output), src, size);
        frame_bytes = 4; /* S16_LE stereo; stock native framing */
    }

    if (packed_size == 0)
    {
        pthread_mutex_unlock(&pcm_mtx);
        return -EINVAL;
    }

    out = dsd_output;
    frames_left = packed_size / frame_bytes;

    while (frames_left > 0)
    {
        snd_pcm_sframes_t written = snd_pcm_writei(handle, out, frames_left);
        if (written == -EPIPE || written == -ESTRPIPE)
        {
            xruns++;
            written = snd_pcm_recover(handle, written, 1);
            if (written >= 0)
                continue;
        }

        if (written < 0)
        {
            logf("DSD write failed: %s", snd_strerror(written));
            pthread_mutex_unlock(&pcm_mtx);
            return written;
        }

        out += written * frame_bytes;
        frames_left -= written;
    }

    pthread_mutex_unlock(&pcm_mtx);
    return size;
}

void pcm_alsa_dsd_pause(bool pause)
{
    pthread_mutex_lock(&pcm_mtx);
    if (dsd_is_active)
    {
        int err = snd_pcm_pause(handle, pause ? 1 : 0);
        if (err < 0)
        {
            /* Some ALSA drivers do not implement hardware pause. Dropping
               pending frames gives deterministic silence; prepare re-arms
               the stream when Rockbox explicitly resumes. */
            if (pause)
                snd_pcm_drop(handle);
            else
                snd_pcm_prepare(handle);
        }
    }
    pthread_mutex_unlock(&pcm_mtx);
}

void pcm_alsa_dsd_close(bool drain)
{
    pthread_mutex_lock(&pcm_mtx);
    if (!dsd_is_active)
    {
        pthread_mutex_unlock(&pcm_mtx);
        return;
    }

    if (drain)
        snd_pcm_drain(handle);
    else
        snd_pcm_drop(handle);

    audiohw_set_dsd_mode(false);
    dsd_is_active = false;
    dsd_transport = DSD_TRANSPORT_ERROR;

    if (set_hwparams(handle, dsd_restore_rate) >= 0)
    {
        set_swparams(handle);
        last_sample_rate = dsd_restore_rate;
        real_sample_rate = dsd_restore_rate;
    }
    else
    {
        last_sample_rate = 0;
    }

    logf("DSD closed");
    pthread_mutex_unlock(&pcm_mtx);
}

bool pcm_alsa_dsd_active(void)
{
    return dsd_is_active;
}
#endif /* HAVE_DSD */

struct pcm_sink builtin_pcm_sink = {
    .caps = {
        .samprs       = hw_freq_sampr,
        .num_samprs   = HW_NUM_FREQ,
        .default_freq = HW_FREQ_DEFAULT,
    },
    .ops = {
        .init     = sink_dma_init,
        .postinit = sink_dma_postinit,
        .set_freq = sink_set_freq,
        .lock     = sink_lock,
        .unlock   = sink_unlock,
        .play     = sink_dma_start,
        .stop     = sink_dma_stop,
    },
};

#ifdef HAVE_RECORDING
void pcm_rec_lock(void)
{
    sink_lock();
}

void pcm_rec_unlock(void)
{
    sink_unlock();
}

void pcm_rec_dma_init(void)
{
    logf("PCM REC DMA Init");

    recording_restore_rate = last_sample_rate ?
                             last_sample_rate :
                             hw_freq_sampr[HW_FREQ_DEFAULT];
    open_hwdev(capture_dev, SND_PCM_STREAM_CAPTURE);
}

void pcm_rec_dma_close(void)
{
    logf("Rec DMA Close");
    open_hwdev(playback_dev, SND_PCM_STREAM_PLAYBACK);
    if (set_hwparams(handle, recording_restore_rate) >= 0)
    {
        set_swparams(handle);
        last_sample_rate = recording_restore_rate;
        real_sample_rate = recording_restore_rate;
    }
    else
    {
        last_sample_rate = 0;
    }
}

void pcm_rec_dma_start(void *start, size_t size)
{
    logf("PCM REC DMA start (%p %d)", start, size);
    pcm_data_rec = start;
    pcm_size = size;

    if (!handle) return;

    while (1)
    {
        snd_pcm_state_t state = snd_pcm_state(handle);

        switch (state)
        {
            case SND_PCM_STATE_RUNNING:
                return;
            case SND_PCM_STATE_XRUN:
            {
                logf("Trying to recover from error");
                int err = snd_pcm_recover(handle, -EPIPE, 0);
                if (err < 0)
                    panicf("Recovery failed: %s", snd_strerror(err));
                continue;
            }
            case SND_PCM_STATE_SETUP:
            {
                int err = snd_pcm_prepare(handle);
                if (err < 0)
                    panicf("Prepare error: %s", snd_strerror(err));
            }
                /* fall through */
            case SND_PCM_STATE_PREPARED:
            {
                int err = snd_pcm_start(handle);
                if (err < 0)
                    panicf("Start error: %s", snd_strerror(err));
                return;
            }
            case SND_PCM_STATE_DRAINING:
                /* run until drained */
                continue;
            default:
                logf("Unhandled state: %s", snd_pcm_state_name(state));
                return;
        }
    }
}

void pcm_rec_dma_stop(void)
{
    logf("Rec DMA Stop");
    close_hwdev(false);
}

const void * pcm_rec_dma_get_peak_buffer(void)
{
    uintptr_t addr = (uintptr_t)pcm_data_rec;
    return (void*)((addr + 3) & ~3);
}

#ifdef SIMULATOR
void audiohw_set_recvol(int left, int right, int type)
{
    (void)left;
    (void)right;
    (void)type;
}
#endif

#endif /* HAVE_RECORDING */
