#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "services-ap80promax.h"

static void write_file(const char *path, const char *contents)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(descriptor >= 0);
    size_t length = strlen(contents);
    assert(write(descriptor, contents, length) == (ssize_t)length);
    assert(close(descriptor) == 0);
}

static void test_rfkill_lookup(void)
{
    char root[] = "/tmp/ap80-rfkill-XXXXXX";
    assert(mkdtemp(root) != NULL);

    char wifi_directory[128];
    char bluetooth_directory[128];
    char path[PATH_MAX];
    snprintf(wifi_directory, sizeof(wifi_directory), "%s/rfkill0", root);
    snprintf(bluetooth_directory, sizeof(bluetooth_directory), "%s/rfkill7",
             root);
    assert(mkdir(wifi_directory, 0700) == 0);
    assert(mkdir(bluetooth_directory, 0700) == 0);

    snprintf(path, sizeof(path), "%s/type", wifi_directory);
    write_file(path, "wlan\n");
    snprintf(path, sizeof(path), "%s/state", wifi_directory);
    write_file(path, "1\n");
    snprintf(path, sizeof(path), "%s/type", bluetooth_directory);
    write_file(path, "bluetooth\n");
    snprintf(path, sizeof(path), "%s/state", bluetooth_directory);
    write_file(path, "0\n");

    char resolved[PATH_MAX];
    assert(ap80_find_rfkill_state(root, "bluetooth", resolved,
                                  sizeof(resolved)) == 0);
    snprintf(path, sizeof(path), "%s/state", bluetooth_directory);
    assert(strcmp(resolved, path) == 0);
    assert(ap80_find_rfkill_state(root, "wwan", resolved,
                                  sizeof(resolved)) == -ENOENT);

    char too_short[4];
    assert(ap80_find_rfkill_state(root, "bluetooth", too_short,
                                  sizeof(too_short)) == -ENAMETOOLONG);

    snprintf(path, sizeof(path), "%s/type", wifi_directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/state", wifi_directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/type", bluetooth_directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/state", bluetooth_directory);
    assert(unlink(path) == 0);
    assert(rmdir(wifi_directory) == 0);
    assert(rmdir(bluetooth_directory) == 0);
    assert(rmdir(root) == 0);
}

static void test_bounded_process_runner(void)
{
    const char *const echo_arguments[] = {
        "/bin/echo", "radio state", NULL
    };
    char output[64];
    assert(ap80_run_argv("/bin/echo", echo_arguments, 1000, output,
                         sizeof(output)) == 0);
    assert(strcmp(output, "radio state\n") == 0);

    char truncated[5];
    assert(ap80_run_argv("/bin/echo", echo_arguments, 1000, truncated,
                         sizeof(truncated)) == 0);
    assert(strcmp(truncated, "radi") == 0);

    const char *const true_arguments[] = {"/bin/true", NULL};
    const char *const false_arguments[] = {"/bin/false", NULL};
    const char *const sleep_arguments[] = {"/bin/sleep", "1", NULL};
    assert(ap80_run_argv("/bin/true", true_arguments, 1000, NULL, 0) == 0);
    assert(ap80_run_argv("/bin/false", false_arguments, 1000, NULL, 0) ==
           -EIO);
    assert(ap80_run_argv("/bin/sleep", sleep_arguments, 30, NULL, 0) ==
           -ETIMEDOUT);
    assert(ap80_run_argv("/bin/true", true_arguments, 0, NULL, 0) ==
           -EINVAL);
}

static void test_service_state_matching(void)
{
    struct ap80_service_status status = {0};

    assert(ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, false));
    assert(ap80_services_state_matches(
               &status, AP80_SERVICE_BLUETOOTH, false));

    status.wifi_enabled = true;
    assert(!ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, true));
    status.wpa_supplicant = true;
    assert(!ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, true));
    status.dhcp_client = true;
    assert(ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, true));

    status.wifi_enabled = false;
    assert(!ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, false));
    status.wpa_supplicant = false;
    assert(!ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, false));
    status.dhcp_client = false;
    assert(ap80_services_state_matches(
               &status, AP80_SERVICE_WIFI, false));

    status.bluetooth_enabled = true;
    assert(!ap80_services_state_matches(
               &status, AP80_SERVICE_BLUETOOTH, true));
    status.bluetoothd = true;
    assert(!ap80_services_state_matches(
               &status, AP80_SERVICE_BLUETOOTH, true));
    status.bluealsa = true;
    assert(ap80_services_state_matches(
               &status, AP80_SERVICE_BLUETOOTH, true));

    status.bluetooth_enabled = false;
    assert(ap80_services_state_matches(
               &status, AP80_SERVICE_BLUETOOTH, false));
    assert(!ap80_services_state_matches(&status, 0, false));
    assert(!ap80_services_state_matches(
               NULL, AP80_SERVICE_WIFI, false));
}

int main(void)
{
    struct ap80_service_status status;

    test_rfkill_lookup();
    test_bounded_process_runner();
    test_service_state_matching();

    ap80_services_get_status(&status);
    assert(ap80_services_claimed() == 0);
    assert(ap80_services_claim(0) == -EINVAL);
    assert(ap80_services_claim(AP80_SERVICE_ALL + 1) == -EINVAL);
    assert(ap80_services_set_enabled(AP80_SERVICE_WIFI, true) == -EPERM);

    assert(ap80_services_claim(AP80_SERVICE_WIFI) == 0);
    assert(ap80_services_claimed() == AP80_SERVICE_WIFI);
    assert(ap80_services_claim(AP80_SERVICE_WIFI) == -EALREADY);

    /* The engineering build must not be able to power an unvalidated radio. */
    assert(ap80_services_set_enabled(AP80_SERVICE_WIFI, true) ==
           -EOPNOTSUPP);
    assert(ap80_services_set_enabled(AP80_SERVICE_WIFI, false) ==
           -EOPNOTSUPP);
    assert(ap80_services_set_enabled(AP80_SERVICE_BLUETOOTH, true) == -EPERM);

    puts("AP80 service-boundary tests passed");
    return 0;
}
