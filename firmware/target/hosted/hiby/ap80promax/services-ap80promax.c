/***************************************************************************
 * AP80 Pro Max vendor-service ownership boundary.
 *
 * The stock image owns Wi-Fi through wifi_up.sh/wifi_down.sh and Bluetooth
 * through S80_bt_init. Keep those contracts in one target module so future
 * UI and protocol consumers never manipulate daemons, routes, or rfkill
 * independently.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#include "services-ap80promax.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define WIFI_INTERFACE "/sys/class/net/wlan0"
#define WIFI_FLAGS "/sys/class/net/wlan0/flags"
#define WIFI_UP_SCRIPT "/usr/bin/wifi_up.sh"
#define WIFI_DOWN_SCRIPT "/usr/bin/wifi_down.sh"
#define RFKILL_ROOT "/sys/class/rfkill"
#define BT_RESUME_SCRIPT "/usr/bin/bt_resume"
#define BT_SUSPEND_SCRIPT "/usr/bin/bt_suspend"

#define AP80_IFF_UP 0x1
#define WIFI_SCRIPT_TIMEOUT_MS 20000
#define BT_SCRIPT_TIMEOUT_MS 20000

static unsigned int claimed_services;
static unsigned int initially_enabled;
static unsigned int resume_services;
static pthread_mutex_t services_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool process_running(const char *name)
{
    DIR *proc = opendir("/proc");
    if (!proc)
        return false;

    bool found = false;
    struct dirent *entry;
    while (!found && (entry = readdir(proc)) != NULL)
    {
        char *end;
        long pid = strtol(entry->d_name, &end, 10);
        if (pid <= 0 || *end != '\0')
            continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;

        char command[32];
        ssize_t size = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (size <= 0)
            continue;

        command[size] = '\0';
        command[strcspn(command, "\r\n")] = '\0';
        found = strcmp(command, name) == 0;
    }

    closedir(proc);
    return found;
}

static bool read_number(const char *path, unsigned int *value)
{
    char buffer[32];
    char *end;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;

    ssize_t size = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (size <= 0)
        return false;

    buffer[size] = '\0';
    unsigned long parsed = strtoul(buffer, &end, 0);
    if (end == buffer)
        return false;

    *value = (unsigned int)parsed;
    return true;
}

static int read_text(const char *path, char *buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size < 2)
        return -EINVAL;

    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -errno;

    ssize_t size;
    do
        size = read(fd, buffer, buffer_size - 1);
    while (size < 0 && errno == EINTR);

    int saved_errno = errno;
    close(fd);
    if (size < 0)
        return -saved_errno;

    buffer[size] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return 0;
}

int ap80_find_rfkill_state(const char *root, const char *type,
                           char *state_path, size_t state_path_size)
{
    if (!root || !type || !state_path || state_path_size == 0)
        return -EINVAL;

    state_path[0] = '\0';
    DIR *directory = opendir(root);
    if (!directory)
        return -errno;

    int result = -ENOENT;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL)
    {
        if (strncmp(entry->d_name, "rfkill", 6) != 0 ||
            entry->d_name[6] == '\0')
            continue;

        char *end;
        (void)strtoul(entry->d_name + 6, &end, 10);
        if (*end != '\0')
            continue;

        char type_path[PATH_MAX];
        int length = snprintf(type_path, sizeof(type_path), "%s/%s/type",
                              root, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(type_path))
            continue;

        char found_type[64];
        if (read_text(type_path, found_type, sizeof(found_type)) != 0 ||
            strcmp(found_type, type) != 0)
            continue;

        length = snprintf(state_path, state_path_size, "%s/%s/state",
                          root, entry->d_name);
        if (length < 0 || (size_t)length >= state_path_size)
        {
            state_path[0] = '\0';
            result = -ENAMETOOLONG;
            break;
        }

        if (access(state_path, F_OK) == 0)
            result = 0;
        else
        {
            state_path[0] = '\0';
            result = -errno;
        }
        break;
    }

    closedir(directory);
    return result;
}

void ap80_services_get_status(struct ap80_service_status *status)
{
    if (!status)
        return;

    memset(status, 0, sizeof(*status));
    unsigned int value;
    char bluetooth_state[PATH_MAX];

    status->wifi_interface = access(WIFI_INTERFACE, F_OK) == 0;
    status->wifi_enabled = read_number(WIFI_FLAGS, &value) &&
                           (value & AP80_IFF_UP) != 0;
    status->wpa_supplicant = process_running("wpa_supplicant");
    status->dhcp_client = process_running("udhcpc");
    status->bluetooth_enabled =
        ap80_find_rfkill_state(RFKILL_ROOT, "bluetooth", bluetooth_state,
                               sizeof(bluetooth_state)) == 0 &&
        read_number(bluetooth_state, &value) &&
                                value != 0;
    status->bluetoothd = process_running("bluetoothd");
    status->bluealsa = process_running("bluealsa");
}

bool ap80_services_state_matches(const struct ap80_service_status *status,
                                 enum ap80_service service, bool enabled)
{
    if (!status)
        return false;

    if (service == AP80_SERVICE_WIFI)
    {
        if (enabled)
        {
            return status->wifi_enabled &&
                   status->wpa_supplicant &&
                   status->dhcp_client;
        }

        return !status->wifi_enabled &&
               !status->wpa_supplicant &&
               !status->dhcp_client;
    }

    if (service == AP80_SERVICE_BLUETOOTH)
    {
        if (enabled)
        {
            return status->bluetooth_enabled &&
                   status->bluetoothd &&
                   status->bluealsa;
        }

        /*
         * The stock bt_suspend contract deliberately keeps bluetoothd and
         * BlueALSA resident for a fast resume. Radio power and its firmware
         * loader are stopped, which is the vendor definition of Bluetooth
         * being off.
         */
        return !status->bluetooth_enabled;
    }

    return false;
}

static unsigned int enabled_services(void)
{
    struct ap80_service_status status;
    ap80_services_get_status(&status);

    unsigned int enabled = 0;
    if (status.wifi_enabled)
        enabled |= AP80_SERVICE_WIFI;
    if (status.bluetooth_enabled)
        enabled |= AP80_SERVICE_BLUETOOTH;
    return enabled;
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000u +
           (uint64_t)now.tv_nsec / 1000000u;
}

static void terminate_child(pid_t pid)
{
    /* The child creates a process group so helpers it launched are bounded. */
    if (kill(-pid, SIGTERM) != 0)
        (void)kill(pid, SIGTERM);

    uint64_t deadline = monotonic_ms() + 100;
    int status;
    while (waitpid(pid, &status, WNOHANG) == 0 &&
           monotonic_ms() < deadline)
        (void)poll(NULL, 0, 10);

    if (waitpid(pid, &status, WNOHANG) == 0)
    {
        if (kill(-pid, SIGKILL) != 0)
            (void)kill(pid, SIGKILL);
    }

    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
}

static void drain_output(int fd, char *output, size_t output_size,
                         size_t *output_length)
{
    char buffer[256];
    ssize_t size;
    while ((size = read(fd, buffer, sizeof(buffer))) > 0)
    {
        if (*output_length + 1 < output_size)
        {
            size_t available = output_size - *output_length - 1;
            size_t copied = (size_t)size < available ?
                            (size_t)size : available;
            memcpy(output + *output_length, buffer, copied);
            *output_length += copied;
            output[*output_length] = '\0';
        }
    }
}

int ap80_run_argv(const char *path, const char *const argv[],
                  unsigned int timeout_ms, char *output,
                  size_t output_size)
{
    if (!path || !argv || !argv[0] || timeout_ms == 0)
        return -EINVAL;

    bool capture = output != NULL && output_size != 0;
    if (capture)
        output[0] = '\0';

    int pipe_fd[2] = {-1, -1};
    if (capture && pipe(pipe_fd) != 0)
        return -errno;

    pid_t pid = fork();
    if (pid < 0)
    {
        int result = -errno;
        if (capture)
        {
            close(pipe_fd[0]);
            close(pipe_fd[1]);
        }
        return result;
    }

    if (pid == 0)
    {
        (void)setpgid(0, 0);

        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd < 0)
            _exit(127);
        if (dup2(null_fd, STDIN_FILENO) < 0 ||
            dup2(null_fd, STDERR_FILENO) < 0)
            _exit(127);

        if (capture)
        {
            close(pipe_fd[0]);
            if (dup2(pipe_fd[1], STDOUT_FILENO) < 0)
                _exit(127);
            close(pipe_fd[1]);
        }
        else
        {
            if (dup2(null_fd, STDOUT_FILENO) < 0)
                _exit(127);
        }

        if (null_fd > STDERR_FILENO)
            close(null_fd);
        execv(path, (char *const *)argv);
        _exit(127);
    }

    (void)setpgid(pid, pid);
    if (capture)
    {
        close(pipe_fd[1]);
        int flags = fcntl(pipe_fd[0], F_GETFL, 0);
        if (flags >= 0)
            (void)fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);
    }

    int status = 0;
    int result = 0;
    size_t output_length = 0;
    uint64_t deadline = monotonic_ms() + timeout_ms;
    for (;;)
    {
        if (capture)
            drain_output(pipe_fd[0], output, output_size, &output_length);

        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid)
            break;
        if (waited < 0 && errno != EINTR)
        {
            result = -errno;
            terminate_child(pid);
            break;
        }

        uint64_t now = monotonic_ms();
        if (now >= deadline)
        {
            terminate_child(pid);
            result = -ETIMEDOUT;
            break;
        }

        int wait_ms = (int)(deadline - now);
        if (wait_ms > 20)
            wait_ms = 20;
        if (capture)
        {
            struct pollfd poll_fd = {
                .fd = pipe_fd[0],
                .events = POLLIN,
            };
            (void)poll(&poll_fd, 1, wait_ms);
        }
        else
            (void)poll(NULL, 0, wait_ms);
    }

    if (capture)
    {
        drain_output(pipe_fd[0], output, output_size, &output_length);
        close(pipe_fd[0]);
    }

    if (result != 0)
        return result;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -EIO;
    return 0;
}

#ifdef AP80_ENABLE_WIRELESS_TRANSITIONS
static int run_script(const char *script, unsigned int timeout_ms)
{
    const char *const argv[] = {script, NULL};
    return ap80_run_argv(script, argv, timeout_ms, NULL, 0);
}
#endif

int ap80_services_claim(unsigned int services)
{
    if (services == 0 || (services & ~AP80_SERVICE_ALL) != 0)
        return -EINVAL;

    pthread_mutex_lock(&services_mutex);
    if (claimed_services & services)
    {
        pthread_mutex_unlock(&services_mutex);
        return -EALREADY;
    }

    if (claimed_services == 0)
        initially_enabled = enabled_services();
    claimed_services |= services;
    pthread_mutex_unlock(&services_mutex);
    return 0;
}

static int set_enabled_locked(enum ap80_service service, bool enabled)
{
    if ((service != AP80_SERVICE_WIFI &&
         service != AP80_SERVICE_BLUETOOTH) ||
        !(claimed_services & service))
        return -EPERM;

#ifndef AP80_ENABLE_WIRELESS_TRANSITIONS
    (void)enabled;
    return -EOPNOTSUPP;
#else
    struct ap80_service_status status;
    ap80_services_get_status(&status);
    if (ap80_services_state_matches(&status, service, enabled))
        return 0;

    if (service == AP80_SERVICE_WIFI)
        return run_script(enabled ? WIFI_UP_SCRIPT : WIFI_DOWN_SCRIPT,
                          WIFI_SCRIPT_TIMEOUT_MS);

    return run_script(enabled ? BT_RESUME_SCRIPT : BT_SUSPEND_SCRIPT,
                      BT_SCRIPT_TIMEOUT_MS);
#endif
}

int ap80_services_set_enabled(enum ap80_service service, bool enabled)
{
    pthread_mutex_lock(&services_mutex);
    int result = set_enabled_locked(service, enabled);
    pthread_mutex_unlock(&services_mutex);
    return result;
}

int ap80_services_prepare_suspend(void)
{
    pthread_mutex_lock(&services_mutex);
    resume_services = enabled_services() & claimed_services;
    int result = 0;

    if (resume_services & AP80_SERVICE_WIFI)
        result = set_enabled_locked(AP80_SERVICE_WIFI, false);
    if (result == 0 && (resume_services & AP80_SERVICE_BLUETOOTH))
        result = set_enabled_locked(AP80_SERVICE_BLUETOOTH, false);
    pthread_mutex_unlock(&services_mutex);
    return result;
}

int ap80_services_resume(void)
{
    pthread_mutex_lock(&services_mutex);
    int result = 0;

    if (resume_services & AP80_SERVICE_WIFI)
        result = set_enabled_locked(AP80_SERVICE_WIFI, true);
    if (result == 0 && (resume_services & AP80_SERVICE_BLUETOOTH))
        result = set_enabled_locked(AP80_SERVICE_BLUETOOTH, true);
    if (result == 0)
        resume_services = 0;
    pthread_mutex_unlock(&services_mutex);
    return result;
}

int ap80_services_handoff(void)
{
    pthread_mutex_lock(&services_mutex);
    unsigned int current = enabled_services();
    int result = 0;

    for (enum ap80_service service = AP80_SERVICE_WIFI;
         service <= AP80_SERVICE_BLUETOOTH; service <<= 1)
    {
        if (!(claimed_services & service))
            continue;

        bool wanted = (initially_enabled & service) != 0;
        if (((current & service) != 0) != wanted)
        {
            result = set_enabled_locked(service, wanted);
            if (result != 0)
            {
                pthread_mutex_unlock(&services_mutex);
                return result;
            }
        }
    }

    claimed_services = 0;
    initially_enabled = 0;
    resume_services = 0;
    pthread_mutex_unlock(&services_mutex);
    return 0;
}

unsigned int ap80_services_claimed(void)
{
    pthread_mutex_lock(&services_mutex);
    unsigned int services = claimed_services;
    pthread_mutex_unlock(&services_mutex);
    return services;
}
