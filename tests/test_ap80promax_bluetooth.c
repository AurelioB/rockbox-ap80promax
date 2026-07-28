#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bluetooth-ap80promax.h"

#ifndef SIMULATOR
#include "services-ap80promax.h"

#define SPEAKER_ADDRESS "02:00:00:00:80:01"
#define HEADPHONE_ADDRESS "02:00:00:00:80:02"

static pthread_mutex_t fake_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool fake_powered;
static bool fake_scanning;
static bool fake_connected;
static bool fake_endpoint_available = true;
static bool fake_speaker_paired;
static bool fake_speaker_trusted;
static bool fake_pair_effect = true;
static bool fake_disconnect_effect = true;
static bool fake_switch_allowed = true;
static bool fake_local_switch_allowed = true;
static bool fake_codec_available = true;
static bool fake_saw_bounded_scan;
static bool fake_saw_scan_off;
static char fake_pcm_device[96] = "hw:0,1";
static unsigned int fake_volume_calls;
static unsigned int fake_local_switch_failures;
static int fake_bluealsa_left;
static int fake_bluealsa_right;
static unsigned int fake_local_volume_calls;
static int fake_local_left;
static int fake_local_right;

static int write_output(char *output, size_t output_size, const char *text)
{
    if (!output || output_size == 0)
        return 0;
    int length = snprintf(output, output_size, "%s", text);
    return length < 0 || (size_t)length >= output_size ? -ENOSPC : 0;
}

void ap80_services_get_status(struct ap80_service_status *status)
{
    memset(status, 0, sizeof(*status));
    pthread_mutex_lock(&fake_mutex);
    status->bluetooth_enabled = fake_powered;
    status->bluetoothd = fake_powered;
    status->bluealsa = fake_powered;
    pthread_mutex_unlock(&fake_mutex);
}

int ap80_services_claim(unsigned int services)
{
    return services == AP80_SERVICE_BLUETOOTH ? 0 : -EINVAL;
}

int ap80_services_set_enabled(enum ap80_service service, bool enabled)
{
    assert(service == AP80_SERVICE_BLUETOOTH);
    pthread_mutex_lock(&fake_mutex);
    fake_powered = enabled;
    if (!enabled)
    {
        fake_scanning = false;
        fake_connected = false;
    }
    pthread_mutex_unlock(&fake_mutex);
    return 0;
}

int ap80_run_argv(const char *path, const char *const argv[],
                  unsigned int timeout_ms, char *output,
                  size_t output_size)
{
    assert(path != NULL);
    assert(argv != NULL);
    assert(argv[0] != NULL);
    assert(strcmp(path, argv[0]) == 0);
    assert(timeout_ms != 0);
    if (output && output_size != 0)
        output[0] = '\0';

    pthread_mutex_lock(&fake_mutex);
    int result = 0;
    if (strcmp(path, "/usr/bin/bluetoothctl") == 0)
    {
        assert(argv[1] != NULL);
        if (strcmp(argv[1], "--timeout") == 0)
        {
            assert(strcmp(argv[2], "3") == 0);
            assert(strcmp(argv[3], "scan") == 0);
            assert(strcmp(argv[4], "on") == 0);
            fake_saw_bounded_scan = true;
            /* Discovery stops when the bounded bluetoothctl client exits. */
            fake_scanning = false;
        }
        else if (strcmp(argv[1], "power") == 0)
        {
            fake_powered = strcmp(argv[2], "on") == 0;
            if (!fake_powered)
            {
                fake_scanning = false;
                fake_connected = false;
            }
        }
        else if (strcmp(argv[1], "scan") == 0)
        {
            assert(strcmp(argv[2], "off") == 0);
            fake_saw_scan_off = true;
            fake_scanning = strcmp(argv[2], "on") == 0;
        }
        else if (strcmp(argv[1], "devices") == 0)
        {
            result = write_output(
                output, output_size,
                "Device " SPEAKER_ADDRESS " AP80 BT Speaker\n"
                "Device " HEADPHONE_ADDRESS " AP80 BT Headphones\n");
        }
        else if (strcmp(argv[1], "paired-devices") == 0)
        {
            char paired[256];
            snprintf(paired, sizeof(paired),
                     "%sDevice " HEADPHONE_ADDRESS
                     " AP80 BT Headphones\n",
                     fake_speaker_paired ?
                     "Device " SPEAKER_ADDRESS " AP80 BT Speaker\n" : "");
            result = write_output(output, output_size, paired);
        }
        else if (strcmp(argv[1], "pair") == 0)
        {
            assert(strcmp(argv[2], SPEAKER_ADDRESS) == 0);
            if (fake_pair_effect)
                fake_speaker_paired = true;
        }
        else if (strcmp(argv[1], "trust") == 0)
        {
            assert(strcmp(argv[2], SPEAKER_ADDRESS) == 0);
            fake_speaker_trusted = true;
        }
        else if (strcmp(argv[1], "connect") == 0)
        {
            assert(strcmp(argv[2], SPEAKER_ADDRESS) == 0);
            fake_connected = true;
        }
        else if (strcmp(argv[1], "disconnect") == 0)
        {
            assert(strcmp(argv[2], SPEAKER_ADDRESS) == 0);
            if (fake_disconnect_effect)
                fake_connected = false;
        }
        else if (strcmp(argv[1], "info") == 0)
        {
            char info[512];
            bool headphone = strcmp(argv[2], HEADPHONE_ADDRESS) == 0;
            snprintf(info, sizeof(info),
                     "Device %s\n"
                     "\tName: %s\n"
                     "\tAlias: %s\n"
                     "\tPaired: %s\n"
                     "\tTrusted: %s\n"
                     "\tConnected: %s\n",
                     argv[2],
                     headphone ? "AP80 BT Headphones" :
                                 "AP80 BT Speaker",
                     headphone ? "AP80 BT Headphones" :
                                 "AP80 BT Speaker",
                     headphone || fake_speaker_paired ? "yes" : "no",
                     headphone || fake_speaker_trusted ? "yes" : "no",
                     !headphone && fake_connected ? "yes" : "no");
            result = write_output(output, output_size, info);
        }
        else
            result = -EINVAL;
    }
    else if (strcmp(path, "/usr/bin/bluealsa-cli") == 0)
    {
        assert(argv[1] != NULL);
        if (strcmp(argv[1], "list-pcms") == 0)
        {
            result = write_output(
                output, output_size,
                fake_endpoint_available ?
                "/org/bluealsa/hci0/dev_02_00_00_00_80_01/"
                "a2dpsrc/sink\n" : "");
        }
        else if (strcmp(argv[1], "codec") == 0)
        {
            if (fake_codec_available)
                result = write_output(output, output_size,
                                      "Selected codec: SBC\n");
            else
                result = -EIO;
        }
        else if (strcmp(argv[1], "volume") == 0)
        {
            assert(argv[2] != NULL);
            assert(argv[3] != NULL);
            assert(argv[4] != NULL);
            fake_bluealsa_left = atoi(argv[3]);
            fake_bluealsa_right = atoi(argv[4]);
            fake_volume_calls++;
        }
        else
            result = -EINVAL;
    }
    else
        result = -ENOENT;

    pthread_mutex_unlock(&fake_mutex);
    return result;
}

bool pcm_alsa_get_playback_device(char *device, size_t size)
{
    pthread_mutex_lock(&fake_mutex);
    size_t length = strlen(fake_pcm_device);
    bool fits = device && length < size;
    if (fits)
        memcpy(device, fake_pcm_device, length + 1);
    pthread_mutex_unlock(&fake_mutex);
    return fits;
}

static bool fake_switch_playback_device_locked(const char *device)
{
    bool bluetooth = strncmp(device, "bluealsa:", strlen("bluealsa:")) == 0;
    bool result = bluetooth ? fake_switch_allowed :
                              fake_local_switch_allowed;
    if (!bluetooth && !result)
        fake_local_switch_failures++;
    if (result)
        snprintf(fake_pcm_device, sizeof(fake_pcm_device), "%s", device);
    return result;
}

bool pcm_alsa_switch_playback_device(const char *device)
{
    pthread_mutex_lock(&fake_mutex);
    bool result = fake_switch_playback_device_locked(device);
    pthread_mutex_unlock(&fake_mutex);
    return result;
}

bool pcm_alsa_switch_playback_device_with_previous(
    const char *device, char *previous, size_t previous_size)
{
    pthread_mutex_lock(&fake_mutex);
    size_t length = strlen(fake_pcm_device);
    bool result = previous && length < previous_size;
    if (result)
    {
        memcpy(previous, fake_pcm_device, length + 1);
        result = fake_switch_playback_device_locked(device);
    }
    else if (previous && previous_size != 0)
        previous[0] = '\0';
    pthread_mutex_unlock(&fake_mutex);
    return result;
}

void hiby_audiohw_set_local_volume(int vol_l, int vol_r)
{
    pthread_mutex_lock(&fake_mutex);
    fake_local_volume_calls++;
    fake_local_left = vol_l;
    fake_local_right = vol_r;
    pthread_mutex_unlock(&fake_mutex);
}
#endif /* !SIMULATOR */

static struct ap80_bluetooth_state wait_for_operation(
    enum ap80_bluetooth_operation operation, uint32_t old_generation)
{
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 1000000,
    };
    struct ap80_bluetooth_state state;
    for (unsigned int attempt = 0; attempt < 5000; ++attempt)
    {
        ap80_bluetooth_get_state(&state);
        if (!state.busy && state.last_operation == operation &&
            state.generation > old_generation)
            return state;
        nanosleep(&delay, NULL);
    }
    assert(!"Bluetooth operation timed out");
    return state;
}

static struct ap80_bluetooth_state queue_power(bool enabled)
{
    struct ap80_bluetooth_state state;
    ap80_bluetooth_get_state(&state);
    assert(ap80_bluetooth_set_power(enabled) == 0);
    return wait_for_operation(enabled ? AP80_BT_OP_POWER_ON :
                                        AP80_BT_OP_POWER_OFF,
                              state.generation);
}

static struct ap80_bluetooth_state queue_scan(bool enabled)
{
    struct ap80_bluetooth_state state;
    ap80_bluetooth_get_state(&state);
    assert(ap80_bluetooth_set_scan(enabled) == 0);
    return wait_for_operation(enabled ? AP80_BT_OP_SCAN_START :
                                        AP80_BT_OP_SCAN_STOP,
                              state.generation);
}

static struct ap80_bluetooth_state queue_address_operation(
    enum ap80_bluetooth_operation operation, const char *address)
{
    struct ap80_bluetooth_state state;
    ap80_bluetooth_get_state(&state);
    int result;
    if (operation == AP80_BT_OP_PAIR_TRUST)
        result = ap80_bluetooth_pair_trust(address);
    else if (operation == AP80_BT_OP_CONNECT)
        result = ap80_bluetooth_connect(address);
    else
        result = ap80_bluetooth_disconnect(address);
    assert(result == 0);
    return wait_for_operation(operation, state.generation);
}

#ifndef SIMULATOR
static void test_production_controller(void)
{
    assert(ap80_bluetooth_set_power(true) == -ENODEV);
    assert(ap80_bluetooth_connect("invalid") == -EINVAL);
    pthread_mutex_lock(&fake_mutex);
    fake_powered = true; /* rfkill/daemons alone must not imply Adapter Powered */
    pthread_mutex_unlock(&fake_mutex);
    assert(ap80_bluetooth_init() == 0);
    assert(ap80_bluetooth_init() == 0);

    struct ap80_bluetooth_state state;
    ap80_bluetooth_get_state(&state);
    assert(!state.powered);
    state = queue_power(true);
    assert(state.last_error == 0);
    assert(state.powered);

    state = queue_scan(true);
    assert(state.last_error == 0);
    assert(!state.scanning);
    assert(state.device_count == 2);

    struct ap80_bluetooth_device devices[AP80_BLUETOOTH_MAX_DEVICES];
    size_t count = ap80_bluetooth_get_devices(
        devices, AP80_BLUETOOTH_MAX_DEVICES);
    assert(count == 2);
    assert(strcmp(devices[0].address, SPEAKER_ADDRESS) == 0);
    assert(strcmp(devices[0].name, "AP80 BT Speaker") == 0);
    assert(!devices[0].paired);
    assert(devices[1].paired);

    state = queue_scan(false);
    assert(state.last_error == 0);
    assert(!state.scanning);
    pthread_mutex_lock(&fake_mutex);
    assert(fake_saw_bounded_scan);
    assert(!fake_saw_scan_off);
    pthread_mutex_unlock(&fake_mutex);

    /* Persisted paired/trusted devices must not be paired a second time. */
    state = queue_address_operation(AP80_BT_OP_PAIR_TRUST,
                                    HEADPHONE_ADDRESS);
    assert(state.last_error == 0);
    count = ap80_bluetooth_get_devices(devices,
                                       AP80_BLUETOOTH_MAX_DEVICES);
    assert(count == 2);
    assert(devices[1].paired);
    assert(devices[1].trusted);

    /* A zero CLI exit is not success unless BlueZ reports the postcondition. */
    pthread_mutex_lock(&fake_mutex);
    fake_pair_effect = false;
    pthread_mutex_unlock(&fake_mutex);
    state = queue_address_operation(AP80_BT_OP_PAIR_TRUST, SPEAKER_ADDRESS);
    assert(state.last_error == -EIO);
    pthread_mutex_lock(&fake_mutex);
    fake_pair_effect = true;
    fake_speaker_trusted = false;
    pthread_mutex_unlock(&fake_mutex);

    state = queue_address_operation(AP80_BT_OP_PAIR_TRUST, SPEAKER_ADDRESS);
    assert(state.last_error == 0);
    count = ap80_bluetooth_get_devices(devices,
                                       AP80_BLUETOOTH_MAX_DEVICES);
    assert(count == 2);
    assert(devices[0].paired);
    assert(devices[0].trusted);

    state = queue_address_operation(AP80_BT_OP_CONNECT, SPEAKER_ADDRESS);
    assert(state.last_error == 0);
    assert(state.connected);
    assert(state.audio_ready);
    assert(state.route_active);
    assert(strcmp(state.codec, "SBC") == 0);
    pthread_mutex_lock(&fake_mutex);
    assert(strncmp(fake_pcm_device, "bluealsa:",
                   strlen("bluealsa:")) == 0);
    assert(fake_volume_calls >= 1);
    assert(fake_bluealsa_left == 64);
    assert(fake_bluealsa_right == 64);
    pthread_mutex_unlock(&fake_mutex);

    assert(ap80_bluetooth_queue_volume(-510, -1020));
    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 1000000,
    };
    for (unsigned int attempt = 0; attempt < 5000; ++attempt)
    {
        pthread_mutex_lock(&fake_mutex);
        bool updated = fake_volume_calls >= 2;
        pthread_mutex_unlock(&fake_mutex);
        if (updated)
            break;
        nanosleep(&delay, NULL);
    }
    pthread_mutex_lock(&fake_mutex);
    assert(fake_bluealsa_left == 64);
    assert(fake_bluealsa_right == 0);
    fake_endpoint_available = false;
    fake_connected = false;
    fake_local_switch_allowed = false;
    pthread_mutex_unlock(&fake_mutex);

    /* A failed local transition must keep ownership of the stale BT route. */
    for (unsigned int attempt = 0; attempt < 5000; ++attempt)
    {
        pthread_mutex_lock(&fake_mutex);
        bool attempted = fake_local_switch_failures != 0;
        pthread_mutex_unlock(&fake_mutex);
        if (attempted)
            break;
        nanosleep(&delay, NULL);
    }
    ap80_bluetooth_get_state(&state);
    assert(state.route_active);
    assert(!state.audio_ready);
    pthread_mutex_lock(&fake_mutex);
    assert(fake_local_switch_failures != 0);
    fake_local_switch_allowed = true;
    pthread_mutex_unlock(&fake_mutex);

    for (unsigned int attempt = 0; attempt < 5000; ++attempt)
    {
        ap80_bluetooth_get_state(&state);
        if (!state.route_active)
            break;
        nanosleep(&delay, NULL);
    }
    assert(!state.route_active);
    assert(!state.audio_ready);
    assert(!state.connected);
    pthread_mutex_lock(&fake_mutex);
    assert(strcmp(fake_pcm_device, "hw:0,1") == 0);
    assert(fake_local_volume_calls != 0);
    assert(fake_local_left == -510);
    assert(fake_local_right == -1020);
    fake_endpoint_available = true;
    pthread_mutex_unlock(&fake_mutex);

    bool saw_fallback = false;
    struct ap80_bluetooth_event event;
    while (ap80_bluetooth_next_event(&event))
    {
        if (event.type == AP80_BT_EVENT_ROUTE_FALLBACK)
            saw_fallback = true;
    }
    assert(saw_fallback);

    /* Codec reporting is advisory; a ready PCM still routes as Unknown. */
    pthread_mutex_lock(&fake_mutex);
    fake_codec_available = false;
    pthread_mutex_unlock(&fake_mutex);
    state = queue_address_operation(AP80_BT_OP_CONNECT, SPEAKER_ADDRESS);
    assert(state.last_error == 0);
    assert(state.route_active);
    assert(strcmp(state.codec, "Unknown") == 0);

    pthread_mutex_lock(&fake_mutex);
    fake_disconnect_effect = false;
    pthread_mutex_unlock(&fake_mutex);
    state = queue_address_operation(AP80_BT_OP_DISCONNECT,
                                    SPEAKER_ADDRESS);
    assert(state.last_error == -EBUSY);
    assert(state.connected);
    assert(!state.route_active);
    pthread_mutex_lock(&fake_mutex);
    fake_disconnect_effect = true;
    pthread_mutex_unlock(&fake_mutex);

    state = queue_address_operation(AP80_BT_OP_DISCONNECT,
                                    SPEAKER_ADDRESS);
    assert(state.last_error == 0);

    /* A DSD/capture-style PCM rejection leaves the local route untouched. */
    pthread_mutex_lock(&fake_mutex);
    fake_codec_available = true;
    fake_switch_allowed = false;
    pthread_mutex_unlock(&fake_mutex);
    state = queue_address_operation(AP80_BT_OP_CONNECT, SPEAKER_ADDRESS);
    assert(state.last_error == -EBUSY);
    assert(state.connected);
    assert(!state.audio_ready);
    assert(!state.route_active);
    pthread_mutex_lock(&fake_mutex);
    assert(strcmp(fake_pcm_device, "hw:0,1") == 0);
    fake_switch_allowed = true;
    pthread_mutex_unlock(&fake_mutex);

    assert(!ap80_bluetooth_queue_volume(-200, -200));

    /* A valid BlueZ link remains retryable after the audio route rejection. */
    state = queue_address_operation(AP80_BT_OP_CONNECT, SPEAKER_ADDRESS);
    assert(state.last_error == 0);
    assert(state.connected);
    assert(state.audio_ready);
    assert(state.route_active);

    state = queue_address_operation(AP80_BT_OP_DISCONNECT,
                                    SPEAKER_ADDRESS);
    assert(state.last_error == 0);
    assert(!state.connected);

    state = queue_power(false);
    assert(state.last_error == 0);
    assert(!state.powered);
    ap80_bluetooth_shutdown();
    ap80_bluetooth_get_state(&state);
    assert(!state.initialized);
}
#else
static void test_simulator_controller(void)
{
    assert(ap80_bluetooth_init() == 0);
    struct ap80_bluetooth_state state = queue_power(true);
    assert(state.powered);
    assert(state.last_error == 0);

    state = queue_scan(true);
    assert(!state.scanning);
    assert(state.device_count == 2);
    struct ap80_bluetooth_device devices[AP80_BLUETOOTH_MAX_DEVICES];
    assert(ap80_bluetooth_get_devices(
               devices, AP80_BLUETOOTH_MAX_DEVICES) == 2);
    assert(strcmp(devices[0].address, "02:00:00:00:80:01") == 0);
    assert(strcmp(devices[0].name, "AP80 BT Speaker") == 0);
    assert(!devices[0].paired);
    assert(strcmp(devices[1].address, "02:00:00:00:80:02") == 0);
    assert(devices[1].paired);
    assert(devices[1].trusted);

    state = queue_address_operation(AP80_BT_OP_CONNECT,
                                    "02:00:00:00:80:01");
    assert(state.last_error == -EACCES);
    state = queue_address_operation(AP80_BT_OP_PAIR_TRUST,
                                    "02:00:00:00:80:01");
    assert(state.last_error == 0);
    state = queue_address_operation(AP80_BT_OP_CONNECT,
                                    "02:00:00:00:80:01");
    assert(state.last_error == 0);
    assert(state.connected);
    assert(state.audio_ready);
    assert(state.route_active);
    assert(strcmp(state.codec, "SBC") == 0);
    assert(ap80_bluetooth_queue_volume(-510, -510));

    state = queue_address_operation(AP80_BT_OP_DISCONNECT,
                                    "02:00:00:00:80:01");
    assert(state.last_error == 0);
    assert(!state.connected);
    state = queue_scan(false);
    assert(!state.scanning);
    state = queue_power(false);
    assert(!state.powered);
    ap80_bluetooth_shutdown();
}
#endif

int main(void)
{
#ifdef SIMULATOR
    test_simulator_controller();
    puts("AP80 Bluetooth simulator tests passed");
#else
    test_production_controller();
    puts("AP80 Bluetooth controller tests passed");
#endif
    return 0;
}
