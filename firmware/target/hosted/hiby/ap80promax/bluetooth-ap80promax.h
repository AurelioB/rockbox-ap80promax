/***************************************************************************
 * AP80 Pro Max Bluetooth controller.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#ifndef BLUETOOTH_AP80PROMAX_H
#define BLUETOOTH_AP80PROMAX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AP80_BLUETOOTH_ADDRESS_SIZE 18
#define AP80_BLUETOOTH_NAME_SIZE 64
#define AP80_BLUETOOTH_CODEC_SIZE 16
#define AP80_BLUETOOTH_MAX_DEVICES 16

enum ap80_bluetooth_operation
{
    AP80_BT_OP_NONE = 0,
    AP80_BT_OP_POWER_ON,
    AP80_BT_OP_POWER_OFF,
    AP80_BT_OP_SCAN_START,
    AP80_BT_OP_SCAN_STOP,
    AP80_BT_OP_PAIR_TRUST,
    AP80_BT_OP_CONNECT,
    AP80_BT_OP_DISCONNECT,
};

enum ap80_bluetooth_event_type
{
    AP80_BT_EVENT_STATE_CHANGED = 1,
    AP80_BT_EVENT_DEVICE_LIST,
    AP80_BT_EVENT_OPERATION_COMPLETE,
    AP80_BT_EVENT_ROUTE_FALLBACK,
};

struct ap80_bluetooth_device
{
    char address[AP80_BLUETOOTH_ADDRESS_SIZE];
    char name[AP80_BLUETOOTH_NAME_SIZE];
    bool paired;
    bool trusted;
    bool connected;
};

struct ap80_bluetooth_state
{
    bool initialized;
    bool powered;
    bool scanning;
    bool busy;
    bool connected;
    bool audio_ready;
    bool route_active;
    enum ap80_bluetooth_operation operation;
    enum ap80_bluetooth_operation last_operation;
    int last_error;
    uint32_t generation;
    size_t device_count;
    char active_address[AP80_BLUETOOTH_ADDRESS_SIZE];
    char active_name[AP80_BLUETOOTH_NAME_SIZE];
    char codec[AP80_BLUETOOTH_CODEC_SIZE];
};

struct ap80_bluetooth_event
{
    enum ap80_bluetooth_event_type type;
    enum ap80_bluetooth_operation operation;
    int error;
    uint32_t generation;
};

/* Initialize/shut down the single asynchronous controller worker. */
int ap80_bluetooth_init(void);
void ap80_bluetooth_shutdown(void);

/*
 * Queue a non-blocking operation. Completion is reported in state.last_error
 * and through AP80_BT_EVENT_OPERATION_COMPLETE.
 */
int ap80_bluetooth_set_power(bool enabled);
int ap80_bluetooth_set_scan(bool enabled);
int ap80_bluetooth_pair_trust(const char *address);
int ap80_bluetooth_connect(const char *address);
int ap80_bluetooth_disconnect(const char *address);

/* Thread-safe state snapshots and event polling for the UI. */
void ap80_bluetooth_get_state(struct ap80_bluetooth_state *state);
size_t ap80_bluetooth_get_devices(struct ap80_bluetooth_device *devices,
                                  size_t capacity);
bool ap80_bluetooth_next_event(struct ap80_bluetooth_event *event);

/*
 * Called from audiohw_set_volume(). The latest values are always remembered;
 * true means the active Bluetooth route consumed the update.
 */
bool ap80_bluetooth_queue_volume(int vol_l, int vol_r);

#endif
