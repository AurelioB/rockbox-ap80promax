/***************************************************************************
 * AP80 Pro Max asynchronous Wi-Fi controller.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#include "wifi-ap80promax.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef SIMULATOR
#include "services-ap80promax.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#define WPA_CONTROL_PATH "/var/run/wpa_supplicant/wlan0"
#define WPA_DATA_CONFIG "/usr/data/wpa_supplicant.conf"
#define WPA_REPLY_SIZE 16384
#define WPA_COMMAND_SIZE 256
#define WIFI_REFRESH_SECONDS 5
#define WIFI_SCAN_POLLS 50
#define WIFI_ASSOCIATION_POLLS 150
#define WIFI_IP_POLLS 50
#define WIFI_POLL_INTERVAL_MS 100

struct wifi_backend
{
    bool (*power_is_on)(void);
    int (*set_power)(bool enabled);
    int (*wpa_command)(const char *command, char *reply, size_t reply_size);
    int (*secure_config)(void);
    void (*sleep_ms)(unsigned int milliseconds);
};

enum wifi_operation
{
    WIFI_OPERATION_NONE = 0,
    WIFI_OPERATION_POWER,
    WIFI_OPERATION_SCAN,
    WIFI_OPERATION_CONNECT,
    WIFI_OPERATION_DISCONNECT,
    WIFI_OPERATION_FORGET,
};

struct wifi_command
{
    enum wifi_operation operation;
    bool enabled;
    struct ap80_wifi_network network;
    char passphrase[AP80_WIFI_PASSPHRASE_MAX + 1];
};

struct wifi_controller
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    bool initialized;
    bool stop;
    bool pending;
    struct wifi_command command;
    struct ap80_wifi_snapshot snapshot;
    struct ap80_wifi_network networks[AP80_WIFI_MAX_NETWORKS];
    size_t network_count;
};

static struct wifi_controller controller = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
};
static const struct wifi_backend *backend;

#ifdef AP80_WIFI_TEST
static struct wifi_backend configured_test_backend;
static bool test_backend_configured;
static bool test_credentials_cleared = true;
#endif

static void secure_zero(void *memory, size_t size)
{
    volatile unsigned char *bytes = memory;
    while (size-- != 0)
        *bytes++ = 0;
}

static void copy_string(char *destination, size_t destination_size,
                        const char *source)
{
    if (destination_size == 0)
        return;
    if (!source)
        source = "";

    size_t length = strlen(source);
    if (length >= destination_size)
        length = destination_size - 1;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static int hex_value(unsigned char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    character = (unsigned char)tolower(character);
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    return -1;
}

static int decode_ssid(const char *encoded, uint8_t *ssid,
                       size_t *ssid_length)
{
    if (!encoded || !ssid || !ssid_length)
        return -EINVAL;

    size_t length = 0;
    for (size_t index = 0; encoded[index] != '\0'; index++)
    {
        unsigned char character = (unsigned char)encoded[index];
        if (character == '\\' && encoded[index + 1] != '\0')
        {
            character = (unsigned char)encoded[++index];
            if (character == 'x' && encoded[index + 1] != '\0' &&
                encoded[index + 2] != '\0')
            {
                int high = hex_value((unsigned char)encoded[index + 1]);
                int low = hex_value((unsigned char)encoded[index + 2]);
                if (high >= 0 && low >= 0)
                {
                    character = (unsigned char)((high << 4) | low);
                    index += 2;
                }
            }
            else if (character == 'n')
                character = '\n';
            else if (character == 'r')
                character = '\r';
            else if (character == 't')
                character = '\t';
            else if (character == 'e')
                character = 0x1b;
        }

        if (length >= AP80_WIFI_SSID_MAX_BYTES)
            return -ENAMETOOLONG;
        ssid[length++] = character;
    }

    *ssid_length = length;
    return 0;
}

static void format_ssid(const uint8_t *ssid, size_t ssid_length,
                        char *display, size_t display_size)
{
    static const char digits[] = "0123456789abcdef";
    size_t used = 0;

    if (display_size == 0)
        return;
    if (ssid_length == 0)
    {
        copy_string(display, display_size, "<hidden>");
        return;
    }

    for (size_t index = 0; index < ssid_length && used + 1 < display_size;
         index++)
    {
        unsigned char character = ssid[index];
        if (character >= 0x20 && character <= 0x7e && character != '\\')
            display[used++] = (char)character;
        else if (character == '\\' && used + 2 < display_size)
        {
            display[used++] = '\\';
            display[used++] = '\\';
        }
        else if (used + 4 < display_size)
        {
            display[used++] = '\\';
            display[used++] = 'x';
            display[used++] = digits[character >> 4];
            display[used++] = digits[character & 0xf];
        }
    }
    display[used] = '\0';
}

int ap80_wifi_encode_ssid(const uint8_t *ssid, size_t ssid_length,
                          char *encoded, size_t encoded_size)
{
    static const char digits[] = "0123456789abcdef";
    if ((!ssid && ssid_length != 0) || !encoded ||
        ssid_length > AP80_WIFI_SSID_MAX_BYTES ||
        encoded_size < ssid_length * 2 + 1)
        return -EINVAL;

    for (size_t index = 0; index < ssid_length; index++)
    {
        encoded[index * 2] = digits[ssid[index] >> 4];
        encoded[index * 2 + 1] = digits[ssid[index] & 0xf];
    }
    encoded[ssid_length * 2] = '\0';
    return 0;
}

int ap80_wifi_encode_psk(const char *passphrase, char *encoded,
                         size_t encoded_size)
{
    if (!passphrase || !encoded)
        return -EINVAL;

    size_t length = strnlen(passphrase, AP80_WIFI_PASSPHRASE_MAX + 1);
    if (length < 8 || length > AP80_WIFI_PASSPHRASE_MAX)
        return -EINVAL;

    if (length == AP80_WIFI_PASSPHRASE_MAX)
    {
        for (size_t index = 0; index < length; index++)
        {
            if (!isxdigit((unsigned char)passphrase[index]))
                return -EINVAL;
        }
        if (encoded_size < length + 1)
            return -ENOSPC;
        memcpy(encoded, passphrase, length + 1);
        return 0;
    }

    size_t required = 3;
    for (size_t index = 0; index < length; index++)
    {
        unsigned char character = (unsigned char)passphrase[index];
        if (character < 0x20 || character > 0x7e)
            return -EINVAL;
        required += character == '"' || character == '\\';
    }
    required += length;
    if (encoded_size < required)
        return -ENOSPC;

    size_t used = 0;
    encoded[used++] = '"';
    for (size_t index = 0; index < length; index++)
    {
        char character = passphrase[index];
        if (character == '"' || character == '\\')
            encoded[used++] = '\\';
        encoded[used++] = character;
    }
    encoded[used++] = '"';
    encoded[used] = '\0';
    return 0;
}

static unsigned int parse_security(const char *flags)
{
    unsigned int security = AP80_WIFI_SECURITY_OPEN;
    if (strstr(flags, "WEP"))
        security |= AP80_WIFI_SECURITY_WEP;
    if (strstr(flags, "PSK"))
        security |= AP80_WIFI_SECURITY_PSK;
    if (strstr(flags, "SAE"))
        security |= AP80_WIFI_SECURITY_SAE;
    if (strstr(flags, "EAP") || strstr(flags, "IEEE8021X"))
        security |= AP80_WIFI_SECURITY_EAP;
    return security;
}

static bool same_ssid(const struct ap80_wifi_network *left,
                      const struct ap80_wifi_network *right)
{
    return left->ssid_length == right->ssid_length &&
           memcmp(left->ssid, right->ssid, left->ssid_length) == 0;
}

static int signal_comparison(const void *left_pointer,
                             const void *right_pointer)
{
    const struct ap80_wifi_network *left = left_pointer;
    const struct ap80_wifi_network *right = right_pointer;
    if (left->current != right->current)
        return left->current ? -1 : 1;
    if (left->saved != right->saved)
        return left->saved ? -1 : 1;
    if (left->signal_dbm == right->signal_dbm)
        return strcmp(left->display_name, right->display_name);
    return left->signal_dbm > right->signal_dbm ? -1 : 1;
}

static size_t split_tab_fields(char *line, char **fields, size_t capacity)
{
    if (!line || !fields || capacity == 0)
        return 0;

    size_t count = 0;
    fields[count++] = line;
    while (count < capacity)
    {
        char *separator = strchr(fields[count - 1], '\t');
        if (!separator)
            break;
        *separator = '\0';
        fields[count++] = separator + 1;
    }
    return count;
}

int ap80_wifi_parse_scan_results(const char *text,
                                 struct ap80_wifi_network *networks,
                                 size_t capacity, size_t *network_count)
{
    if (!text || (!networks && capacity != 0) || !network_count)
        return -EINVAL;

    *network_count = 0;
    char *copy = strdup(text);
    if (!copy)
        return -ENOMEM;

    int result = 0;
    char *save_line;
    for (char *line = strtok_r(copy, "\r\n", &save_line);
         line != NULL; line = strtok_r(NULL, "\r\n", &save_line))
    {
        if (strncmp(line, "bssid", 6) == 0)
            continue;

        char *fields[5];
        size_t field_count = split_tab_fields(line, fields, 5);
        if (field_count != 5)
            continue;

        struct ap80_wifi_network network;
        memset(&network, 0, sizeof(network));
        network.network_id = -1;
        copy_string(network.bssid, sizeof(network.bssid), fields[0]);
        network.frequency_mhz = (int)strtol(fields[1], NULL, 10);
        network.signal_dbm = (int)strtol(fields[2], NULL, 10);
        network.security = parse_security(fields[3]);

        size_t ssid_length;
        result = decode_ssid(fields[4], network.ssid, &ssid_length);
        if (result == -ENAMETOOLONG)
        {
            result = 0;
            continue;
        }
        if (result != 0)
            break;
        if (ssid_length == 0)
            continue;
        network.ssid_length = (uint8_t)ssid_length;
        format_ssid(network.ssid, ssid_length, network.display_name,
                    sizeof(network.display_name));

        bool duplicate = false;
        for (size_t index = 0; index < *network_count; index++)
        {
            if (same_ssid(&networks[index], &network) &&
                networks[index].security == network.security)
            {
                if (network.signal_dbm > networks[index].signal_dbm)
                    networks[index] = network;
                duplicate = true;
                break;
            }
        }
        if (!duplicate && *network_count < capacity)
            networks[(*network_count)++] = network;
    }

    if (result == 0 && *network_count > 1)
        qsort(networks, *network_count, sizeof(*networks),
              signal_comparison);
    free(copy);
    return result;
}

int ap80_wifi_apply_saved_networks(const char *text,
                                   struct ap80_wifi_network *networks,
                                   size_t capacity, size_t *network_count)
{
    if (!text || !network_count ||
        (!networks && capacity != 0) || *network_count > capacity)
        return -EINVAL;

    char *copy = strdup(text);
    if (!copy)
        return -ENOMEM;

    char *save_line;
    for (char *line = strtok_r(copy, "\r\n", &save_line);
         line != NULL; line = strtok_r(NULL, "\r\n", &save_line))
    {
        if (strncmp(line, "network id", 10) == 0)
            continue;

        char *fields[4];
        size_t field_count = split_tab_fields(line, fields, 4);
        if (field_count < 2)
            continue;

        uint8_t ssid[AP80_WIFI_SSID_MAX_BYTES];
        size_t ssid_length;
        if (decode_ssid(fields[1], ssid, &ssid_length) != 0 ||
            ssid_length == 0)
            continue;

        int network_id = (int)strtol(fields[0], NULL, 10);
        bool current = field_count >= 4 &&
                       strstr(fields[3], "CURRENT") != NULL;
        bool matched = false;
        for (size_t index = 0; index < *network_count; index++)
        {
            if (networks[index].ssid_length == ssid_length &&
                memcmp(networks[index].ssid, ssid, ssid_length) == 0)
            {
                networks[index].saved = true;
                networks[index].network_id = network_id;
                networks[index].current = current;
                matched = true;
                break;
            }
        }

        if (!matched && *network_count < capacity)
        {
            struct ap80_wifi_network *network =
                &networks[(*network_count)++];
            memset(network, 0, sizeof(*network));
            memcpy(network->ssid, ssid, ssid_length);
            network->ssid_length = (uint8_t)ssid_length;
            format_ssid(ssid, ssid_length, network->display_name,
                        sizeof(network->display_name));
            if (field_count >= 3 && strcmp(fields[2], "any") != 0)
                copy_string(network->bssid, sizeof(network->bssid),
                            fields[2]);
            network->signal_dbm = -127;
            network->security = AP80_WIFI_SECURITY_UNKNOWN;
            network->network_id = network_id;
            network->saved = true;
            network->current = current;
        }
    }

    if (*network_count > 1)
        qsort(networks, *network_count, sizeof(*networks),
              signal_comparison);
    free(copy);
    return 0;
}

static void parse_status_field(char *line,
                               struct ap80_wifi_snapshot *snapshot)
{
    char *separator = strchr(line, '=');
    if (!separator)
        return;
    *separator++ = '\0';

    if (strcmp(line, "wpa_state") == 0)
    {
        if (strcmp(separator, "COMPLETED") == 0)
        {
            snapshot->phase = AP80_WIFI_CONNECTED;
            snapshot->powered = true;
        }
        else if (snapshot->powered)
            snapshot->phase = AP80_WIFI_IDLE;
    }
    else if (strcmp(line, "ssid") == 0)
    {
        uint8_t ssid[AP80_WIFI_SSID_MAX_BYTES];
        size_t ssid_length;
        if (decode_ssid(separator, ssid, &ssid_length) == 0)
            format_ssid(ssid, ssid_length, snapshot->ssid,
                        sizeof(snapshot->ssid));
    }
    else if (strcmp(line, "bssid") == 0)
        copy_string(snapshot->bssid, sizeof(snapshot->bssid), separator);
    else if (strcmp(line, "ip_address") == 0)
        copy_string(snapshot->ipv4, sizeof(snapshot->ipv4), separator);
    else if (strcmp(line, "id") == 0)
        snapshot->network_id = (int)strtol(separator, NULL, 10);
}

void ap80_wifi_parse_status(const char *status_text, const char *signal_text,
                            struct ap80_wifi_snapshot *snapshot)
{
    if (!snapshot)
        return;

    snapshot->ssid[0] = '\0';
    snapshot->bssid[0] = '\0';
    snapshot->ipv4[0] = '\0';
    snapshot->rssi_dbm = -127;
    snapshot->network_id = -1;

    if (status_text)
    {
        char *copy = strdup(status_text);
        if (copy)
        {
            char *save;
            for (char *line = strtok_r(copy, "\r\n", &save);
                 line != NULL; line = strtok_r(NULL, "\r\n", &save))
                parse_status_field(line, snapshot);
            free(copy);
        }
    }

    if (signal_text)
    {
        char *copy = strdup(signal_text);
        if (copy)
        {
            char *save;
            for (char *line = strtok_r(copy, "\r\n", &save);
                 line != NULL; line = strtok_r(NULL, "\r\n", &save))
            {
                if (strncmp(line, "RSSI=", 5) == 0)
                    snapshot->rssi_dbm = (int)strtol(line + 5, NULL, 10);
            }
            free(copy);
        }
    }
}

static void default_sleep_ms(unsigned int milliseconds)
{
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
        ;
}

#ifndef SIMULATOR
static unsigned int control_socket_serial;

static int real_wpa_command(const char *command, char *reply,
                            size_t reply_size)
{
    if (!command || !reply || reply_size < 2)
        return -EINVAL;

    int socket_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (socket_fd < 0)
        return -errno;
    (void)fcntl(socket_fd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un local;
    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    int length = snprintf(local.sun_path, sizeof(local.sun_path),
                          "/tmp/rockbox-wpa-%ld-%u", (long)getpid(),
                          ++control_socket_serial);
    if (length < 0 || (size_t)length >= sizeof(local.sun_path))
    {
        close(socket_fd);
        return -ENAMETOOLONG;
    }

    unlink(local.sun_path);
    if (bind(socket_fd, (struct sockaddr *)&local, sizeof(local)) != 0)
    {
        int result = -errno;
        close(socket_fd);
        return result;
    }

    struct sockaddr_un remote;
    memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    copy_string(remote.sun_path, sizeof(remote.sun_path), WPA_CONTROL_PATH);

    size_t command_length = strlen(command);
    if (sendto(socket_fd, command, command_length, 0,
               (struct sockaddr *)&remote, sizeof(remote)) < 0)
    {
        int result = -errno;
        unlink(local.sun_path);
        close(socket_fd);
        return result;
    }

    struct pollfd descriptor = {
        .fd = socket_fd,
        .events = POLLIN,
    };
    int polled;
    do
        polled = poll(&descriptor, 1, 2000);
    while (polled < 0 && errno == EINTR);
    if (polled <= 0)
    {
        int result = polled == 0 ? -ETIMEDOUT : -errno;
        unlink(local.sun_path);
        close(socket_fd);
        return result;
    }

    ssize_t received;
    do
        received = recv(socket_fd, reply, reply_size - 1, 0);
    while (received < 0 && errno == EINTR);
    int result = 0;
    if (received < 0)
        result = -errno;
    else
        reply[received] = '\0';

    unlink(local.sun_path);
    close(socket_fd);
    return result;
}

static bool real_power_is_on(void)
{
    struct ap80_service_status status;
    ap80_services_get_status(&status);
    return status.wifi_enabled;
}

static int real_set_power(bool enabled)
{
    if (!(ap80_services_claimed() & AP80_SERVICE_WIFI))
    {
        int result = ap80_services_claim(AP80_SERVICE_WIFI);
        if (result != 0 && result != -EALREADY)
            return result;
    }

    int result = ap80_services_set_enabled(AP80_SERVICE_WIFI, enabled);
    if (result != 0 || !enabled)
        return result;

    for (unsigned int poll = 0; poll < 100; poll++)
    {
        if (access(WPA_CONTROL_PATH, F_OK) == 0)
            return 0;
        default_sleep_ms(WIFI_POLL_INTERVAL_MS);
    }
    return -ETIMEDOUT;
}

static int real_secure_config(void)
{
    const char *path = getenv("env_wifi_wpa_supplicant_conf");
    if (!path || path[0] != '/')
        path = WPA_DATA_CONFIG;

    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
        return -errno;

    struct stat status;
    int result = 0;
    if (fstat(descriptor, &status) != 0)
        result = -errno;
    else if (!S_ISREG(status.st_mode))
        result = -EINVAL;
    else if (fchmod(descriptor, S_IRUSR | S_IWUSR) != 0)
        result = -errno;

    close(descriptor);
    return result;
}

static const struct wifi_backend real_backend = {
    .power_is_on = real_power_is_on,
    .set_power = real_set_power,
    .wpa_command = real_wpa_command,
    .secure_config = real_secure_config,
    .sleep_ms = default_sleep_ms,
};

static void read_interface_ipv4(char *address, size_t address_size)
{
    struct ifaddrs *interfaces;
    if (getifaddrs(&interfaces) != 0)
        return;

    for (struct ifaddrs *interface = interfaces; interface;
         interface = interface->ifa_next)
    {
        if (!interface->ifa_addr ||
            interface->ifa_addr->sa_family != AF_INET ||
            strcmp(interface->ifa_name, "wlan0") != 0)
            continue;

        struct sockaddr_in *ipv4 =
            (struct sockaddr_in *)interface->ifa_addr;
        if (inet_ntop(AF_INET, &ipv4->sin_addr, address,
                      address_size) != NULL)
            break;
    }
    freeifaddrs(interfaces);
}
#endif

#ifdef SIMULATOR
static bool simulator_powered;
static bool simulator_connected;
static int simulator_network_id;
static int simulator_next_network_id;
static char simulator_ssid[AP80_WIFI_SSID_TEXT_SIZE];

static int simulator_reply(char *reply, size_t reply_size, const char *text)
{
    if (!reply || reply_size == 0)
        return -EINVAL;
    if (strlen(text) >= reply_size)
        return -ENOSPC;
    copy_string(reply, reply_size, text);
    return 0;
}

static void simulator_reset(void)
{
    simulator_powered = false;
    simulator_connected = false;
    simulator_network_id = -1;
    simulator_next_network_id = 1;
    copy_string(simulator_ssid, sizeof(simulator_ssid), "Rockbox Lab");
}

static bool simulator_power_is_on(void)
{
    return simulator_powered;
}

static int simulator_set_power(bool enabled)
{
    simulator_powered = enabled;
    if (!enabled)
    {
        simulator_connected = false;
        simulator_network_id = -1;
    }
    return 0;
}

static int simulator_wpa_command(const char *command, char *reply,
                                 size_t reply_size)
{
    static const char scan_results[] =
        "bssid / frequency / signal level / flags / ssid\n"
        "02:00:00:00:00:01\t2412\t-41\t[WPA2-PSK-CCMP][ESS]\t"
        "Rockbox Lab\n"
        "02:00:00:00:00:02\t2437\t-67\t[ESS]\tGuest\\x20Radio\n"
        "02:00:00:00:00:03\t2462\t-73\t[WPA2-PSK-CCMP][ESS]\t"
        "Saved Cabin\n";
    static const char saved_networks[] =
        "network id / ssid / bssid / flags\n"
        "0\tSaved Cabin\tany\t\n";

    if (!simulator_powered)
        return -ENETDOWN;
    if (strcmp(command, "BSS_FLUSH 0") == 0 ||
        strcmp(command, "SCAN") == 0 ||
        strncmp(command, "ENABLE_NETWORK ", 15) == 0 ||
        strcmp(command, "RECONFIGURE") == 0 ||
        strcmp(command, "SAVE_CONFIG") == 0)
        return simulator_reply(reply, reply_size, "OK\n");
    if (strncmp(command, "SET_NETWORK ", 12) == 0)
    {
        const char *ssid_value = strstr(command, " ssid ");
        if (ssid_value)
        {
            ssid_value += 6;
            uint8_t raw_ssid[AP80_WIFI_SSID_MAX_BYTES];
            size_t raw_length = 0;
            while (ssid_value[0] != '\0' && ssid_value[1] != '\0' &&
                   raw_length < sizeof(raw_ssid))
            {
                int high = hex_value((unsigned char)ssid_value[0]);
                int low = hex_value((unsigned char)ssid_value[1]);
                if (high < 0 || low < 0)
                    break;
                raw_ssid[raw_length++] =
                    (uint8_t)((high << 4) | low);
                ssid_value += 2;
            }
            format_ssid(raw_ssid, raw_length, simulator_ssid,
                        sizeof(simulator_ssid));
        }
        return simulator_reply(reply, reply_size, "OK\n");
    }
    if (strcmp(command, "SCAN_RESULTS") == 0)
        return simulator_reply(reply, reply_size, scan_results);
    if (strcmp(command, "LIST_NETWORKS") == 0)
        return simulator_reply(reply, reply_size, saved_networks);
    if (strcmp(command, "ADD_NETWORK") == 0)
    {
        char identifier[16];
        snprintf(identifier, sizeof(identifier), "%d\n",
                 simulator_next_network_id++);
        return simulator_reply(reply, reply_size, identifier);
    }
    if (strncmp(command, "SELECT_NETWORK ", 15) == 0)
    {
        simulator_network_id = (int)strtol(command + 15, NULL, 10);
        simulator_connected = true;
        if (simulator_network_id == 0)
            copy_string(simulator_ssid, sizeof(simulator_ssid),
                        "Saved Cabin");
        else
            copy_string(simulator_ssid, sizeof(simulator_ssid),
                        "Rockbox Lab");
        return simulator_reply(reply, reply_size, "OK\n");
    }
    if (strcmp(command, "STATUS") == 0)
    {
        char status[512];
        if (simulator_connected)
            snprintf(status, sizeof(status),
                     "bssid=02:00:00:00:00:01\n"
                     "ssid=%s\nid=%d\nwpa_state=COMPLETED\n"
                     "ip_address=192.0.2.80\n",
                     simulator_ssid, simulator_network_id);
        else
            copy_string(status, sizeof(status),
                        "wpa_state=DISCONNECTED\n");
        return simulator_reply(reply, reply_size, status);
    }
    if (strcmp(command, "SIGNAL_POLL") == 0)
        return simulator_reply(reply, reply_size, "RSSI=-41\n");
    if (strcmp(command, "DISCONNECT") == 0)
    {
        simulator_connected = false;
        simulator_network_id = -1;
        return simulator_reply(reply, reply_size, "OK\n");
    }
    if (strncmp(command, "REMOVE_NETWORK ", 15) == 0)
    {
        int identifier = (int)strtol(command + 15, NULL, 10);
        if (identifier == simulator_network_id)
        {
            simulator_connected = false;
            simulator_network_id = -1;
        }
        return simulator_reply(reply, reply_size, "OK\n");
    }
    return simulator_reply(reply, reply_size, "FAIL\n");
}

static int simulator_secure_config(void)
{
    return 0;
}

static const struct wifi_backend simulator_backend = {
    .power_is_on = simulator_power_is_on,
    .set_power = simulator_set_power,
    .wpa_command = simulator_wpa_command,
    .secure_config = simulator_secure_config,
    .sleep_ms = default_sleep_ms,
};
#endif

static bool reply_is_ok(const char *reply)
{
    return strncmp(reply, "OK", 2) == 0 &&
           (reply[2] == '\0' || reply[2] == '\r' || reply[2] == '\n');
}

static int send_command(const char *command, char *reply, size_t reply_size)
{
    int result = backend->wpa_command(command, reply, reply_size);
    if (result == 0 && strncmp(reply, "FAIL", 4) == 0)
        result = -EIO;
    return result;
}

static int send_ok_command(const char *command)
{
    char reply[32];
    int result = send_command(command, reply, sizeof(reply));
    if (result == 0 && !reply_is_ok(reply))
        result = -EIO;
    return result;
}

static int save_config_securely(void)
{
    int result = backend->secure_config();
    if (result == 0)
        result = send_ok_command("SAVE_CONFIG");
    if (result == 0)
        result = backend->secure_config();
    return result;
}

static bool scan_results_have_data(const char *results)
{
    const char *line = strchr(results, '\n');
    if (!line)
        return false;
    while (*line == '\r' || *line == '\n')
        line++;
    return *line != '\0';
}

static int refresh_snapshot(struct ap80_wifi_snapshot *snapshot)
{
    char status[2048];
    char signal[512];
    int result = send_command("STATUS", status, sizeof(status));
    if (result != 0)
        return result;

    signal[0] = '\0';
    (void)send_command("SIGNAL_POLL", signal, sizeof(signal));
    ap80_wifi_parse_status(status, signal, snapshot);

#ifndef SIMULATOR
#ifdef AP80_WIFI_TEST
    bool real_active = !test_backend_configured;
#else
    bool real_active = true;
#endif
    if (real_active && snapshot->phase == AP80_WIFI_CONNECTED &&
        snapshot->ipv4[0] == '\0')
        read_interface_ipv4(snapshot->ipv4, sizeof(snapshot->ipv4));
#endif
    return 0;
}

static int scan_networks(struct ap80_wifi_network *networks,
                         size_t *network_count)
{
    /*
     * SCAN_RESULTS remains populated with the previous table while a new
     * scan runs. Flush that cache first so non-header data unambiguously
     * belongs to this request.
     */
    int result = send_ok_command("BSS_FLUSH 0");
    if (result != 0)
        return result;
    result = send_ok_command("SCAN");
    if (result != 0)
        return result;

    char *scan_results = malloc(WPA_REPLY_SIZE);
    char *saved_networks = malloc(WPA_REPLY_SIZE);
    if (!scan_results || !saved_networks)
    {
        free(scan_results);
        free(saved_networks);
        return -ENOMEM;
    }

    scan_results[0] = '\0';
    for (unsigned int poll = 0; poll < WIFI_SCAN_POLLS; poll++)
    {
        backend->sleep_ms(WIFI_POLL_INTERVAL_MS);
        result = send_command("SCAN_RESULTS", scan_results, WPA_REPLY_SIZE);
        if (result != 0)
            break;
        if (scan_results_have_data(scan_results))
            break;
    }

    if (result == 0)
        result = ap80_wifi_parse_scan_results(
            scan_results, networks, AP80_WIFI_MAX_NETWORKS, network_count);
    if (result == 0)
    {
        result = send_command("LIST_NETWORKS", saved_networks,
                              WPA_REPLY_SIZE);
        if (result == 0)
            result = ap80_wifi_apply_saved_networks(
                saved_networks, networks, AP80_WIFI_MAX_NETWORKS,
                network_count);
    }

    free(scan_results);
    free(saved_networks);
    return result;
}

static int wait_for_association(struct ap80_wifi_snapshot *snapshot)
{
    int result = -ETIMEDOUT;
    for (unsigned int poll = 0; poll < WIFI_ASSOCIATION_POLLS; poll++)
    {
        result = refresh_snapshot(snapshot);
        if (result == 0 && snapshot->phase == AP80_WIFI_CONNECTED)
            break;
        backend->sleep_ms(WIFI_POLL_INTERVAL_MS);
    }
    if (result != 0 || snapshot->phase != AP80_WIFI_CONNECTED)
        return result == 0 ? -ETIMEDOUT : result;

    for (unsigned int poll = 0;
         snapshot->ipv4[0] == '\0' && poll < WIFI_IP_POLLS; poll++)
    {
        backend->sleep_ms(WIFI_POLL_INTERVAL_MS);
        result = refresh_snapshot(snapshot);
        if (result != 0)
            break;
    }
    return result;
}

static int set_network_value(int network_id, const char *name,
                             const char *value, char *command,
                             size_t command_size)
{
    int length = snprintf(command, command_size, "SET_NETWORK %d %s %s",
                          network_id, name, value);
    if (length < 0 || (size_t)length >= command_size)
        return -ENOSPC;
    return send_ok_command(command);
}

static int connect_network(const struct ap80_wifi_network *network,
                           const char *passphrase,
                           struct ap80_wifi_snapshot *snapshot,
                           int *connected_network_id)
{
    char command[WPA_COMMAND_SIZE];
    char reply[64];
    char encoded_ssid[AP80_WIFI_SSID_MAX_BYTES * 2 + 1];
    char encoded_psk[AP80_WIFI_PASSPHRASE_MAX * 2 + 3];
    int result = 0;
    int network_id = network->saved ? network->network_id : -1;
    bool created = false;
    bool supplied_passphrase = passphrase[0] != '\0';
    bool selection_attempted = false;
    bool modified_existing = false;

    if ((network->security & (AP80_WIFI_SECURITY_WEP |
                              AP80_WIFI_SECURITY_EAP)) != 0 ||
        ((network->security & AP80_WIFI_SECURITY_SAE) != 0 &&
         (network->security & AP80_WIFI_SECURITY_PSK) == 0))
    {
        result = -EOPNOTSUPP;
        goto done;
    }

    if (network_id < 0)
    {
        result = send_command("ADD_NETWORK", reply, sizeof(reply));
        if (result != 0)
            goto done;
        char *end;
        long parsed = strtol(reply, &end, 10);
        if (end == reply || parsed < 0 || parsed > 9999)
        {
            result = -EIO;
            goto done;
        }
        network_id = (int)parsed;
        created = true;
    }

    modified_existing = !created &&
        (supplied_passphrase ||
         network->security == AP80_WIFI_SECURITY_OPEN);

    if (created || supplied_passphrase ||
        network->security == AP80_WIFI_SECURITY_OPEN)
    {
        result = ap80_wifi_encode_ssid(network->ssid,
                                       network->ssid_length,
                                       encoded_ssid,
                                       sizeof(encoded_ssid));
        if (result != 0)
            goto done;
        result = set_network_value(network_id, "ssid", encoded_ssid,
                                   command, sizeof(command));
        if (result != 0)
            goto done;
        if (network->bssid[0] == '\0')
        {
            result = set_network_value(network_id, "scan_ssid", "1",
                                       command, sizeof(command));
            if (result != 0)
                goto done;
        }
    }

    if (network->security == AP80_WIFI_SECURITY_OPEN)
    {
        result = set_network_value(network_id, "key_mgmt", "NONE",
                                   command, sizeof(command));
        if (result != 0)
            goto done;
    }
    else if (supplied_passphrase)
    {
        result = ap80_wifi_encode_psk(passphrase, encoded_psk,
                                      sizeof(encoded_psk));
        if (result != 0)
            goto done;
        result = set_network_value(network_id, "psk", encoded_psk,
                                   command, sizeof(command));
        if (result != 0)
            goto done;
        result = set_network_value(network_id, "key_mgmt", "WPA-PSK",
                                   command, sizeof(command));
        if (result != 0)
            goto done;
    }
    else if (created)
    {
        result = -EINVAL;
        goto done;
    }

    int length = snprintf(command, sizeof(command), "ENABLE_NETWORK %d",
                          network_id);
    if (length < 0 || (size_t)length >= sizeof(command))
    {
        result = -ENOSPC;
        goto done;
    }
    result = send_ok_command(command);
    if (result != 0)
        goto done;

    length = snprintf(command, sizeof(command), "SELECT_NETWORK %d",
                      network_id);
    if (length < 0 || (size_t)length >= sizeof(command))
    {
        result = -ENOSPC;
        goto done;
    }
    selection_attempted = true;
    result = send_ok_command(command);
    if (result != 0)
        goto done;

    result = wait_for_association(snapshot);
    if (result == 0 && (created || supplied_passphrase))
        result = save_config_securely();
    if (result == 0)
        *connected_network_id = network_id;

done:
    if (result != 0)
    {
        if (created && network_id >= 0)
        {
            snprintf(command, sizeof(command), "REMOVE_NETWORK %d",
                     network_id);
            (void)send_ok_command(command);
        }
        else if (modified_existing)
        {
            /* Discard unsaved SSID/security/PSK edits on an old profile. */
            (void)send_ok_command("RECONFIGURE");
        }

        /*
         * SELECT_NETWORK disables every other profile. Restore fallback
         * networks even when the failed candidate was an existing profile.
         */
        if (selection_attempted)
            (void)send_ok_command("ENABLE_NETWORK all");
    }
    secure_zero(command, sizeof(command));
    secure_zero(encoded_ssid, sizeof(encoded_ssid));
    secure_zero(encoded_psk, sizeof(encoded_psk));
    return result;
}

static int disconnect_network(struct ap80_wifi_snapshot *snapshot)
{
    int result = send_ok_command("DISCONNECT");
    if (result == 0)
        result = refresh_snapshot(snapshot);
    return result;
}

static int forget_network(const struct ap80_wifi_network *network,
                          struct ap80_wifi_snapshot *snapshot)
{
    if (!network->saved || network->network_id < 0)
        return -EINVAL;

    int result = 0;
    if (network->current)
        result = send_ok_command("DISCONNECT");

    char command[64];
    if (result == 0)
    {
        int length = snprintf(command, sizeof(command), "REMOVE_NETWORK %d",
                              network->network_id);
        if (length < 0 || (size_t)length >= sizeof(command))
            result = -ENOSPC;
        else
            result = send_ok_command(command);
    }
    if (result == 0)
        result = save_config_securely();
    if (result == 0)
        result = refresh_snapshot(snapshot);
    return result;
}

struct operation_networks
{
    bool replace;
    struct ap80_wifi_network networks[AP80_WIFI_MAX_NETWORKS];
    size_t count;
};

static int perform_operation(const struct wifi_command *command,
                             struct ap80_wifi_snapshot *snapshot,
                             struct operation_networks *operation_networks)
{
    int result;
    switch (command->operation)
    {
        case WIFI_OPERATION_POWER:
            result = backend->set_power(command->enabled);
            snapshot->powered = backend->power_is_on();
            if (result == 0 && command->enabled)
            {
                snapshot->phase = AP80_WIFI_IDLE;
                (void)refresh_snapshot(snapshot);
            }
            else if (result == 0)
            {
                snapshot->phase = AP80_WIFI_OFF;
                snapshot->ssid[0] = '\0';
                snapshot->bssid[0] = '\0';
                snapshot->ipv4[0] = '\0';
                snapshot->rssi_dbm = -127;
                snapshot->network_id = -1;
                operation_networks->replace = true;
                operation_networks->count = 0;
            }
            return result;

        case WIFI_OPERATION_SCAN:
            result = scan_networks(operation_networks->networks,
                                   &operation_networks->count);
            if (result == 0)
            {
                operation_networks->replace = true;
                (void)refresh_snapshot(snapshot);
            }
            return result;

        case WIFI_OPERATION_CONNECT:
        {
            int network_id = -1;
            result = connect_network(&command->network,
                                     command->passphrase, snapshot,
                                     &network_id);
            if (result == 0)
            {
                snapshot->network_id = network_id;
                snapshot->phase = AP80_WIFI_CONNECTED;
            }
            return result;
        }

        case WIFI_OPERATION_DISCONNECT:
            return disconnect_network(snapshot);

        case WIFI_OPERATION_FORGET:
            return forget_network(&command->network, snapshot);

        case WIFI_OPERATION_NONE:
            break;
    }
    return -EINVAL;
}

static void update_current_network_locked(void)
{
    for (size_t index = 0; index < controller.network_count; index++)
        controller.networks[index].current =
            controller.snapshot.phase == AP80_WIFI_CONNECTED &&
            controller.networks[index].network_id ==
            controller.snapshot.network_id;
}

static void reconcile_network_operation_locked(
    const struct wifi_command *command, int result,
    const struct ap80_wifi_snapshot *snapshot)
{
    if (result != 0)
        return;

    for (size_t index = 0; index < controller.network_count; index++)
    {
        struct ap80_wifi_network *network = &controller.networks[index];
        if (command->operation == WIFI_OPERATION_CONNECT &&
            same_ssid(network, &command->network))
        {
            network->saved = true;
            network->network_id = snapshot->network_id;
        }
        else if (command->operation == WIFI_OPERATION_FORGET &&
                 network->network_id == command->network.network_id)
        {
            network->saved = false;
            network->current = false;
            network->network_id = -1;
        }
    }
}

static void publish_refresh(struct ap80_wifi_snapshot *snapshot)
{
    pthread_mutex_lock(&controller.mutex);
    if (!controller.stop && !controller.pending &&
        !controller.snapshot.busy)
    {
        snapshot->generation = controller.snapshot.generation + 1;
        snapshot->network_count = controller.network_count;
        controller.snapshot = *snapshot;
        update_current_network_locked();
    }
    pthread_mutex_unlock(&controller.mutex);
}

static void *wifi_worker(void *unused)
{
    (void)unused;
    for (;;)
    {
        pthread_mutex_lock(&controller.mutex);
        bool timed_refresh = false;
        while (!controller.stop && !controller.pending)
        {
            if (!controller.snapshot.powered)
                pthread_cond_wait(&controller.condition,
                                  &controller.mutex);
            else
            {
                struct timespec deadline;
                clock_gettime(CLOCK_REALTIME, &deadline);
                deadline.tv_sec += WIFI_REFRESH_SECONDS;
                int result = pthread_cond_timedwait(
                    &controller.condition, &controller.mutex, &deadline);
                if (result == ETIMEDOUT && !controller.pending)
                {
                    timed_refresh = true;
                    break;
                }
            }
        }

        if (controller.stop)
        {
            pthread_mutex_unlock(&controller.mutex);
            break;
        }

        if (timed_refresh)
        {
            struct ap80_wifi_snapshot snapshot = controller.snapshot;
            pthread_mutex_unlock(&controller.mutex);
            if (refresh_snapshot(&snapshot) == 0)
                publish_refresh(&snapshot);
            continue;
        }

        struct wifi_command command = controller.command;
        secure_zero(controller.command.passphrase,
                    sizeof(controller.command.passphrase));
        controller.pending = false;
        struct ap80_wifi_snapshot snapshot = controller.snapshot;
        pthread_mutex_unlock(&controller.mutex);

        struct operation_networks operation_networks;
        memset(&operation_networks, 0, sizeof(operation_networks));
        int result = perform_operation(&command, &snapshot,
                                       &operation_networks);
        secure_zero(command.passphrase, sizeof(command.passphrase));

        pthread_mutex_lock(&controller.mutex);
#ifdef AP80_WIFI_TEST
        test_credentials_cleared = true;
#endif
        if (operation_networks.replace && result == 0)
        {
            controller.network_count = operation_networks.count;
            memcpy(controller.networks, operation_networks.networks,
                   operation_networks.count *
                   sizeof(operation_networks.networks[0]));
        }
        reconcile_network_operation_locked(&command, result, &snapshot);

        snapshot.busy = false;
        snapshot.last_error = result;
        snapshot.powered = backend->power_is_on();
        if (result != 0)
            snapshot.phase = AP80_WIFI_ERROR;
        else if (!snapshot.powered)
            snapshot.phase = AP80_WIFI_OFF;
        else if (snapshot.phase != AP80_WIFI_CONNECTED)
            snapshot.phase = AP80_WIFI_IDLE;
        snapshot.generation = controller.snapshot.generation + 1;
        snapshot.network_count = controller.network_count;
        controller.snapshot = snapshot;
        update_current_network_locked();
        pthread_mutex_unlock(&controller.mutex);

        secure_zero(&command, sizeof(command));
        secure_zero(&operation_networks, sizeof(operation_networks));
    }
    return NULL;
}

#ifdef AP80_WIFI_TEST
int ap80_wifi_set_test_backend(
    const struct ap80_wifi_test_backend *test_backend)
{
    if (!test_backend || !test_backend->power_is_on ||
        !test_backend->set_power || !test_backend->wpa_command ||
        !test_backend->secure_config || !test_backend->sleep_ms)
        return -EINVAL;

    pthread_mutex_lock(&controller.mutex);
    if (controller.initialized)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -EBUSY;
    }
    configured_test_backend.power_is_on = test_backend->power_is_on;
    configured_test_backend.set_power = test_backend->set_power;
    configured_test_backend.wpa_command = test_backend->wpa_command;
    configured_test_backend.secure_config = test_backend->secure_config;
    configured_test_backend.sleep_ms = test_backend->sleep_ms;
    test_backend_configured = true;
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

bool ap80_wifi_test_credentials_cleared(void)
{
    pthread_mutex_lock(&controller.mutex);
    bool cleared = test_credentials_cleared;
    pthread_mutex_unlock(&controller.mutex);
    return cleared;
}
#endif

int ap80_wifi_init(void)
{
    pthread_mutex_lock(&controller.mutex);
    if (controller.initialized)
    {
        pthread_mutex_unlock(&controller.mutex);
        return 0;
    }

#ifdef AP80_WIFI_TEST
    if (test_backend_configured)
        backend = &configured_test_backend;
    else
#endif
#ifdef SIMULATOR
    {
        simulator_reset();
        backend = &simulator_backend;
    }
#else
        backend = &real_backend;
#endif

    memset(&controller.command, 0, sizeof(controller.command));
    memset(&controller.snapshot, 0, sizeof(controller.snapshot));
    memset(controller.networks, 0, sizeof(controller.networks));
    controller.network_count = 0;
    controller.stop = false;
    controller.pending = false;
    controller.snapshot.powered = backend->power_is_on();
    controller.snapshot.phase = controller.snapshot.powered ?
                                AP80_WIFI_IDLE : AP80_WIFI_OFF;
    controller.snapshot.rssi_dbm = -127;
    controller.snapshot.network_id = -1;
    controller.snapshot.generation = 1;
    controller.initialized = true;

    int result = pthread_create(&controller.worker, NULL, wifi_worker, NULL);
    if (result != 0)
    {
        controller.initialized = false;
        pthread_mutex_unlock(&controller.mutex);
        return -result;
    }

    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

void ap80_wifi_shutdown(void)
{
    pthread_mutex_lock(&controller.mutex);
    if (!controller.initialized)
    {
        pthread_mutex_unlock(&controller.mutex);
        return;
    }

    controller.stop = true;
    pthread_cond_signal(&controller.condition);
    pthread_mutex_unlock(&controller.mutex);
    pthread_join(controller.worker, NULL);

    pthread_mutex_lock(&controller.mutex);
    secure_zero(&controller.command, sizeof(controller.command));
    controller.initialized = false;
    controller.pending = false;
#ifdef AP80_WIFI_TEST
    test_credentials_cleared = true;
#endif
    pthread_mutex_unlock(&controller.mutex);
}

static int queue_operation(enum wifi_operation operation, bool enabled,
                           const struct ap80_wifi_network *network,
                           const char *passphrase)
{
    size_t passphrase_length = 0;
    if (passphrase)
    {
        passphrase_length =
            strnlen(passphrase, AP80_WIFI_PASSPHRASE_MAX + 1);
        if (passphrase_length > AP80_WIFI_PASSPHRASE_MAX)
            return -EINVAL;
    }

    pthread_mutex_lock(&controller.mutex);
    if (!controller.initialized)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -ENODEV;
    }
    if (controller.pending || controller.snapshot.busy)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -EBUSY;
    }
    if (operation != WIFI_OPERATION_POWER &&
        !controller.snapshot.powered)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -ENETDOWN;
    }

    memset(&controller.command, 0, sizeof(controller.command));
    controller.command.operation = operation;
    controller.command.enabled = enabled;
    if (network)
        controller.command.network = *network;
    if (passphrase_length != 0)
        memcpy(controller.command.passphrase, passphrase,
               passphrase_length + 1);

    controller.pending = true;
    controller.snapshot.busy = true;
    controller.snapshot.last_error = 0;
    switch (operation)
    {
        case WIFI_OPERATION_POWER:
            controller.snapshot.phase = enabled ?
                AP80_WIFI_POWERING_ON : AP80_WIFI_POWERING_OFF;
            break;
        case WIFI_OPERATION_SCAN:
            controller.snapshot.phase = AP80_WIFI_SCANNING;
            break;
        case WIFI_OPERATION_CONNECT:
            controller.snapshot.phase = AP80_WIFI_CONNECTING;
            break;
        default:
            break;
    }
    controller.snapshot.generation++;
#ifdef AP80_WIFI_TEST
    if (operation == WIFI_OPERATION_CONNECT && passphrase_length != 0)
        test_credentials_cleared = false;
#endif
    pthread_cond_signal(&controller.condition);
    pthread_mutex_unlock(&controller.mutex);
    return 0;
}

int ap80_wifi_request_power(bool enabled)
{
    pthread_mutex_lock(&controller.mutex);
    if (!controller.initialized)
    {
        pthread_mutex_unlock(&controller.mutex);
        return -ENODEV;
    }
    bool unchanged = !controller.pending && !controller.snapshot.busy &&
                     controller.snapshot.powered == enabled;
    pthread_mutex_unlock(&controller.mutex);
    if (unchanged)
        return 0;
    return queue_operation(WIFI_OPERATION_POWER, enabled, NULL, NULL);
}

int ap80_wifi_request_scan(void)
{
    return queue_operation(WIFI_OPERATION_SCAN, false, NULL, NULL);
}

int ap80_wifi_request_connect(const struct ap80_wifi_network *network,
                              const char *passphrase)
{
    if (!network ||
        network->ssid_length > AP80_WIFI_SSID_MAX_BYTES ||
        network->ssid_length == 0)
        return -EINVAL;
    if ((network->security & (AP80_WIFI_SECURITY_WEP |
                              AP80_WIFI_SECURITY_EAP)) != 0 ||
        ((network->security & AP80_WIFI_SECURITY_SAE) != 0 &&
         (network->security & AP80_WIFI_SECURITY_PSK) == 0))
        return -EOPNOTSUPP;

    if (network->security != AP80_WIFI_SECURITY_OPEN &&
        (!passphrase || passphrase[0] == '\0') &&
        (!network->saved || network->network_id < 0))
        return -EINVAL;

    if (passphrase && passphrase[0] != '\0' &&
        network->security != AP80_WIFI_SECURITY_OPEN)
    {
        char encoded[AP80_WIFI_PASSPHRASE_MAX * 2 + 3];
        int result = ap80_wifi_encode_psk(passphrase, encoded,
                                          sizeof(encoded));
        secure_zero(encoded, sizeof(encoded));
        if (result != 0)
            return result;
    }
    return queue_operation(WIFI_OPERATION_CONNECT, false, network,
                           passphrase);
}

int ap80_wifi_request_disconnect(void)
{
    return queue_operation(WIFI_OPERATION_DISCONNECT, false, NULL, NULL);
}

int ap80_wifi_request_forget(const struct ap80_wifi_network *network)
{
    if (!network || !network->saved || network->network_id < 0)
        return -EINVAL;
    return queue_operation(WIFI_OPERATION_FORGET, false, network, NULL);
}

void ap80_wifi_get_snapshot(struct ap80_wifi_snapshot *snapshot)
{
    if (!snapshot)
        return;
    pthread_mutex_lock(&controller.mutex);
    *snapshot = controller.snapshot;
    pthread_mutex_unlock(&controller.mutex);
}

size_t ap80_wifi_get_networks(struct ap80_wifi_network *networks,
                              size_t capacity)
{
    pthread_mutex_lock(&controller.mutex);
    size_t copied = controller.network_count;
    if (copied > capacity)
        copied = capacity;
    if (networks && copied != 0)
        memcpy(networks, controller.networks,
               copied * sizeof(controller.networks[0]));
    pthread_mutex_unlock(&controller.mutex);
    return copied;
}
