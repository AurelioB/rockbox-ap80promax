/***************************************************************************
 * AP80 Pro Max experimental connectivity menu.
 *
 * All radio operations are queued to the target workers. This module only
 * polls copied snapshots and keeps the UI responsive while work is pending.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "action.h"
#include "kernel.h"
#include "keyboard.h"
#include "menu.h"
#include "misc.h"
#include "splash.h"
#include "yesno.h"
#include "wifi-ap80promax.h"
#include "bluetooth-ap80promax.h"

#define CONNECTIVITY_POLL_TICKS (HZ / 5)
#define CONNECTIVITY_STATUS_LINES 12
#define CONNECTIVITY_STATUS_WIDTH 160

enum progress_result
{
    PROGRESS_DONE = 0,
    PROGRESS_CANCELLED = 1,
    PROGRESS_USB = 2,
};

enum choice_result
{
    CHOICE_CANCELLED = -1,
    CHOICE_USB = -3,
};

enum confirmation_result
{
    CONFIRM_CANCELLED = 0,
    CONFIRM_ACCEPTED,
    CONFIRM_USB,
};

struct string_list
{
    const char * const *items;
    int count;
};

struct status_list
{
    char lines[CONNECTIVITY_STATUS_LINES][CONNECTIVITY_STATUS_WIDTH];
    int count;
};

struct wifi_network_cache
{
    struct ap80_wifi_network networks[AP80_WIFI_MAX_NETWORKS];
    size_t count;
};

struct bluetooth_device_cache
{
    struct ap80_bluetooth_device devices[AP80_BLUETOOTH_MAX_DEVICES];
    size_t count;
    uint32_t generation;
};

static bool connectivity_init_attempted;
static int wifi_init_result;
static int bluetooth_init_result;
static struct wifi_network_cache wifi_cache;
static struct wifi_network_cache wifi_saved_cache;
static struct bluetooth_device_cache bluetooth_cache;
static struct status_list status_cache;

static void secure_wipe(void *buffer, size_t size)
{
    volatile unsigned char *p = buffer;
    while (size-- > 0)
        *p++ = 0;
}

static void connectivity_init(void)
{
    if (connectivity_init_attempted)
        return;

    connectivity_init_attempted = true;
    wifi_init_result = ap80_wifi_init();
    bluetooth_init_result = ap80_bluetooth_init();
}

static bool wifi_available(void)
{
    connectivity_init();
    if (wifi_init_result < 0)
    {
        splashf(HZ * 2, "Wi-Fi unavailable\nError %d",
                -wifi_init_result);
        return false;
    }
    return true;
}

static bool bluetooth_available(void)
{
    connectivity_init();
    if (bluetooth_init_result < 0)
    {
        splashf(HZ * 2, "Bluetooth unavailable\nError %d",
                -bluetooth_init_result);
        return false;
    }
    return true;
}

static void show_request_error(const char *operation, int error)
{
    splashf(HZ * 2, "%s\nError %d", operation,
            error < 0 ? -error : error);
}

static const char *wifi_phase_name(enum ap80_wifi_phase phase)
{
    switch (phase)
    {
        case AP80_WIFI_OFF:          return "Off";
        case AP80_WIFI_POWERING_ON:  return "Powering on";
        case AP80_WIFI_IDLE:         return "Ready";
        case AP80_WIFI_SCANNING:     return "Scanning";
        case AP80_WIFI_CONNECTING:   return "Connecting";
        case AP80_WIFI_CONNECTED:    return "Connected";
        case AP80_WIFI_POWERING_OFF: return "Powering off";
        case AP80_WIFI_ERROR:        return "Error";
    }
    return "Unknown";
}

static const char *bluetooth_operation_name(enum ap80_bluetooth_operation op)
{
    switch (op)
    {
        case AP80_BT_OP_NONE:         return "Ready";
        case AP80_BT_OP_POWER_ON:     return "Powering on";
        case AP80_BT_OP_POWER_OFF:    return "Powering off";
        case AP80_BT_OP_SCAN_START:   return "Starting scan";
        case AP80_BT_OP_SCAN_STOP:    return "Stopping scan";
        case AP80_BT_OP_PAIR_TRUST:   return "Pairing and trusting";
        case AP80_BT_OP_CONNECT:      return "Connecting";
        case AP80_BT_OP_DISCONNECT:   return "Disconnecting";
    }
    return "Working";
}

static int progress_action(void)
{
    int action = get_action(CONTEXT_STD, CONNECTIVITY_POLL_TICKS);
    if (action == ACTION_STD_CANCEL)
        return PROGRESS_CANCELLED;
    if (default_event_handler(action) == SYS_USB_CONNECTED)
        return PROGRESS_USB;
    return PROGRESS_DONE;
}

static int wait_for_wifi(unsigned int generation, const char *title)
{
    bool changed = false;

    while (true)
    {
        struct ap80_wifi_snapshot snapshot;
        ap80_wifi_get_snapshot(&snapshot);
        changed |= snapshot.generation != generation;

        if (changed && !snapshot.busy)
        {
            if (snapshot.last_error < 0)
                show_request_error(title, snapshot.last_error);
            return snapshot.last_error < 0 ?
                   snapshot.last_error : PROGRESS_DONE;
        }

        splashf(0, "%s\n%s\nBack: hide progress", title,
                wifi_phase_name(snapshot.phase));
        int result = progress_action();
        if (result != PROGRESS_DONE)
            return result;
    }
}

static int wait_for_bluetooth(uint32_t generation,
                              enum ap80_bluetooth_operation operation,
                              const char *title, bool cancellable)
{
    bool changed = false;

    while (true)
    {
        struct ap80_bluetooth_state state;
        ap80_bluetooth_get_state(&state);
        changed |= state.generation != generation;

        if (changed && !state.busy && state.last_operation == operation)
        {
            if (state.last_error < 0)
                show_request_error(title, state.last_error);
            return state.last_error < 0 ?
                   state.last_error : PROGRESS_DONE;
        }

        splashf(0, "%s\n%s%s", title,
                bluetooth_operation_name(state.operation),
                cancellable ? "\nBack: hide progress" : "");
        int result = progress_action();
        if (result == PROGRESS_USB)
            return result;
        if (cancellable && result == PROGRESS_CANCELLED)
            return result;
    }
}

static const char *string_list_name(int item, void *data,
                                    char *buffer, size_t buffer_size)
{
    struct string_list *list = data;
    (void)buffer;
    (void)buffer_size;
    if (item < 0 || item >= list->count)
        return "";
    return list->items[item];
}

static int choose_string(const char *title, const char * const *items,
                         int count, enum themable_icons icon)
{
    struct string_list data = {items, count};
    struct simplelist_info info;
    simplelist_info_init(&info, (char *)title, count, &data);
    info.get_name = string_list_name;
    info.title_icon = icon;
    info.speak_onshow = false;
    if (simplelist_show_list(&info))
        return CHOICE_USB;
    return info.selection;
}

static const char *status_list_name(int item, void *data,
                                    char *buffer, size_t buffer_size)
{
    struct status_list *list = data;
    (void)buffer;
    (void)buffer_size;
    if (item < 0 || item >= list->count)
        return "";
    return list->lines[item];
}

static int show_status_list(const char *title, struct status_list *data,
                            enum themable_icons icon)
{
    struct simplelist_info info;
    simplelist_info_init(&info, (char *)title, data->count, data);
    info.get_name = status_list_name;
    info.title_icon = icon;
    info.speak_onshow = false;
    return simplelist_show_list(&info) ? 1 : 0;
}

static enum confirmation_result confirm_action(const char *subject,
                                               const char *action)
{
    const char *lines[] = {subject, action};
    const char *yes_lines[] = {"Yes"};
    const char *no_lines[] = {"Cancel"};
    const struct text_message message = {lines, 2};
    const struct text_message yes_message = {yes_lines, 1};
    const struct text_message no_message = {no_lines, 1};
    enum yesno_res result =
        gui_syncyesno_run(&message, &yes_message, &no_message);
    if (result == YESNO_USB)
        return CONFIRM_USB;
    return result == YESNO_YES ? CONFIRM_ACCEPTED : CONFIRM_CANCELLED;
}

static const char *wifi_security_name(unsigned int security)
{
    if (security & AP80_WIFI_SECURITY_UNKNOWN)
        return "Saved profile";
    if (security & AP80_WIFI_SECURITY_EAP)
        return "Enterprise";
    if (security & AP80_WIFI_SECURITY_SAE)
        return (security & AP80_WIFI_SECURITY_PSK) ? "WPA2/WPA3" : "WPA3";
    if (security & AP80_WIFI_SECURITY_PSK)
        return "WPA/WPA2";
    if (security & AP80_WIFI_SECURITY_WEP)
        return "WEP";
    return "Open";
}

static char *wifi_power_name(int selected_item, void *data,
                             char *buffer, size_t buffer_size)
{
    struct ap80_wifi_snapshot snapshot;
    (void)selected_item;
    (void)data;
    connectivity_init();

    if (wifi_init_result < 0)
        snprintf(buffer, buffer_size, "Wi-Fi: unavailable");
    else
    {
        ap80_wifi_get_snapshot(&snapshot);
        snprintf(buffer, buffer_size, "Wi-Fi: %s",
                 wifi_phase_name(snapshot.phase));
    }
    return buffer;
}

static int toggle_wifi_power(void)
{
    struct ap80_wifi_snapshot before;
    if (!wifi_available())
        return 0;

    ap80_wifi_get_snapshot(&before);
    if (before.busy)
    {
        splash(HZ, "Wi-Fi is busy");
        return 0;
    }

    bool enable = !before.powered;
    int result = ap80_wifi_request_power(enable);
    if (result < 0)
        show_request_error(enable ? "Power on Wi-Fi" : "Power off Wi-Fi",
                           result);
    else
    {
        result = wait_for_wifi(
            before.generation,
            enable ? "Powering on Wi-Fi" : "Powering off Wi-Fi");
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static int request_wifi_disconnect(void)
{
    struct ap80_wifi_snapshot before;
    if (!wifi_available())
        return 0;

    ap80_wifi_get_snapshot(&before);
    if (!before.powered)
    {
        splash(HZ, "Wi-Fi is off");
        return 0;
    }
    if (before.phase != AP80_WIFI_CONNECTED)
    {
        splash(HZ, "Wi-Fi is not connected");
        return 0;
    }

    int result = ap80_wifi_request_disconnect();
    if (result < 0)
        show_request_error("Disconnect Wi-Fi", result);
    else
    {
        result = wait_for_wifi(before.generation, "Disconnecting Wi-Fi");
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static int connect_wifi_network(const struct ap80_wifi_network *network)
{
    struct ap80_wifi_snapshot before;
    char passphrase[AP80_WIFI_PASSPHRASE_MAX + 1] = "";
    const char *credential = NULL;

    if ((network->security & AP80_WIFI_SECURITY_EAP) ||
        (network->security & AP80_WIFI_SECURITY_WEP) ||
        ((network->security & AP80_WIFI_SECURITY_SAE) &&
         !(network->security & AP80_WIFI_SECURITY_PSK)))
    {
        splash(HZ * 2, "This Wi-Fi security is not supported");
        return 0;
    }

    if ((network->security &
         (AP80_WIFI_SECURITY_PSK | AP80_WIFI_SECURITY_SAE)) &&
        !network->saved)
    {
        if (kbd_input_ex(passphrase, sizeof(passphrase), NULL,
                         KBD_INPUT_SECURE) < 0)
        {
            secure_wipe(passphrase, sizeof(passphrase));
            return 0;
        }
        credential = passphrase;
    }

    ap80_wifi_get_snapshot(&before);
    int result = ap80_wifi_request_connect(network, credential);
    secure_wipe(passphrase, sizeof(passphrase));
    if (result < 0)
        show_request_error("Connect Wi-Fi", result);
    else
    {
        result = wait_for_wifi(before.generation, "Connecting Wi-Fi");
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static int forget_wifi_network(const struct ap80_wifi_network *network)
{
    struct ap80_wifi_snapshot before;
    const char *name = network->display_name[0] ?
                       network->display_name : "<hidden network>";
    enum confirmation_result confirmation =
        confirm_action(name, "Forget this network?");
    if (confirmation == CONFIRM_USB)
        return 1;
    if (confirmation != CONFIRM_ACCEPTED)
        return 0;

    ap80_wifi_get_snapshot(&before);
    int result = ap80_wifi_request_forget(network);
    if (result < 0)
        show_request_error("Forget Wi-Fi network", result);
    else
    {
        result = wait_for_wifi(before.generation,
                               "Forgetting Wi-Fi network");
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static int show_wifi_network_status(const struct ap80_wifi_network *network)
{
    struct status_list *data = &status_cache;
    const char *name = network->display_name[0] ?
                       network->display_name : "<hidden network>";

    data->count = 0;
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Name: %s", name);
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "BSSID: %s", network->bssid[0] ? network->bssid : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Security: %s", wifi_security_name(network->security));
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Signal: %d dBm", network->signal_dbm);
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Frequency: %d MHz", network->frequency_mhz);
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Saved: %s", network->saved ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Current: %s", network->current ? "yes" : "no");
    return show_status_list("Wi-Fi network", data, Icon_Radio_screen);
}

static int wifi_network_actions(const struct ap80_wifi_network *network)
{
    enum { WIFI_ACTION_CONNECT, WIFI_ACTION_FORGET, WIFI_ACTION_DETAILS };
    const char *items[3];
    int actions[3];
    int count = 0;
    const char *title = network->display_name[0] ?
                        network->display_name : "<hidden network>";

    items[count] = network->current ? "Disconnect" : "Connect";
    actions[count++] = WIFI_ACTION_CONNECT;
    if (network->saved)
    {
        items[count] = "Forget";
        actions[count++] = WIFI_ACTION_FORGET;
    }
    items[count] = "Details";
    actions[count++] = WIFI_ACTION_DETAILS;

    int selection = choose_string(title, items, count, Icon_Radio_screen);
    if (selection == CHOICE_USB)
        return 1;
    if (selection < 0 || selection >= count)
        return 0;

    switch (actions[selection])
    {
        case WIFI_ACTION_CONNECT:
            return network->current ? request_wifi_disconnect() :
                                      connect_wifi_network(network);
        case WIFI_ACTION_FORGET:
            return forget_wifi_network(network);
        case WIFI_ACTION_DETAILS:
            return show_wifi_network_status(network);
    }
    return 0;
}

static const char *wifi_network_name(int item, void *data,
                                     char *buffer, size_t buffer_size)
{
    struct wifi_network_cache *cache = data;
    if (item < 0 || (size_t)item >= cache->count)
        return "";

    const struct ap80_wifi_network *network = &cache->networks[item];
    const char *name = network->display_name[0] ?
                       network->display_name : "<hidden network>";
    snprintf(buffer, buffer_size, "%s%s  %d dBm  %s%s",
             network->current ? "* " : "", name, network->signal_dbm,
             wifi_security_name(network->security),
             network->saved ? "  saved" : "");
    return buffer;
}

static int scan_wifi_networks(void)
{
    struct ap80_wifi_snapshot before;
    if (!wifi_available())
        return -1;

    ap80_wifi_get_snapshot(&before);
    if (!before.powered)
    {
        splash(HZ, "Turn Wi-Fi on first");
        return -1;
    }
    if (before.busy)
    {
        splash(HZ, "Wi-Fi is busy");
        return -1;
    }

    int result = ap80_wifi_request_scan();
    if (result < 0)
    {
        show_request_error("Scan Wi-Fi", result);
        return -1;
    }
    result = wait_for_wifi(before.generation, "Scanning Wi-Fi");
    if (result == PROGRESS_USB)
        return PROGRESS_USB;
    if (result != PROGRESS_DONE)
        return -1; /* a finite one-shot scan may finish in the background */

    wifi_cache.count = ap80_wifi_get_networks(
        wifi_cache.networks, AP80_WIFI_MAX_NETWORKS);
    return 0;
}

static int show_wifi_networks(void)
{
    int result = scan_wifi_networks();
    if (result == PROGRESS_USB)
        return 1;
    if (result != PROGRESS_DONE)
        return 0;
    if (wifi_cache.count == 0)
    {
        splash(HZ, "No Wi-Fi networks found");
        return 0;
    }

    struct simplelist_info info;
    simplelist_info_init(&info, "Wi-Fi networks", wifi_cache.count,
                         &wifi_cache);
    info.get_name = wifi_network_name;
    info.title_icon = Icon_Radio_screen;
    info.speak_onshow = false;
    bool usb = simplelist_show_list(&info);
    if (usb)
        return 1;
    if (info.selection >= 0 &&
        (size_t)info.selection < wifi_cache.count)
    {
        return wifi_network_actions(&wifi_cache.networks[info.selection]);
    }
    return 0;
}

static int connect_hidden_wifi(void)
{
    static const char * const security_items[] =
    {
        "Open",
        "WPA/WPA2 Personal",
    };
    struct ap80_wifi_snapshot snapshot;
    struct ap80_wifi_network network;
    char ssid[AP80_WIFI_SSID_TEXT_SIZE] = "";

    if (!wifi_available())
        return 0;
    ap80_wifi_get_snapshot(&snapshot);
    if (!snapshot.powered)
    {
        splash(HZ, "Turn Wi-Fi on first");
        return 0;
    }

    if (kbd_input(ssid, sizeof(ssid), NULL) < 0)
        return 0;
    size_t ssid_length = strlen(ssid);
    if (ssid_length == 0 || ssid_length > AP80_WIFI_SSID_MAX_BYTES)
    {
        splashf(HZ * 2, "SSID must be 1-%d bytes",
                AP80_WIFI_SSID_MAX_BYTES);
        return 0;
    }

    int security = choose_string("Hidden Wi-Fi security", security_items,
                                 2, Icon_Radio_screen);
    if (security == CHOICE_USB)
        return 1;
    if (security < 0)
        return 0;

    memset(&network, 0, sizeof(network));
    memcpy(network.ssid, ssid, ssid_length);
    network.ssid_length = ssid_length;
    snprintf(network.display_name, sizeof(network.display_name), "%s", ssid);
    network.network_id = -1;
    if (security == 1)
        network.security = AP80_WIFI_SECURITY_PSK;
    return connect_wifi_network(&network);
}

static int forget_saved_wifi(void)
{
    struct wifi_network_cache *saved = &wifi_saved_cache;
    saved->count = 0;
    wifi_cache.count = ap80_wifi_get_networks(
        wifi_cache.networks, AP80_WIFI_MAX_NETWORKS);
    if (wifi_cache.count == 0)
    {
        int result = scan_wifi_networks();
        if (result == PROGRESS_USB)
            return 1;
        if (result != PROGRESS_DONE)
            return 0;
    }

    for (size_t i = 0; i < wifi_cache.count; ++i)
    {
        if (wifi_cache.networks[i].saved &&
            saved->count < AP80_WIFI_MAX_NETWORKS)
        {
            saved->networks[saved->count++] = wifi_cache.networks[i];
        }
    }
    if (saved->count == 0)
    {
        splash(HZ, "No saved Wi-Fi networks");
        return 0;
    }

    struct simplelist_info info;
    simplelist_info_init(&info, "Saved Wi-Fi networks", saved->count, saved);
    info.get_name = wifi_network_name;
    info.title_icon = Icon_Radio_screen;
    info.speak_onshow = false;
    if (simplelist_show_list(&info))
        return 1;
    if (info.selection >= 0 && (size_t)info.selection < saved->count)
        return forget_wifi_network(&saved->networks[info.selection]);
    return 0;
}

static int show_wifi_status(void)
{
    struct ap80_wifi_snapshot snapshot;
    struct status_list *data = &status_cache;
    if (!wifi_available())
        return 0;
    ap80_wifi_get_snapshot(&snapshot);

    data->count = 0;
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "State: %s", wifi_phase_name(snapshot.phase));
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Powered: %s", snapshot.powered ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "SSID: %s", snapshot.ssid[0] ? snapshot.ssid : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "BSSID: %s", snapshot.bssid[0] ? snapshot.bssid : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "IPv4: %s", snapshot.ipv4[0] ? snapshot.ipv4 : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Signal: %d dBm", snapshot.rssi_dbm);
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Network ID: %d", snapshot.network_id);
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Last error: %d", snapshot.last_error);
    return show_status_list("Wi-Fi status", data, Icon_Radio_screen);
}

static char *bluetooth_power_name(int selected_item, void *data,
                                  char *buffer, size_t buffer_size)
{
    struct ap80_bluetooth_state state;
    (void)selected_item;
    (void)data;
    connectivity_init();

    if (bluetooth_init_result < 0)
        snprintf(buffer, buffer_size, "Bluetooth: unavailable");
    else
    {
        ap80_bluetooth_get_state(&state);
        if (state.busy)
            snprintf(buffer, buffer_size, "Bluetooth: %s",
                     bluetooth_operation_name(state.operation));
        else if (state.connected)
            snprintf(buffer, buffer_size, "Bluetooth: connected%s%s",
                     state.codec[0] ? " / " : "",
                     state.codec[0] ? state.codec : "");
        else
            snprintf(buffer, buffer_size, "Bluetooth: %s",
                     state.powered ? "On" : "Off");
    }
    return buffer;
}

static int toggle_bluetooth_power(void)
{
    struct ap80_bluetooth_state before;
    if (!bluetooth_available())
        return 0;
    ap80_bluetooth_get_state(&before);
    if (before.busy)
    {
        splash(HZ, "Bluetooth is busy");
        return 0;
    }

    bool enable = !before.powered;
    enum ap80_bluetooth_operation operation =
        enable ? AP80_BT_OP_POWER_ON : AP80_BT_OP_POWER_OFF;
    int result = ap80_bluetooth_set_power(enable);
    if (result < 0)
        show_request_error(enable ? "Power on Bluetooth" :
                                    "Power off Bluetooth", result);
    else
    {
        result = wait_for_bluetooth(before.generation, operation,
                                    enable ? "Powering on Bluetooth" :
                                             "Powering off Bluetooth", true);
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static const char *bluetooth_device_name(int item, void *data,
                                         char *buffer, size_t buffer_size)
{
    struct bluetooth_device_cache *cache = data;
    if (item < 0 || (size_t)item >= cache->count)
        return "";

    const struct ap80_bluetooth_device *device = &cache->devices[item];
    snprintf(buffer, buffer_size, "%s%s%s%s",
             device->connected ? "* " : "",
             device->name[0] ? device->name : device->address,
             device->paired ? "  paired" : "",
             device->trusted ? "  trusted" : "");
    return buffer;
}

static int bluetooth_list_action(int action, struct gui_synclist *lists)
{
    struct bluetooth_device_cache *cache = lists->data;
    struct ap80_bluetooth_state state;

    if (action != ACTION_NONE && action != ACTION_REDRAW)
        return action;

    ap80_bluetooth_get_state(&state);
    if (action == ACTION_REDRAW || state.generation != cache->generation)
    {
        cache->generation = state.generation;
        cache->count = ap80_bluetooth_get_devices(
            cache->devices, AP80_BLUETOOTH_MAX_DEVICES);
        gui_synclist_set_nb_items(lists, cache->count);
        return ACTION_REDRAW;
    }
    return action;
}

static int stop_bluetooth_scan(void)
{
    struct ap80_bluetooth_state state;
    ap80_bluetooth_get_state(&state);
    if (!state.scanning && state.operation != AP80_BT_OP_SCAN_START)
        return 0;

    uint32_t generation = state.generation;
    /* The controller queues this behind an in-flight SCAN_START, guaranteeing
     * eventual scan-off without waiting for vendor discovery work here. */
    int result = ap80_bluetooth_set_scan(false);
    if (result < 0)
        show_request_error("Stop Bluetooth scan", result);
    else
    {
        result = wait_for_bluetooth(generation, AP80_BT_OP_SCAN_STOP,
                                    "Stopping Bluetooth scan", true);
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static int use_bluetooth_device(struct ap80_bluetooth_device device)
{
    struct ap80_bluetooth_state before;
    ap80_bluetooth_get_state(&before);

    if (device.connected)
    {
        int result = ap80_bluetooth_disconnect(device.address);
        if (result < 0)
            show_request_error("Disconnect Bluetooth", result);
        else
        {
            result = wait_for_bluetooth(before.generation,
                                        AP80_BT_OP_DISCONNECT,
                                        "Disconnecting Bluetooth", true);
            if (result == PROGRESS_USB)
                return 1;
        }
        return 0;
    }

    if (!device.paired || !device.trusted)
    {
        int result = ap80_bluetooth_pair_trust(device.address);
        if (result < 0)
        {
            show_request_error("Pair Bluetooth device", result);
            return 0;
        }
        result = wait_for_bluetooth(before.generation,
                                    AP80_BT_OP_PAIR_TRUST,
                                    "Pairing Bluetooth device", true);
        if (result == PROGRESS_USB)
            return 1;
        if (result != PROGRESS_DONE)
            return 0;
        ap80_bluetooth_get_state(&before);
    }

    int result = ap80_bluetooth_connect(device.address);
    if (result < 0)
        show_request_error("Connect Bluetooth", result);
    else
    {
        result = wait_for_bluetooth(before.generation, AP80_BT_OP_CONNECT,
                                    "Connecting Bluetooth", true);
        if (result == PROGRESS_USB)
            return 1;
    }
    return 0;
}

static int show_bluetooth_devices(void)
{
    struct ap80_bluetooth_state before;
    if (!bluetooth_available())
        return 0;
    ap80_bluetooth_get_state(&before);
    if (!before.powered)
    {
        splash(HZ, "Turn Bluetooth on first");
        return 0;
    }
    if (before.busy)
    {
        splash(HZ, "Bluetooth is busy");
        return 0;
    }

    int result = ap80_bluetooth_set_scan(true);
    if (result < 0)
    {
        show_request_error("Start Bluetooth scan", result);
        return 0;
    }
    result = wait_for_bluetooth(before.generation, AP80_BT_OP_SCAN_START,
                                "Starting Bluetooth scan", true);
    if (result != PROGRESS_DONE)
    {
        int stop_result = stop_bluetooth_scan();
        return result == PROGRESS_USB || stop_result != 0 ? 1 : 0;
    }

    ap80_bluetooth_get_state(&before);
    bluetooth_cache.generation = before.generation;
    bluetooth_cache.count = ap80_bluetooth_get_devices(
        bluetooth_cache.devices, AP80_BLUETOOTH_MAX_DEVICES);

    struct simplelist_info info;
    simplelist_info_init(&info, "Bluetooth devices",
                         bluetooth_cache.count, &bluetooth_cache);
    info.get_name = bluetooth_device_name;
    info.action_callback = bluetooth_list_action;
    info.timeout = CONNECTIVITY_POLL_TICKS;
    info.title_icon = Icon_Radio_screen;
    info.speak_onshow = false;
    bool usb = simplelist_show_list(&info);
    int selection = info.selection;
    struct ap80_bluetooth_device selected;
    bool have_selection = !usb && selection >= 0 &&
                          (size_t)selection < bluetooth_cache.count;
    if (have_selection)
        selected = bluetooth_cache.devices[selection];

    int stop_result = stop_bluetooth_scan();
    if (usb || stop_result != 0)
        return 1;
    if (have_selection)
        return use_bluetooth_device(selected);
    return 0;
}

static int show_bluetooth_status(void)
{
    struct ap80_bluetooth_state state;
    struct status_list *data = &status_cache;
    if (!bluetooth_available())
        return 0;
    ap80_bluetooth_get_state(&state);

    data->count = 0;
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Powered: %s", state.powered ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Scanning: %s", state.scanning ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Connected: %s", state.connected ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Device: %s", state.active_name[0] ? state.active_name : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Address: %s",
             state.active_address[0] ? state.active_address : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Audio ready: %s", state.audio_ready ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Route active: %s", state.route_active ? "yes" : "no");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Active codec: %s", state.codec[0] ? state.codec : "n/a");
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Operation: %s", bluetooth_operation_name(state.operation));
    snprintf(data->lines[data->count++], CONNECTIVITY_STATUS_WIDTH,
             "Last error: %d", state.last_error);
    return show_status_list("Bluetooth status", data, Icon_Radio_screen);
}

static int connectivity_menu_callback(int action,
                                      const struct menu_item_ex *item,
                                      struct gui_synclist *list)
{
    (void)item;
    (void)list;
    if (action == ACTION_ENTER_MENUITEM)
        connectivity_init();
    else if (action == ACTION_EXIT_MENUITEM && bluetooth_init_result >= 0)
    {
        struct ap80_bluetooth_state state;
        ap80_bluetooth_get_state(&state);
        if (state.scanning || state.operation == AP80_BT_OP_SCAN_START)
            ap80_bluetooth_set_scan(false);
    }
    return action;
}

MENUITEM_FUNCTION_DYNTEXT(wifi_power_item, MENU_FUNC_CHECK_RETVAL,
                          toggle_wifi_power,
                          wifi_power_name, NULL, NULL, NULL,
                          Icon_Radio_screen);
MENUITEM_FUNCTION(wifi_networks_item, MENU_FUNC_CHECK_RETVAL,
                  "Wi-Fi networks", show_wifi_networks, NULL,
                  Icon_Radio_screen);
MENUITEM_FUNCTION(hidden_wifi_item, MENU_FUNC_CHECK_RETVAL,
                  "Hidden Wi-Fi network",
                  connect_hidden_wifi, NULL, Icon_Keyboard);
MENUITEM_FUNCTION(wifi_disconnect_item, MENU_FUNC_CHECK_RETVAL,
                  "Disconnect Wi-Fi",
                  request_wifi_disconnect, NULL, Icon_NOICON);
MENUITEM_FUNCTION(wifi_forget_item, MENU_FUNC_CHECK_RETVAL,
                  "Forget saved Wi-Fi",
                  forget_saved_wifi, NULL, Icon_NOICON);
MENUITEM_FUNCTION(wifi_status_item, MENU_FUNC_CHECK_RETVAL, "Wi-Fi status",
                  show_wifi_status, NULL, Icon_Questionmark);

MENUITEM_FUNCTION_DYNTEXT(bluetooth_power_item, MENU_FUNC_CHECK_RETVAL,
                          toggle_bluetooth_power,
                          bluetooth_power_name, NULL, NULL, NULL,
                          Icon_Radio_screen);
MENUITEM_FUNCTION(bluetooth_devices_item, MENU_FUNC_CHECK_RETVAL,
                  "Bluetooth devices", show_bluetooth_devices, NULL,
                  Icon_Radio_screen);
MENUITEM_FUNCTION(bluetooth_status_item, MENU_FUNC_CHECK_RETVAL,
                  "Bluetooth status", show_bluetooth_status, NULL,
                  Icon_Questionmark);

MAKE_MENU(connectivity_menu, "Connectivity", connectivity_menu_callback,
          Icon_Radio_screen,
          &wifi_power_item, &wifi_networks_item, &hidden_wifi_item,
          &wifi_disconnect_item, &wifi_forget_item, &wifi_status_item,
          &bluetooth_power_item, &bluetooth_devices_item,
          &bluetooth_status_item);
