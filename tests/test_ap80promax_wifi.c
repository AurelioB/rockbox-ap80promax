#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "wifi-ap80promax.h"

#ifdef AP80_WIFI_TEST
static bool fake_powered;
static bool fake_connected;
static int fake_network_id = -1;
static bool saw_escaped_psk;
static bool saw_enable_all;
static bool saw_bss_flush;
static bool saw_reconfigure;
static bool saw_scan_ssid;
static bool force_association_failure;
static unsigned int scan_result_requests;
static unsigned int secure_config_calls;
static char fake_ssid[AP80_WIFI_SSID_TEXT_SIZE] = "Cafe\\tLab";

static bool fake_power_is_on(void)
{
    return fake_powered;
}

static int fake_set_power(bool enabled)
{
    usleep(20000);
    fake_powered = enabled;
    if (!enabled)
    {
        fake_connected = false;
        fake_network_id = -1;
    }
    return 0;
}

static int reply_with(char *reply, size_t reply_size, const char *text)
{
    size_t length = strlen(text);
    if (length >= reply_size)
        return -ENOSPC;
    memcpy(reply, text, length + 1);
    return 0;
}

static int fake_wpa_command(const char *command, char *reply,
                            size_t reply_size)
{
    static const char scan_results[] =
        "bssid / frequency / signal level / flags / ssid\n"
        "02:00:00:00:00:05\t2412\t-35\t[WPA2-PSK-CCMP][ESS]\t"
        "Cafe\\tLab\n"
        "02:00:00:00:00:06\t2437\t-65\t[ESS]\tGuest\n";
    static const char saved_networks[] =
        "network id / ssid / bssid / flags\n"
        "0\tWorkshop Hidden\tany\t\n";
    static const char scan_header[] =
        "bssid / frequency / signal level / flags / ssid\n";

    assert(fake_powered);
    if (strcmp(command, "BSS_FLUSH 0") == 0)
    {
        saw_bss_flush = true;
        scan_result_requests = 0;
        return reply_with(reply, reply_size, "OK\n");
    }
    if (strcmp(command, "SCAN") == 0 ||
        strncmp(command, "SET_NETWORK ", 12) == 0 ||
        strncmp(command, "ENABLE_NETWORK ", 15) == 0 ||
        strcmp(command, "RECONFIGURE") == 0 ||
        strcmp(command, "SAVE_CONFIG") == 0)
    {
        if (strcmp(command, "ENABLE_NETWORK all") == 0)
            saw_enable_all = true;
        if (strcmp(command, "RECONFIGURE") == 0)
            saw_reconfigure = true;
        if (strstr(command, " scan_ssid 1") != NULL)
            saw_scan_ssid = true;
        if (strstr(command, " psk ") != NULL)
        {
            assert(strchr(command, '\n') == NULL);
            saw_escaped_psk =
                strstr(command, "\\\"") != NULL &&
                strstr(command, "\\\\") != NULL;
        }
        return reply_with(reply, reply_size, "OK\n");
    }
    if (strcmp(command, "SCAN_RESULTS") == 0)
    {
        assert(saw_bss_flush);
        scan_result_requests++;
        if (scan_result_requests < 3)
            return reply_with(reply, reply_size, scan_header);
        return reply_with(reply, reply_size, scan_results);
    }
    if (strcmp(command, "LIST_NETWORKS") == 0)
        return reply_with(reply, reply_size, saved_networks);
    if (strcmp(command, "ADD_NETWORK") == 0)
        return reply_with(reply, reply_size, "3\n");
    if (strncmp(command, "SELECT_NETWORK ", 15) == 0)
    {
        fake_network_id = (int)strtol(command + 15, NULL, 10);
        fake_connected = !force_association_failure;
        if (fake_network_id == 0)
            strcpy(fake_ssid, "Workshop Hidden");
        else
            strcpy(fake_ssid, "Cafe\\tLab");
        return reply_with(reply, reply_size, "OK\n");
    }
    if (strcmp(command, "STATUS") == 0)
    {
        if (fake_connected)
        {
            char status[512];
            snprintf(status, sizeof(status),
                     "bssid=02:00:00:00:00:05\n"
                     "ssid=%s\n"
                     "id=%d\n"
                     "wpa_state=COMPLETED\n"
                     "ip_address=192.0.2.81\n",
                     fake_ssid, fake_network_id);
            return reply_with(reply, reply_size, status);
        }
        return reply_with(reply, reply_size,
                          "wpa_state=DISCONNECTED\n");
    }
    if (strcmp(command, "SIGNAL_POLL") == 0)
        return reply_with(reply, reply_size, "RSSI=-35\n");
    if (strcmp(command, "DISCONNECT") == 0)
    {
        fake_connected = false;
        fake_network_id = -1;
        return reply_with(reply, reply_size, "OK\n");
    }
    if (strncmp(command, "REMOVE_NETWORK ", 15) == 0)
    {
        fake_connected = false;
        fake_network_id = -1;
        return reply_with(reply, reply_size, "OK\n");
    }
    return reply_with(reply, reply_size, "FAIL\n");
}

static void fake_sleep_ms(unsigned int milliseconds)
{
    (void)milliseconds;
}

static int fake_secure_config(void)
{
    secure_config_calls++;
    return 0;
}
#endif

static struct ap80_wifi_snapshot wait_until_idle(void)
{
    struct ap80_wifi_snapshot snapshot;
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 1000000,
    };

    for (unsigned int attempt = 0; attempt < 5000; attempt++)
    {
        ap80_wifi_get_snapshot(&snapshot);
        if (!snapshot.busy)
            return snapshot;
        nanosleep(&delay, NULL);
    }
    assert(!"Wi-Fi operation timed out");
    return snapshot;
}

static void test_parsers_and_encoders(void)
{
    static const char scan_results[] =
        "bssid / frequency / signal level / flags / ssid\n"
        "00:11:22:33:44:55\t2412\t-70\t[ESS]\tGuest\\x20Net\n"
        "00:11:22:33:44:66\t2437\t-40\t[WPA2-PSK-CCMP][ESS]\t"
        "Secure\\\\Net\n"
        "00:11:22:33:44:77\t2462\t-80\t[ESS]\tGuest\\x20Net\n";
    struct ap80_wifi_network networks[4];
    size_t count;
    assert(ap80_wifi_parse_scan_results(scan_results, networks, 4,
                                         &count) == 0);
    assert(count == 2);
    assert(strcmp(networks[0].display_name, "Secure\\\\Net") == 0);
    assert(networks[0].security == AP80_WIFI_SECURITY_PSK);
    assert(strcmp(networks[1].display_name, "Guest Net") == 0);
    assert(networks[1].signal_dbm == -70);

    static const char saved[] =
        "network id / ssid / bssid / flags\n"
        "7\tGuest\\x20Net\tany\t[CURRENT]\n"
        "8\tHidden\\x20Saved\tany\t\n";
    assert(ap80_wifi_apply_saved_networks(saved, networks, 4, &count) == 0);
    assert(count == 3);
    assert(networks[0].saved);
    assert(networks[0].current);
    assert(networks[0].network_id == 7);
    struct ap80_wifi_network *hidden = NULL;
    for (size_t index = 0; index < count; index++)
    {
        if (strcmp(networks[index].display_name, "Hidden Saved") == 0)
            hidden = &networks[index];
    }
    assert(hidden != NULL);
    assert(hidden->saved);
    assert(hidden->bssid[0] == '\0');
    assert(hidden->security == AP80_WIFI_SECURITY_UNKNOWN);

    char encoded[132];
    assert(ap80_wifi_encode_psk("pass\"wo\\rd", encoded,
                                 sizeof(encoded)) == 0);
    assert(strcmp(encoded, "\"pass\\\"wo\\\\rd\"") == 0);
    assert(ap80_wifi_encode_psk("bad\npassword", encoded,
                                 sizeof(encoded)) == -EINVAL);
    assert(ap80_wifi_encode_psk(
               "0123456789abcdef0123456789abcdef"
               "0123456789abcdef0123456789abcdef",
               encoded, sizeof(encoded)) == 0);

    struct ap80_wifi_snapshot snapshot = {
        .powered = true,
        .phase = AP80_WIFI_IDLE,
    };
    ap80_wifi_parse_status("ssid=Test\\x20Net\n"
                           "bssid=00:11:22:33:44:55\n"
                           "id=9\nwpa_state=COMPLETED\n"
                           "ip_address=192.0.2.8\n",
                           "RSSI=-52\n", &snapshot);
    assert(snapshot.phase == AP80_WIFI_CONNECTED);
    assert(strcmp(snapshot.ssid, "Test Net") == 0);
    assert(strcmp(snapshot.ipv4, "192.0.2.8") == 0);
    assert(snapshot.rssi_dbm == -52);
    assert(snapshot.network_id == 9);
}

int main(void)
{
    test_parsers_and_encoders();

#ifdef AP80_WIFI_TEST
    const struct ap80_wifi_test_backend fake_backend = {
        .power_is_on = fake_power_is_on,
        .set_power = fake_set_power,
        .wpa_command = fake_wpa_command,
        .secure_config = fake_secure_config,
        .sleep_ms = fake_sleep_ms,
    };
    assert(ap80_wifi_set_test_backend(&fake_backend) == 0);
#endif
    assert(ap80_wifi_init() == 0);

    struct ap80_wifi_snapshot snapshot;
    ap80_wifi_get_snapshot(&snapshot);
    assert(snapshot.phase == AP80_WIFI_OFF);
    assert(!snapshot.powered);
    assert(ap80_wifi_request_scan() == -ENETDOWN);

    assert(ap80_wifi_request_power(true) == 0);
#ifdef AP80_WIFI_TEST
    assert(ap80_wifi_request_scan() == -EBUSY);
#endif
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
    assert(snapshot.powered);
    assert(snapshot.phase == AP80_WIFI_IDLE);

    assert(ap80_wifi_request_scan() == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
    assert(snapshot.network_count == 3);
#ifdef AP80_WIFI_TEST
    assert(scan_result_requests >= 3);
    assert(saw_bss_flush);
#endif

    struct ap80_wifi_network networks[AP80_WIFI_MAX_NETWORKS];
    size_t count = ap80_wifi_get_networks(
        networks, AP80_WIFI_MAX_NETWORKS);
    assert(count == 3);

    struct ap80_wifi_network *secure = NULL;
    for (size_t index = 0; index < count; index++)
    {
#ifdef AP80_WIFI_TEST
        if (strcmp(networks[index].display_name, "Cafe\\x09Lab") == 0)
#else
        if (strcmp(networks[index].display_name, "Rockbox Lab") == 0)
#endif
            secure = &networks[index];
    }
    assert(secure != NULL);
    assert(secure->security & AP80_WIFI_SECURITY_PSK);

#ifdef AP80_WIFI_TEST
    /*
     * A failed SELECT of a newly-added profile must remove it and re-enable
     * the profiles SELECT_NETWORK temporarily disabled.
     */
    force_association_failure = true;
    assert(ap80_wifi_request_connect(secure, "pass\"wo\\rd") == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == -ETIMEDOUT);
    assert(snapshot.phase == AP80_WIFI_ERROR);
    assert(saw_enable_all);
    assert(ap80_wifi_test_credentials_cleared());
    force_association_failure = false;
#endif

    assert(ap80_wifi_request_connect(secure, "pass\"wo\\rd") == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
    assert(snapshot.phase == AP80_WIFI_CONNECTED);
    assert(snapshot.network_id >= 0);
    assert(snapshot.ipv4[0] != '\0');
#ifdef AP80_WIFI_TEST
    assert(saw_escaped_psk);
    assert(secure_config_calls == 2);
    assert(ap80_wifi_test_credentials_cleared());
#endif

    count = ap80_wifi_get_networks(networks, AP80_WIFI_MAX_NETWORKS);
    secure = NULL;
    for (size_t index = 0; index < count; index++)
    {
        if (networks[index].current)
            secure = &networks[index];
    }
    assert(secure != NULL);
    assert(secure->saved);

    assert(ap80_wifi_request_disconnect() == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
    assert(snapshot.phase == AP80_WIFI_IDLE);

    count = ap80_wifi_get_networks(networks, AP80_WIFI_MAX_NETWORKS);
    secure = NULL;
    for (size_t index = 0; index < count; index++)
    {
        if (networks[index].saved && networks[index].network_id ==
            snapshot.network_id)
            secure = &networks[index];
    }
    /* Disconnect clears snapshot.network_id, so find the newly saved entry. */
    if (!secure)
    {
        for (size_t index = 0; index < count; index++)
        {
#ifdef AP80_WIFI_TEST
            if (strcmp(networks[index].display_name,
                       "Cafe\\x09Lab") == 0)
#else
            if (strcmp(networks[index].display_name,
                       "Rockbox Lab") == 0)
#endif
                secure = &networks[index];
        }
    }
    assert(secure != NULL && secure->saved);

#ifdef AP80_WIFI_TEST
    /*
     * A failed SELECT of an edited saved profile must reload its persisted
     * values and re-enable every fallback network.
     */
    force_association_failure = true;
    saw_enable_all = false;
    saw_reconfigure = false;
    assert(ap80_wifi_request_connect(secure, "replacement-pass") == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == -ETIMEDOUT);
    assert(saw_reconfigure);
    assert(saw_enable_all);
    force_association_failure = false;
#endif

    struct ap80_wifi_network forgotten = *secure;
    assert(ap80_wifi_request_forget(&forgotten) == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
#ifdef AP80_WIFI_TEST
    assert(secure_config_calls == 4);
#endif

    count = ap80_wifi_get_networks(networks, AP80_WIFI_MAX_NETWORKS);
    for (size_t index = 0; index < count; index++)
    {
        if (networks[index].ssid_length == forgotten.ssid_length &&
            memcmp(networks[index].ssid, forgotten.ssid,
                   forgotten.ssid_length) == 0)
            assert(!networks[index].saved);
    }

#ifdef AP80_WIFI_TEST
    /*
     * LIST_NETWORKS-only profiles remain actionable even when a hidden AP
     * was absent from the scan results. NULL reuses its saved credentials.
     */
    struct ap80_wifi_network *hidden_saved = NULL;
    for (size_t index = 0; index < count; index++)
    {
        if (strcmp(networks[index].display_name,
                   "Workshop Hidden") == 0)
            hidden_saved = &networks[index];
    }
    assert(hidden_saved != NULL);
    assert(hidden_saved->saved);
    assert(hidden_saved->security == AP80_WIFI_SECURITY_UNKNOWN);
    assert(hidden_saved->bssid[0] == '\0');
    assert(ap80_wifi_request_connect(hidden_saved, NULL) == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
    assert(snapshot.phase == AP80_WIFI_CONNECTED);
    assert(snapshot.network_id == 0);
    assert(ap80_wifi_request_disconnect() == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);

    /*
     * A manually-entered network has no BSSID, so the new profile must set
     * scan_ssid=1 before selection.
     */
    struct ap80_wifi_network manual_hidden;
    memset(&manual_hidden, 0, sizeof(manual_hidden));
    static const char manual_ssid[] = "Manual Hidden";
    memcpy(manual_hidden.ssid, manual_ssid, sizeof(manual_ssid) - 1);
    manual_hidden.ssid_length = sizeof(manual_ssid) - 1;
    manual_hidden.network_id = -1;
    saw_scan_ssid = false;
    assert(ap80_wifi_request_connect(&manual_hidden, NULL) == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
    assert(saw_scan_ssid);
    assert(ap80_wifi_request_disconnect() == 0);
    snapshot = wait_until_idle();
    assert(snapshot.last_error == 0);
#endif

    assert(ap80_wifi_request_power(false) == 0);
    snapshot = wait_until_idle();
    assert(snapshot.phase == AP80_WIFI_OFF);
    assert(!snapshot.powered);
    assert(snapshot.network_count == 0);

    ap80_wifi_shutdown();
    puts("AP80 Wi-Fi controller tests passed");
    return 0;
}
