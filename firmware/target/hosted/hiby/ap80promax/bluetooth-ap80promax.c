/***************************************************************************
 * AP80 Pro Max Bluetooth controller.
 *
 * Keep BlueZ/BlueALSA control off the UI thread and keep audio transport in
 * the existing ALSA backend.  This module owns control-plane state only.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#define _POSIX_C_SOURCE 200809L

#include "bluetooth-ap80promax.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef SIMULATOR
#include <poll.h>

#include "services-ap80promax.h"
#ifdef AP80_BLUETOOTH_TEST
bool pcm_alsa_switch_playback_device(const char *device);
bool pcm_alsa_switch_playback_device_with_previous(
    const char *device, char *previous, size_t previous_size);
void hiby_audiohw_set_local_volume(int vol_l, int vol_r);
#else
#include "pcm-alsa.h"
void hiby_audiohw_set_local_volume(int vol_l, int vol_r);
#endif

#endif

#define AP80_BT_REQUEST_CAPACITY 8
#define AP80_BT_EVENT_CAPACITY 16
#define AP80_BT_PCM_DEVICE_SIZE 96

#ifndef AP80_BT_ROUTE_MONITOR_MS
#define AP80_BT_ROUTE_MONITOR_MS 5000u
#endif
#ifndef AP80_BT_ROUTE_MISSES
#define AP80_BT_ROUTE_MISSES 2u
#endif

#ifndef SIMULATOR
#define BLUETOOTHCTL_PATH "/usr/bin/bluetoothctl"
#define BLUEALSA_CLI_PATH "/usr/bin/bluealsa-cli"
#define BT_COMMAND_TIMEOUT_MS 3000u
#define BT_PAIR_TIMEOUT_MS 30000u
#define BT_CONNECT_TIMEOUT_MS 15000u
#define BT_SCAN_WINDOW_SECONDS "3"
#define BT_SCAN_COMMAND_TIMEOUT_MS 5000u
#define BLUEALSA_COMMAND_TIMEOUT_MS 2000u
#define BLUEALSA_READY_TIMEOUT_MS 8000u
#define AP80_LOCAL_PLAYBACK_FALLBACK "hw:0,0"
#endif

struct ap80_bt_request
{
    enum ap80_bluetooth_operation operation;
    char address[AP80_BLUETOOTH_ADDRESS_SIZE];
};

struct ap80_bt_controller
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    bool worker_started;
    bool stopping;

    struct ap80_bluetooth_state state;
    struct ap80_bluetooth_device devices[AP80_BLUETOOTH_MAX_DEVICES];

    struct ap80_bt_request requests[AP80_BT_REQUEST_CAPACITY];
    size_t request_head;
    size_t request_count;

    struct ap80_bluetooth_event events[AP80_BT_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;

    bool volume_known;
    bool volume_pending;
    int volume_l;
    int volume_r;
    uint32_t volume_generation;

    uint64_t next_route_monitor;
    unsigned int route_misses;

#ifndef SIMULATOR
    char local_device[AP80_BT_PCM_DEVICE_SIZE];
    char bluetooth_device[AP80_BT_PCM_DEVICE_SIZE];
    bool local_device_valid;
#endif
};

static struct ap80_bt_controller controller = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
};

#ifndef SIMULATOR
static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000u +
           (uint64_t)now.tv_nsec / 1000000u;
}
#endif

static void copy_string(char *destination, size_t destination_size,
                        const char *source)
{
    if (destination_size == 0)
        return;
    if (!source)
        source = "";
    snprintf(destination, destination_size, "%s", source);
}

static bool normalize_address(const char *address,
                              char normalized[AP80_BLUETOOTH_ADDRESS_SIZE])
{
    if (!address || strlen(address) != AP80_BLUETOOTH_ADDRESS_SIZE - 1)
        return false;

    for (size_t i = 0; i < AP80_BLUETOOTH_ADDRESS_SIZE - 1; ++i)
    {
        if (i % 3 == 2)
        {
            if (address[i] != ':')
                return false;
            normalized[i] = ':';
        }
        else
        {
            unsigned char character = (unsigned char)address[i];
            if (!isxdigit(character))
                return false;
            normalized[i] = (char)toupper(character);
        }
    }
    normalized[AP80_BLUETOOTH_ADDRESS_SIZE - 1] = '\0';
    return true;
}

static void push_event_locked(enum ap80_bluetooth_event_type type,
                              enum ap80_bluetooth_operation operation,
                              int error)
{
    controller.state.generation++;
    if (controller.event_count == AP80_BT_EVENT_CAPACITY)
    {
        controller.event_head =
            (controller.event_head + 1) % AP80_BT_EVENT_CAPACITY;
        controller.event_count--;
    }

    size_t index = (controller.event_head + controller.event_count) %
                   AP80_BT_EVENT_CAPACITY;
    controller.events[index] = (struct ap80_bluetooth_event) {
        .type = type,
        .operation = operation,
        .error = error,
        .generation = controller.state.generation,
    };
    controller.event_count++;
}

static int find_device_locked(const char *address)
{
    for (size_t i = 0; i < controller.state.device_count; ++i)
    {
        if (strcmp(controller.devices[i].address, address) == 0)
            return (int)i;
    }
    return -1;
}

#ifndef SIMULATOR
static int ensure_device_locked(const char *address)
{
    int index = find_device_locked(address);
    if (index >= 0)
        return index;
    if (controller.state.device_count >= AP80_BLUETOOTH_MAX_DEVICES)
        return -1;

    index = (int)controller.state.device_count++;
    memset(&controller.devices[index], 0, sizeof(controller.devices[index]));
    copy_string(controller.devices[index].address,
                sizeof(controller.devices[index].address), address);
    copy_string(controller.devices[index].name,
                sizeof(controller.devices[index].name), address);
    return index;
}
#endif

static void mark_device_connection_locked(const char *address, bool connected)
{
    int index = find_device_locked(address);
    if (index >= 0)
        controller.devices[index].connected = connected;
}

static void clear_connection_locked(void)
{
    if (controller.state.active_address[0] != '\0')
        mark_device_connection_locked(controller.state.active_address, false);
    controller.state.connected = false;
    controller.state.audio_ready = false;
    controller.state.route_active = false;
    controller.state.active_address[0] = '\0';
    controller.state.active_name[0] = '\0';
    controller.state.codec[0] = '\0';
    controller.route_misses = 0;
    controller.volume_pending = false;
}

static void set_active_device_locked(const char *address, const char *name)
{
    if (controller.state.active_address[0] != '\0' &&
        strcmp(controller.state.active_address, address) != 0)
        mark_device_connection_locked(controller.state.active_address, false);

    copy_string(controller.state.active_address,
                sizeof(controller.state.active_address), address);
    copy_string(controller.state.active_name,
                sizeof(controller.state.active_name),
                name && name[0] != '\0' ? name : address);
    mark_device_connection_locked(address, true);
}

#ifndef SIMULATOR
static bool is_stopping(void)
{
    bool stopping;
    pthread_mutex_lock(&controller.mutex);
    stopping = controller.stopping;
    pthread_mutex_unlock(&controller.mutex);
    return stopping;
}

static void condition_wait_ms_locked(uint64_t delay_ms)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
        return;

    deadline.tv_sec += (time_t)(delay_ms / 1000u);
    deadline.tv_nsec += (long)(delay_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    (void)pthread_cond_timedwait(&controller.condition, &controller.mutex,
                                 &deadline);
}
#endif

#ifndef SIMULATOR

static bool output_has_property(const char *output, const char *property,
                                const char *value)
{
    if (!output || !property || !value)
        return false;

    char expected[96];
    int length = snprintf(expected, sizeof(expected), "%s: %s",
                          property, value);
    return length > 0 && (size_t)length < sizeof(expected) &&
           strstr(output, expected) != NULL;
}

static void parse_info_name(const char *output, char *name, size_t name_size)
{
    const char *properties[] = {"Alias:", "Name:"};
    name[0] = '\0';
    if (!output)
        return;

    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); ++i)
    {
        const char *start = strstr(output, properties[i]);
        if (!start)
            continue;
        start += strlen(properties[i]);
        while (*start == ' ' || *start == '\t')
            start++;

        size_t length = strcspn(start, "\r\n");
        if (length >= name_size)
            length = name_size - 1;
        memcpy(name, start, length);
        name[length] = '\0';
        return;
    }
}

static int query_bluez_info(const char *address, bool *connected,
                            bool *paired, bool *trusted,
                            char *name, size_t name_size)
{
    const char *const argv[] = {
        BLUETOOTHCTL_PATH, "info", address, NULL
    };
    char output[2048];
    int result = ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                               BT_COMMAND_TIMEOUT_MS, output,
                               sizeof(output));
    if (result != 0)
        return result;

    if (connected)
        *connected = output_has_property(output, "Connected", "yes");
    if (paired)
        *paired = output_has_property(output, "Paired", "yes");
    if (trusted)
        *trusted = output_has_property(output, "Trusted", "yes");
    if (name && name_size != 0)
        parse_info_name(output, name, name_size);
    return 0;
}

static int parse_device_lines(
    const char *output, bool paired,
    struct ap80_bluetooth_device devices[AP80_BLUETOOTH_MAX_DEVICES],
    size_t *device_count)
{
    const char *line = output;
    while (line && *line != '\0')
    {
        const char *line_end = strpbrk(line, "\r\n");
        size_t line_length = line_end ? (size_t)(line_end - line) :
                                       strlen(line);
        const char *device = strstr(line, "Device ");
        if (device && (size_t)(device - line) < line_length)
        {
            device += strlen("Device ");
            if ((size_t)(device - line) +
                AP80_BLUETOOTH_ADDRESS_SIZE - 1 <= line_length)
            {
                char raw[AP80_BLUETOOTH_ADDRESS_SIZE];
                char address[AP80_BLUETOOTH_ADDRESS_SIZE];
                memcpy(raw, device, sizeof(raw) - 1);
                raw[sizeof(raw) - 1] = '\0';
                if (normalize_address(raw, address))
                {
                    int index = -1;
                    for (size_t i = 0; i < *device_count; ++i)
                    {
                        if (strcmp(devices[i].address, address) == 0)
                        {
                            index = (int)i;
                            break;
                        }
                    }
                    if (index < 0 &&
                        *device_count < AP80_BLUETOOTH_MAX_DEVICES)
                    {
                        index = (int)(*device_count)++;
                        memset(&devices[index], 0, sizeof(devices[index]));
                        copy_string(devices[index].address,
                                    sizeof(devices[index].address), address);
                    }
                    if (index >= 0)
                    {
                        devices[index].paired |= paired;
                        const char *name = device + sizeof(raw) - 1;
                        while ((size_t)(name - line) < line_length &&
                               (*name == ' ' || *name == '\t'))
                            name++;
                        size_t name_length = line_length -
                                             (size_t)(name - line);
                        if (name_length != 0)
                        {
                            if (name_length >=
                                sizeof(devices[index].name))
                                name_length =
                                    sizeof(devices[index].name) - 1;
                            memcpy(devices[index].name, name, name_length);
                            devices[index].name[name_length] = '\0';
                        }
                        else if (devices[index].name[0] == '\0')
                            copy_string(devices[index].name,
                                        sizeof(devices[index].name),
                                        address);
                    }
                }
            }
        }

        if (!line_end)
            break;
        line = line_end + 1;
        if (*line == '\n' && line[-1] == '\r')
            line++;
    }
    return 0;
}

static int refresh_devices(void)
{
    const char *const devices_argv[] = {
        BLUETOOTHCTL_PATH, "devices", NULL
    };
    const char *const paired_argv[] = {
        BLUETOOTHCTL_PATH, "paired-devices", NULL
    };
    char devices_output[4096];
    char paired_output[4096];
    int result = ap80_run_argv(BLUETOOTHCTL_PATH, devices_argv,
                               BT_COMMAND_TIMEOUT_MS, devices_output,
                               sizeof(devices_output));
    if (result != 0)
        return result;

    result = ap80_run_argv(BLUETOOTHCTL_PATH, paired_argv,
                           BT_COMMAND_TIMEOUT_MS, paired_output,
                           sizeof(paired_output));
    if (result != 0)
        return result;

    struct ap80_bluetooth_device found[AP80_BLUETOOTH_MAX_DEVICES];
    memset(found, 0, sizeof(found));
    size_t found_count = 0;
    parse_device_lines(devices_output, false, found, &found_count);
    parse_device_lines(paired_output, true, found, &found_count);

    pthread_mutex_lock(&controller.mutex);
    for (size_t i = 0; i < found_count; ++i)
    {
        int old = find_device_locked(found[i].address);
        if (old >= 0)
            found[i].trusted = controller.devices[old].trusted;
        found[i].connected =
            controller.state.connected &&
            strcmp(controller.state.active_address,
                   found[i].address) == 0;
    }

    bool changed = found_count != controller.state.device_count ||
                   memcmp(found, controller.devices,
                          found_count * sizeof(found[0])) != 0;
    if (changed)
    {
        memcpy(controller.devices, found,
               found_count * sizeof(controller.devices[0]));
        if (found_count < AP80_BLUETOOTH_MAX_DEVICES)
        {
            memset(&controller.devices[found_count], 0,
                   (AP80_BLUETOOTH_MAX_DEVICES - found_count) *
                   sizeof(controller.devices[0]));
        }
        controller.state.device_count = found_count;
        push_event_locked(AP80_BT_EVENT_DEVICE_LIST, AP80_BT_OP_NONE, 0);
    }
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

static int run_scan_window(void)
{
    /*
     * bluetoothctl's scan command otherwise stays attached to discovery.
     * Its own timeout closes that D-Bus client cleanly; the process runner is
     * a second, slightly longer hard bound.
     */
    const char *const argv[] = {
        BLUETOOTHCTL_PATH, "--timeout", BT_SCAN_WINDOW_SECONDS,
        "scan", "on", NULL
    };
    return ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                         BT_SCAN_COMMAND_TIMEOUT_MS, NULL, 0);
}

static void bluealsa_object_path(
    const char *address, char path[AP80_BT_PCM_DEVICE_SIZE])
{
    char device[AP80_BLUETOOTH_ADDRESS_SIZE];
    copy_string(device, sizeof(device), address);
    for (size_t i = 0; device[i] != '\0'; ++i)
    {
        if (device[i] == ':')
            device[i] = '_';
    }
    snprintf(path, AP80_BT_PCM_DEVICE_SIZE,
             "/org/bluealsa/hci0/dev_%s/a2dpsrc/sink", device);
}

static void bluealsa_pcm_device(
    const char *address, char device[AP80_BT_PCM_DEVICE_SIZE])
{
    snprintf(device, AP80_BT_PCM_DEVICE_SIZE,
             "bluealsa:DEV=%s,PROFILE=a2dp", address);
}

static int wait_for_bluealsa(const char *address)
{
    char object_path[AP80_BT_PCM_DEVICE_SIZE];
    bluealsa_object_path(address, object_path);
    const char *const argv[] = {
        BLUEALSA_CLI_PATH, "list-pcms", NULL
    };

    uint64_t deadline = monotonic_ms() + BLUEALSA_READY_TIMEOUT_MS;
    int last_error = -ETIMEDOUT;
    do
    {
        char output[4096];
        int result = ap80_run_argv(BLUEALSA_CLI_PATH, argv,
                                   BLUEALSA_COMMAND_TIMEOUT_MS, output,
                                   sizeof(output));
        if (result == 0)
        {
            if (strstr(output, object_path) != NULL)
                return 0;
            last_error = -ETIMEDOUT;
        }
        else
            last_error = result;

        if (is_stopping())
            return -ECANCELED;
        (void)poll(NULL, 0, 100);
    }
    while (monotonic_ms() < deadline);

    return last_error;
}

static int query_bluealsa_codec(const char *address, char *codec,
                                size_t codec_size)
{
    char object_path[AP80_BT_PCM_DEVICE_SIZE];
    bluealsa_object_path(address, object_path);
    const char *const argv[] = {
        BLUEALSA_CLI_PATH, "codec", object_path, NULL
    };
    char output[256];
    int result = ap80_run_argv(BLUEALSA_CLI_PATH, argv,
                               BLUEALSA_COMMAND_TIMEOUT_MS, output,
                               sizeof(output));
    if (result != 0)
        return result;

    const char *start = strstr(output, "Selected codec:");
    if (start)
        start += strlen("Selected codec:");
    else
        start = output;
    while (*start == ' ' || *start == '\t')
        start++;

    size_t length = strcspn(start, " \t\r\n");
    if (length == 0)
        return -EIO;
    if (length >= codec_size)
        length = codec_size - 1;
    memcpy(codec, start, length);
    codec[length] = '\0';
    return 0;
}

static int map_volume(int volume)
{
    if (volume < -1020)
        volume = -1020;
    if (volume > 0)
        volume = 0;
    return ((volume + 1020) * 127 + 510) / 1020;
}

static int set_bluealsa_volume(const char *address, int left, int right,
                               bool mapped)
{
    char object_path[AP80_BT_PCM_DEVICE_SIZE];
    char left_text[8];
    char right_text[8];
    bluealsa_object_path(address, object_path);
    if (mapped)
    {
        left = map_volume(left);
        right = map_volume(right);
    }
    snprintf(left_text, sizeof(left_text), "%d", left);
    snprintf(right_text, sizeof(right_text), "%d", right);

    const char *const argv[] = {
        BLUEALSA_CLI_PATH, "volume", object_path,
        left_text, right_text, NULL
    };
    return ap80_run_argv(BLUEALSA_CLI_PATH, argv,
                         BLUEALSA_COMMAND_TIMEOUT_MS, NULL, 0);
}

static int restore_local_route(void)
{
    bool route_active;
    const char *local_device;

    pthread_mutex_lock(&controller.mutex);
    route_active = controller.state.route_active;
    if (!controller.local_device_valid)
    {
        copy_string(controller.local_device,
                    sizeof(controller.local_device),
                    AP80_LOCAL_PLAYBACK_FALLBACK);
        controller.local_device_valid = true;
    }
    local_device = controller.local_device;
    pthread_mutex_unlock(&controller.mutex);

    if (!route_active)
        return 0;
    if (!pcm_alsa_switch_playback_device(local_device))
        return -EIO;

    pthread_mutex_lock(&controller.mutex);
    controller.state.route_active = false;
    controller.volume_pending = false;
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                      AP80_BT_OP_NONE, 0);
    pthread_mutex_unlock(&controller.mutex);

    /*
     * Knob events remain intercepted while the PCM transition runs. After
     * releasing route ownership, apply the latest remembered generation
     * directly to the local codec. If a concurrent knob event raced our
     * write, repeat with its newer value; once the generation is stable,
     * subsequent events already target the local codec themselves.
     */
    for (;;)
    {
        pthread_mutex_lock(&controller.mutex);
        bool volume_known = controller.volume_known;
        int volume_l = controller.volume_l;
        int volume_r = controller.volume_r;
        uint32_t volume_generation = controller.volume_generation;
        pthread_mutex_unlock(&controller.mutex);

        if (!volume_known)
            break;
        hiby_audiohw_set_local_volume(volume_l, volume_r);

        pthread_mutex_lock(&controller.mutex);
        bool current =
            controller.volume_generation == volume_generation;
        pthread_mutex_unlock(&controller.mutex);
        if (current)
            break;
    }
    return 0;
}

static int process_power(bool enabled)
{
    if (enabled)
    {
        int result = ap80_services_set_enabled(AP80_SERVICE_BLUETOOTH, true);
        if (result != 0)
            return result;

        const char *const argv[] = {
            BLUETOOTHCTL_PATH, "power", "on", NULL
        };
        result = ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                               BT_COMMAND_TIMEOUT_MS, NULL, 0);
        if (result != 0)
            return result;

        pthread_mutex_lock(&controller.mutex);
        controller.state.powered = true;
        push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                          AP80_BT_OP_POWER_ON, 0);
        pthread_mutex_unlock(&controller.mutex);
        return 0;
    }

    int result = restore_local_route();
    if (result != 0)
        return result;

    int first_error = 0;
    pthread_mutex_lock(&controller.mutex);
    bool scanning = controller.state.scanning;
    pthread_mutex_unlock(&controller.mutex);
    if (scanning)
    {
        const char *const scan_argv[] = {
            BLUETOOTHCTL_PATH, "scan", "off", NULL
        };
        first_error = ap80_run_argv(BLUETOOTHCTL_PATH, scan_argv,
                                    BT_COMMAND_TIMEOUT_MS, NULL, 0);
    }

    const char *const power_argv[] = {
        BLUETOOTHCTL_PATH, "power", "off", NULL
    };
    int command_result = ap80_run_argv(BLUETOOTHCTL_PATH, power_argv,
                                       BT_COMMAND_TIMEOUT_MS, NULL, 0);
    if (first_error == 0)
        first_error = command_result;

    int service_result =
        ap80_services_set_enabled(AP80_SERVICE_BLUETOOTH, false);
    if (first_error == 0)
        first_error = service_result;
    if (service_result != 0)
        return first_error;

    pthread_mutex_lock(&controller.mutex);
    controller.state.powered = false;
    controller.state.scanning = false;
    clear_connection_locked();
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                      AP80_BT_OP_POWER_OFF, first_error);
    pthread_mutex_unlock(&controller.mutex);
    return first_error;
}

static int process_scan(bool enabled)
{
    pthread_mutex_lock(&controller.mutex);
    bool powered = controller.state.powered;
    bool scanning = controller.state.scanning;
    pthread_mutex_unlock(&controller.mutex);

    if (enabled && !powered)
        return -EHOSTDOWN;
    if (!enabled && !scanning)
        return 0;

    if (enabled)
    {
        pthread_mutex_lock(&controller.mutex);
        controller.state.scanning = true;
        push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                          AP80_BT_OP_SCAN_START, 0);
        pthread_mutex_unlock(&controller.mutex);

        int result = run_scan_window();
        (void)refresh_devices();

        pthread_mutex_lock(&controller.mutex);
        controller.state.scanning = false;
        push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                          AP80_BT_OP_SCAN_START, result);
        pthread_mutex_unlock(&controller.mutex);
        return result;
    }

    {
        const char *const argv[] = {
            BLUETOOTHCTL_PATH, "scan", "off", NULL
        };
        int result = ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                                   BT_COMMAND_TIMEOUT_MS, NULL, 0);
        if (result == 0)
        {
            pthread_mutex_lock(&controller.mutex);
            controller.state.scanning = false;
            push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                              AP80_BT_OP_SCAN_STOP, 0);
            pthread_mutex_unlock(&controller.mutex);
        }
        return result;
    }
}

static int process_pair_trust(const char *address)
{
    pthread_mutex_lock(&controller.mutex);
    bool powered = controller.state.powered;
    pthread_mutex_unlock(&controller.mutex);
    if (!powered)
        return -EHOSTDOWN;

    bool connected = false;
    bool paired = false;
    bool trusted = false;
    char name[AP80_BLUETOOTH_NAME_SIZE] = "";
    int info_result = query_bluez_info(address, &connected,
                                       &paired, &trusted,
                                       name, sizeof(name));

    if (!paired)
    {
        const char *const pair_argv[] = {
            BLUETOOTHCTL_PATH, "pair", address, NULL
        };
        int result = ap80_run_argv(BLUETOOTHCTL_PATH, pair_argv,
                                   BT_PAIR_TIMEOUT_MS, NULL, 0);
        if (result != 0)
        {
            info_result = query_bluez_info(address, &connected,
                                           &paired, &trusted,
                                           name, sizeof(name));
            if (info_result != 0 || !paired)
                return result;
        }
        else
            paired = true;
    }

    if (!trusted)
    {
        const char *const trust_argv[] = {
            BLUETOOTHCTL_PATH, "trust", address, NULL
        };
        int result = ap80_run_argv(BLUETOOTHCTL_PATH, trust_argv,
                                   BT_COMMAND_TIMEOUT_MS, NULL, 0);
        if (result != 0)
        {
            info_result = query_bluez_info(address, &connected,
                                           &paired, &trusted,
                                           name, sizeof(name));
            if (info_result != 0 || !trusted)
                return result;
        }
        else
            trusted = true;
    }

    info_result = query_bluez_info(address, &connected,
                                   &paired, &trusted,
                                   name, sizeof(name));
    if (info_result != 0)
        return info_result;
    if (!paired || !trusted)
        return -EIO;

    pthread_mutex_lock(&controller.mutex);
    int index = ensure_device_locked(address);
    if (index >= 0)
    {
        controller.devices[index].paired = paired;
        controller.devices[index].trusted = trusted;
        if (name[0] != '\0')
            copy_string(controller.devices[index].name,
                        sizeof(controller.devices[index].name), name);
    }
    if (info_result == 0 && connected)
    {
        set_active_device_locked(address, name);
        controller.state.connected = true;
        controller.state.audio_ready = false;
    }
    push_event_locked(AP80_BT_EVENT_DEVICE_LIST,
                      AP80_BT_OP_PAIR_TRUST, 0);
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

static void reconcile_failed_audio_route(const char *address)
{
    bool connected = false;
    char name[AP80_BLUETOOTH_NAME_SIZE] = "";
    int info_result = query_bluez_info(address, &connected, NULL, NULL,
                                       name, sizeof(name));

    pthread_mutex_lock(&controller.mutex);
    if (strcmp(controller.state.active_address, address) == 0)
    {
        controller.state.audio_ready = false;
        controller.state.route_active = false;
        controller.state.codec[0] = '\0';
        controller.route_misses = 0;

        if (info_result == 0 && !connected)
            clear_connection_locked();
        else
        {
            /*
             * A valid BlueZ link may remain after BlueALSA or ALSA routing
             * fails. Preserve it so selecting the device again retries only
             * the audio path; an unknown query result is not evidence that
             * the link disappeared.
             */
            controller.state.connected = true;
            if (name[0] != '\0')
            {
                copy_string(controller.state.active_name,
                            sizeof(controller.state.active_name), name);
            }
            mark_device_connection_locked(address, true);
            controller.next_route_monitor = monotonic_ms() +
                                            AP80_BT_ROUTE_MONITOR_MS;
        }
        push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                          AP80_BT_OP_CONNECT, 0);
    }
    pthread_mutex_unlock(&controller.mutex);
}

static int process_connect(const char *address)
{
    pthread_mutex_lock(&controller.mutex);
    bool powered = controller.state.powered;
    bool route_active = controller.state.route_active;
    pthread_mutex_unlock(&controller.mutex);
    if (!powered)
        return -EHOSTDOWN;
    if (route_active)
        return -EALREADY;

    bool connected = false;
    char name[AP80_BLUETOOTH_NAME_SIZE] = "";
    int info_result = query_bluez_info(address, &connected,
                                       NULL, NULL, name, sizeof(name));
    if (info_result != 0 || !connected)
    {
        const char *const argv[] = {
            BLUETOOTHCTL_PATH, "connect", address, NULL
        };
        int result = ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                                   BT_CONNECT_TIMEOUT_MS, NULL, 0);
        if (result != 0)
            return result;

        result = query_bluez_info(address, &connected, NULL, NULL,
                                  name, sizeof(name));
        if (result != 0)
            return result;
        if (!connected)
            return -ENOTCONN;
    }

    pthread_mutex_lock(&controller.mutex);
    int index = ensure_device_locked(address);
    if (index >= 0 && name[0] != '\0')
        copy_string(controller.devices[index].name,
                    sizeof(controller.devices[index].name), name);
    set_active_device_locked(address, name);
    controller.state.connected = true;
    controller.state.audio_ready = false;
    controller.next_route_monitor = monotonic_ms() +
                                    AP80_BT_ROUTE_MONITOR_MS;
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                      AP80_BT_OP_CONNECT, 0);
    pthread_mutex_unlock(&controller.mutex);

    int result = wait_for_bluealsa(address);
    if (result != 0)
    {
        reconcile_failed_audio_route(address);
        return result;
    }

    char codec[AP80_BLUETOOTH_CODEC_SIZE] = "Unknown";
    (void)query_bluealsa_codec(address, codec, sizeof(codec));

    pthread_mutex_lock(&controller.mutex);
    controller.state.audio_ready = true;
    copy_string(controller.state.codec, sizeof(controller.state.codec),
                codec);
    bool volume_known = controller.volume_known;
    int volume_l = controller.volume_l;
    int volume_r = controller.volume_r;
    uint32_t volume_generation = controller.volume_generation;
    pthread_mutex_unlock(&controller.mutex);

    result = volume_known ?
             set_bluealsa_volume(address, volume_l, volume_r, true) :
             set_bluealsa_volume(address, 64, 64, false);
    if (result != 0)
    {
        reconcile_failed_audio_route(address);
        return result;
    }

    char local_device[AP80_BT_PCM_DEVICE_SIZE];
    char bluetooth_device[AP80_BT_PCM_DEVICE_SIZE];
    pthread_mutex_lock(&controller.mutex);
    bluealsa_pcm_device(address, controller.bluetooth_device);
    copy_string(bluetooth_device, sizeof(bluetooth_device),
                controller.bluetooth_device);
    /*
     * Reserve route ownership before touching PCM. Volume and local-output
     * changes will now wait for or reject this transactional transition.
     */
    controller.state.route_active = true;
    pthread_mutex_unlock(&controller.mutex);

    /*
     * This existing backend transition is transactional. In particular it
     * rejects DSD/capture without disturbing the local endpoint.
     */
    if (!pcm_alsa_switch_playback_device_with_previous(
            bluetooth_device, local_device, sizeof(local_device)))
    {
        reconcile_failed_audio_route(address);
        return -EBUSY;
    }

    pthread_mutex_lock(&controller.mutex);
    if (strncmp(local_device, "bluealsa:", strlen("bluealsa:")) == 0)
    {
        copy_string(local_device, sizeof(local_device),
                    AP80_LOCAL_PLAYBACK_FALLBACK);
    }
    copy_string(controller.local_device, sizeof(controller.local_device),
                local_device);
    controller.local_device_valid = true;
    controller.route_misses = 0;
    controller.next_route_monitor = monotonic_ms() +
                                    AP80_BT_ROUTE_MONITOR_MS;
    if (controller.volume_generation == volume_generation)
        controller.volume_pending = false;
    else
        controller.volume_pending = true;
    if (controller.volume_pending)
        pthread_cond_signal(&controller.condition);
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                      AP80_BT_OP_CONNECT, 0);
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

static int process_disconnect(const char *address)
{
    pthread_mutex_lock(&controller.mutex);
    bool active_matches =
        controller.state.active_address[0] == '\0' ||
        strcmp(controller.state.active_address, address) == 0;
    pthread_mutex_unlock(&controller.mutex);
    if (!active_matches)
        return -ENOTCONN;

    int result = restore_local_route();
    if (result != 0)
        return result;

    const char *const argv[] = {
        BLUETOOTHCTL_PATH, "disconnect", address, NULL
    };
    result = ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                           BT_COMMAND_TIMEOUT_MS, NULL, 0);
    bool still_connected = true;
    int info_result = query_bluez_info(address, &still_connected,
                                       NULL, NULL, NULL, 0);
    if (info_result != 0)
        return result != 0 ? result : info_result;
    if (still_connected)
        return result != 0 ? result : -EBUSY;

    pthread_mutex_lock(&controller.mutex);
    if (strcmp(controller.state.active_address, address) == 0)
        clear_connection_locked();
    else
        mark_device_connection_locked(address, false);
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                      AP80_BT_OP_DISCONNECT, 0);
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

static int process_operation(const struct ap80_bt_request *request)
{
    switch (request->operation)
    {
        case AP80_BT_OP_POWER_ON:
            return process_power(true);
        case AP80_BT_OP_POWER_OFF:
            return process_power(false);
        case AP80_BT_OP_SCAN_START:
            return process_scan(true);
        case AP80_BT_OP_SCAN_STOP:
            return process_scan(false);
        case AP80_BT_OP_PAIR_TRUST:
            return process_pair_trust(request->address);
        case AP80_BT_OP_CONNECT:
            return process_connect(request->address);
        case AP80_BT_OP_DISCONNECT:
            return process_disconnect(request->address);
        case AP80_BT_OP_NONE:
        default:
            return -EINVAL;
    }
}

static void process_pending_volume(void)
{
    char address[AP80_BLUETOOTH_ADDRESS_SIZE];
    int volume_l;
    int volume_r;

    pthread_mutex_lock(&controller.mutex);
    copy_string(address, sizeof(address), controller.state.active_address);
    volume_l = controller.volume_l;
    volume_r = controller.volume_r;
    controller.volume_pending = false;
    pthread_mutex_unlock(&controller.mutex);

    if (address[0] != '\0')
        (void)set_bluealsa_volume(address, volume_l, volume_r, true);
}

static void monitor_active_route(void)
{
    char address[AP80_BLUETOOTH_ADDRESS_SIZE];
    pthread_mutex_lock(&controller.mutex);
    copy_string(address, sizeof(address), controller.state.active_address);
    pthread_mutex_unlock(&controller.mutex);
    if (address[0] == '\0')
        return;

    char object_path[AP80_BT_PCM_DEVICE_SIZE];
    bluealsa_object_path(address, object_path);
    const char *const argv[] = {
        BLUEALSA_CLI_PATH, "list-pcms", NULL
    };
    char output[4096];
    int result = ap80_run_argv(BLUEALSA_CLI_PATH, argv,
                               BLUEALSA_COMMAND_TIMEOUT_MS, output,
                               sizeof(output));
    bool ready = result == 0 && strstr(output, object_path) != NULL;

    pthread_mutex_lock(&controller.mutex);
    if (ready)
    {
        controller.route_misses = 0;
        pthread_mutex_unlock(&controller.mutex);
        return;
    }
    controller.route_misses++;
    bool fallback = controller.route_misses >= AP80_BT_ROUTE_MISSES;
    pthread_mutex_unlock(&controller.mutex);
    if (!fallback)
        return;

    bool connected = false;
    int info_result = query_bluez_info(address, &connected, NULL, NULL,
                                       NULL, 0);
    int restore_result = restore_local_route();

    pthread_mutex_lock(&controller.mutex);
    controller.state.audio_ready = false;
    controller.state.connected = info_result == 0 && connected;
    controller.state.codec[0] = '\0';
    mark_device_connection_locked(address, controller.state.connected);
    /*
     * Keep the address while the stale ALSA route is still selected so the
     * next monitor pass can retry the local transition.
     */
    if (!controller.state.connected && restore_result == 0)
    {
        controller.state.active_address[0] = '\0';
        controller.state.active_name[0] = '\0';
    }
    controller.route_misses = 0;
    int error = result != 0 ? result :
                (restore_result != 0 ? restore_result : -ENODEV);
    push_event_locked(AP80_BT_EVENT_ROUTE_FALLBACK,
                      AP80_BT_OP_NONE, error);
    pthread_mutex_unlock(&controller.mutex);
}

#else /* SIMULATOR */

static const struct ap80_bluetooth_device simulator_devices[] = {
    {
        .address = "02:00:00:00:80:01",
        .name = "AP80 BT Speaker",
    },
    {
        .address = "02:00:00:00:80:02",
        .name = "AP80 BT Headphones",
        .paired = true,
        .trusted = true,
    },
};

static void simulator_populate_devices_locked(void)
{
    if (controller.state.device_count != 0)
        return;
    memcpy(controller.devices, simulator_devices, sizeof(simulator_devices));
    controller.state.device_count =
        sizeof(simulator_devices) / sizeof(simulator_devices[0]);
    push_event_locked(AP80_BT_EVENT_DEVICE_LIST,
                      AP80_BT_OP_SCAN_START, 0);
}

static int simulator_process_operation(const struct ap80_bt_request *request)
{
    pthread_mutex_lock(&controller.mutex);
    int result = 0;
    switch (request->operation)
    {
        case AP80_BT_OP_POWER_ON:
            controller.state.powered = true;
            break;
        case AP80_BT_OP_POWER_OFF:
            controller.state.powered = false;
            controller.state.scanning = false;
            clear_connection_locked();
            break;
        case AP80_BT_OP_SCAN_START:
            if (!controller.state.powered)
                result = -EHOSTDOWN;
            else
            {
                controller.state.scanning = true;
                simulator_populate_devices_locked();
                controller.state.scanning = false;
            }
            break;
        case AP80_BT_OP_SCAN_STOP:
            controller.state.scanning = false;
            break;
        case AP80_BT_OP_PAIR_TRUST:
        {
            if (!controller.state.powered)
            {
                result = -EHOSTDOWN;
                break;
            }
            int index = find_device_locked(request->address);
            if (index < 0)
                result = -ENODEV;
            else
            {
                controller.devices[index].paired = true;
                controller.devices[index].trusted = true;
                push_event_locked(AP80_BT_EVENT_DEVICE_LIST,
                                  request->operation, 0);
            }
            break;
        }
        case AP80_BT_OP_CONNECT:
        {
            if (!controller.state.powered)
            {
                result = -EHOSTDOWN;
                break;
            }
            int index = find_device_locked(request->address);
            if (index < 0)
                result = -ENODEV;
            else if (!controller.devices[index].paired)
                result = -EACCES;
            else
            {
                set_active_device_locked(
                    request->address, controller.devices[index].name);
                controller.state.connected = true;
                controller.state.audio_ready = true;
                controller.state.route_active = true;
                copy_string(controller.state.codec,
                            sizeof(controller.state.codec), "SBC");
            }
            break;
        }
        case AP80_BT_OP_DISCONNECT:
            if (strcmp(controller.state.active_address,
                       request->address) != 0)
                result = -ENOTCONN;
            else
                clear_connection_locked();
            break;
        case AP80_BT_OP_NONE:
        default:
            result = -EINVAL;
            break;
    }
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED,
                      request->operation, result);
    pthread_mutex_unlock(&controller.mutex);
    return result;
}

#endif /* SIMULATOR */

static bool dequeue_request_locked(struct ap80_bt_request *request)
{
    if (controller.request_count == 0)
        return false;
    *request = controller.requests[controller.request_head];
    controller.request_head =
        (controller.request_head + 1) % AP80_BT_REQUEST_CAPACITY;
    controller.request_count--;
    controller.state.busy = true;
    controller.state.operation = request->operation;
    return true;
}

static void finish_operation_locked(enum ap80_bluetooth_operation operation,
                                    int error)
{
    controller.state.last_operation = operation;
    controller.state.last_error = error;
    if (controller.request_count != 0)
    {
        controller.state.busy = true;
        controller.state.operation =
            controller.requests[controller.request_head].operation;
    }
    else
    {
        controller.state.busy = false;
        controller.state.operation = AP80_BT_OP_NONE;
    }
    push_event_locked(AP80_BT_EVENT_OPERATION_COMPLETE, operation, error);
}

static void *bluetooth_worker(void *unused)
{
    (void)unused;
    for (;;)
    {
        struct ap80_bt_request request;
        pthread_mutex_lock(&controller.mutex);
        if (controller.stopping)
        {
            pthread_mutex_unlock(&controller.mutex);
            break;
        }
        if (dequeue_request_locked(&request))
        {
            pthread_mutex_unlock(&controller.mutex);
#ifdef SIMULATOR
            int result = simulator_process_operation(&request);
#else
            int result = process_operation(&request);
#endif
            pthread_mutex_lock(&controller.mutex);
            finish_operation_locked(request.operation, result);
            pthread_mutex_unlock(&controller.mutex);
            continue;
        }

#ifndef SIMULATOR
        if (controller.volume_pending && controller.state.route_active)
        {
            pthread_mutex_unlock(&controller.mutex);
            process_pending_volume();
            continue;
        }

        uint64_t now = monotonic_ms();
        if ((controller.state.route_active || controller.state.connected) &&
            now >= controller.next_route_monitor)
        {
            controller.next_route_monitor =
                now + AP80_BT_ROUTE_MONITOR_MS;
            pthread_mutex_unlock(&controller.mutex);
            monitor_active_route();
            continue;
        }

        uint64_t next_wakeup = 0;
        if ((controller.state.route_active || controller.state.connected) &&
            next_wakeup == 0)
            next_wakeup = controller.next_route_monitor;
        if (next_wakeup != 0)
        {
            uint64_t delay = next_wakeup > now ? next_wakeup - now : 1;
            condition_wait_ms_locked(delay);
        }
        else
            (void)pthread_cond_wait(&controller.condition,
                                    &controller.mutex);
#else
        (void)pthread_cond_wait(&controller.condition, &controller.mutex);
#endif
        pthread_mutex_unlock(&controller.mutex);
    }

#ifndef SIMULATOR
    (void)restore_local_route();
    pthread_mutex_lock(&controller.mutex);
    bool scanning = controller.state.scanning;
    controller.state.scanning = false;
    pthread_mutex_unlock(&controller.mutex);
    if (scanning)
    {
        const char *const argv[] = {
            BLUETOOTHCTL_PATH, "scan", "off", NULL
        };
        (void)ap80_run_argv(BLUETOOTHCTL_PATH, argv,
                            BT_COMMAND_TIMEOUT_MS, NULL, 0);
    }
#endif
    return NULL;
}

int ap80_bluetooth_init(void)
{
    pthread_mutex_lock(&controller.mutex);
    if (controller.state.initialized)
    {
        pthread_mutex_unlock(&controller.mutex);
        return 0;
    }

#ifndef SIMULATOR
    int result = ap80_services_claim(AP80_SERVICE_BLUETOOTH);
    if (result != 0 && result != -EALREADY)
    {
        pthread_mutex_unlock(&controller.mutex);
        return result;
    }
#endif

    memset(&controller.state, 0, sizeof(controller.state));
    memset(controller.devices, 0, sizeof(controller.devices));
    controller.request_head = 0;
    controller.request_count = 0;
    controller.event_head = 0;
    controller.event_count = 0;
    controller.stopping = false;
    controller.route_misses = 0;
#ifndef SIMULATOR
    controller.local_device_valid = false;
    /*
     * rfkill and daemon presence do not imply BlueZ Adapter1.Powered.
     * Keep cold init non-blocking and require an explicit async power-on.
     */
    controller.state.powered = false;
#endif
    controller.state.initialized = true;
    push_event_locked(AP80_BT_EVENT_STATE_CHANGED, AP80_BT_OP_NONE, 0);

    pthread_attr_t attributes;
    int thread_result = pthread_attr_init(&attributes);
    if (thread_result == 0)
    {
        size_t stack_size = 64u * 1024u;
#ifdef PTHREAD_STACK_MIN
        if (stack_size < (size_t)PTHREAD_STACK_MIN)
            stack_size = (size_t)PTHREAD_STACK_MIN;
#endif
        (void)pthread_attr_setstacksize(&attributes, stack_size);
        thread_result = pthread_create(&controller.worker, &attributes,
                                       bluetooth_worker, NULL);
        pthread_attr_destroy(&attributes);
    }
    if (thread_result != 0)
    {
        controller.state.initialized = false;
        pthread_mutex_unlock(&controller.mutex);
        return -thread_result;
    }

    controller.worker_started = true;
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

void ap80_bluetooth_shutdown(void)
{
    pthread_mutex_lock(&controller.mutex);
    if (!controller.worker_started)
    {
        controller.state.initialized = false;
        pthread_mutex_unlock(&controller.mutex);
        return;
    }
    controller.stopping = true;
    pthread_cond_broadcast(&controller.condition);
    pthread_t worker = controller.worker;
    pthread_mutex_unlock(&controller.mutex);

    (void)pthread_join(worker, NULL);

    pthread_mutex_lock(&controller.mutex);
    controller.worker_started = false;
    controller.state.initialized = false;
    controller.state.busy = false;
    controller.state.operation = AP80_BT_OP_NONE;
    controller.request_count = 0;
    controller.volume_pending = false;
    pthread_mutex_unlock(&controller.mutex);
}

static int enqueue_operation(enum ap80_bluetooth_operation operation,
                             const char *address)
{
    char normalized[AP80_BLUETOOTH_ADDRESS_SIZE] = "";
    if (address && !normalize_address(address, normalized))
        return -EINVAL;

    pthread_mutex_lock(&controller.mutex);
    if (!controller.state.initialized || controller.stopping)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -ENODEV;
    }
    if (controller.request_count == AP80_BT_REQUEST_CAPACITY)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -EAGAIN;
    }

    size_t index = (controller.request_head + controller.request_count) %
                   AP80_BT_REQUEST_CAPACITY;
    controller.requests[index].operation = operation;
    copy_string(controller.requests[index].address,
                sizeof(controller.requests[index].address), normalized);
    controller.request_count++;
    if (!controller.state.busy)
    {
        controller.state.busy = true;
        controller.state.operation = operation;
        push_event_locked(AP80_BT_EVENT_STATE_CHANGED, operation, 0);
    }
    pthread_cond_signal(&controller.condition);
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

int ap80_bluetooth_set_power(bool enabled)
{
    return enqueue_operation(enabled ? AP80_BT_OP_POWER_ON :
                                       AP80_BT_OP_POWER_OFF, NULL);
}

int ap80_bluetooth_set_scan(bool enabled)
{
    return enqueue_operation(enabled ? AP80_BT_OP_SCAN_START :
                                       AP80_BT_OP_SCAN_STOP, NULL);
}

int ap80_bluetooth_pair_trust(const char *address)
{
    if (!address)
        return -EINVAL;
    return enqueue_operation(AP80_BT_OP_PAIR_TRUST, address);
}

int ap80_bluetooth_connect(const char *address)
{
    if (!address)
        return -EINVAL;
    return enqueue_operation(AP80_BT_OP_CONNECT, address);
}

int ap80_bluetooth_disconnect(const char *address)
{
    if (!address)
        return -EINVAL;
    return enqueue_operation(AP80_BT_OP_DISCONNECT, address);
}

void ap80_bluetooth_get_state(struct ap80_bluetooth_state *state)
{
    if (!state)
        return;
    pthread_mutex_lock(&controller.mutex);
    *state = controller.state;
    pthread_mutex_unlock(&controller.mutex);
}

size_t ap80_bluetooth_get_devices(struct ap80_bluetooth_device *devices,
                                  size_t capacity)
{
    pthread_mutex_lock(&controller.mutex);
    size_t count = controller.state.device_count;
    if (devices)
    {
        if (count > capacity)
            count = capacity;
        memcpy(devices, controller.devices,
               count * sizeof(controller.devices[0]));
    }
    pthread_mutex_unlock(&controller.mutex);
    return count;
}

bool ap80_bluetooth_next_event(struct ap80_bluetooth_event *event)
{
    if (!event)
        return false;
    pthread_mutex_lock(&controller.mutex);
    if (controller.event_count == 0)
    {
        pthread_mutex_unlock(&controller.mutex);
        return false;
    }

    *event = controller.events[controller.event_head];
    controller.event_head =
        (controller.event_head + 1) % AP80_BT_EVENT_CAPACITY;
    controller.event_count--;
    pthread_mutex_unlock(&controller.mutex);
    return true;
}

bool ap80_bluetooth_queue_volume(int vol_l, int vol_r)
{
    pthread_mutex_lock(&controller.mutex);
    controller.volume_known = true;
    controller.volume_l = vol_l;
    controller.volume_r = vol_r;
    controller.volume_generation++;
    bool consumed = controller.state.initialized &&
                    controller.state.route_active &&
                    !controller.stopping;
    if (consumed)
    {
        controller.volume_pending = true;
        pthread_cond_signal(&controller.condition);
    }
    pthread_mutex_unlock(&controller.mutex);
    return consumed;
}
