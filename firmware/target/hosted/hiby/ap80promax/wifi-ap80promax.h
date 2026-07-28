/***************************************************************************
 * AP80 Pro Max asynchronous Wi-Fi controller.
 *
 * The UI-facing calls only queue work. A single worker owns radio service
 * transitions and the wpa_supplicant control socket; callers consume copied
 * snapshots identified by a monotonically increasing generation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#ifndef WIFI_AP80PROMAX_H
#define WIFI_AP80PROMAX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AP80_WIFI_SSID_MAX_BYTES 32
#define AP80_WIFI_SSID_TEXT_SIZE (AP80_WIFI_SSID_MAX_BYTES * 4 + 1)
#define AP80_WIFI_BSSID_SIZE 18
#define AP80_WIFI_IPV4_SIZE 16
#define AP80_WIFI_MAX_NETWORKS 24
#define AP80_WIFI_PASSPHRASE_MAX 64

enum ap80_wifi_phase
{
    AP80_WIFI_OFF = 0,
    AP80_WIFI_POWERING_ON,
    AP80_WIFI_IDLE,
    AP80_WIFI_SCANNING,
    AP80_WIFI_CONNECTING,
    AP80_WIFI_CONNECTED,
    AP80_WIFI_POWERING_OFF,
    AP80_WIFI_ERROR,
};

enum ap80_wifi_security
{
    AP80_WIFI_SECURITY_OPEN = 0,
    AP80_WIFI_SECURITY_WEP = 1 << 0,
    AP80_WIFI_SECURITY_PSK = 1 << 1,
    AP80_WIFI_SECURITY_SAE = 1 << 2,
    AP80_WIFI_SECURITY_EAP = 1 << 3,
    /* Saved profile was not visible in scan results; reuse its settings. */
    AP80_WIFI_SECURITY_UNKNOWN = 1 << 4,
};

struct ap80_wifi_network
{
    uint8_t ssid[AP80_WIFI_SSID_MAX_BYTES];
    uint8_t ssid_length;
    char display_name[AP80_WIFI_SSID_TEXT_SIZE];
    char bssid[AP80_WIFI_BSSID_SIZE];
    int frequency_mhz;
    int signal_dbm;
    unsigned int security;
    int network_id;
    bool saved;
    bool current;
};

struct ap80_wifi_snapshot
{
    enum ap80_wifi_phase phase;
    unsigned int generation;
    int last_error;
    bool busy;
    bool powered;
    char ssid[AP80_WIFI_SSID_TEXT_SIZE];
    char bssid[AP80_WIFI_BSSID_SIZE];
    char ipv4[AP80_WIFI_IPV4_SIZE];
    int rssi_dbm;
    int network_id;
    size_t network_count;
};

int ap80_wifi_init(void);
void ap80_wifi_shutdown(void);

int ap80_wifi_request_power(bool enabled);
int ap80_wifi_request_scan(void);
int ap80_wifi_request_connect(const struct ap80_wifi_network *network,
                              const char *passphrase);
int ap80_wifi_request_disconnect(void);
int ap80_wifi_request_forget(const struct ap80_wifi_network *network);

void ap80_wifi_get_snapshot(struct ap80_wifi_snapshot *snapshot);
size_t ap80_wifi_get_networks(struct ap80_wifi_network *networks,
                              size_t capacity);

/* Pure parser/encoding seams used by host tests and protocol consumers. */
int ap80_wifi_parse_scan_results(const char *text,
                                 struct ap80_wifi_network *networks,
                                 size_t capacity, size_t *network_count);
int ap80_wifi_apply_saved_networks(const char *text,
                                   struct ap80_wifi_network *networks,
                                   size_t capacity, size_t *network_count);
void ap80_wifi_parse_status(const char *status_text, const char *signal_text,
                            struct ap80_wifi_snapshot *snapshot);
int ap80_wifi_encode_ssid(const uint8_t *ssid, size_t ssid_length,
                          char *encoded, size_t encoded_size);
int ap80_wifi_encode_psk(const char *passphrase, char *encoded,
                         size_t encoded_size);

#ifdef AP80_WIFI_TEST
struct ap80_wifi_test_backend
{
    bool (*power_is_on)(void);
    int (*set_power)(bool enabled);
    int (*wpa_command)(const char *command, char *reply, size_t reply_size);
    int (*secure_config)(void);
    void (*sleep_ms)(unsigned int milliseconds);
};

/* Must be called before ap80_wifi_init(). */
int ap80_wifi_set_test_backend(
    const struct ap80_wifi_test_backend *backend);
bool ap80_wifi_test_credentials_cleared(void);
#endif

#endif
