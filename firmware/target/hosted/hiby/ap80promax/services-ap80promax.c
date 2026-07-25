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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define WIFI_INTERFACE "/sys/class/net/wlan0"
#define WIFI_UP_SCRIPT "/usr/bin/wifi_up.sh"
#define WIFI_DOWN_SCRIPT "/usr/bin/wifi_down.sh"
#define BT_INIT_SCRIPT "/etc/init.d/S80_bt_init"

static unsigned int claimed_services;
static unsigned int initially_enabled;
static unsigned int resume_services;

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

void ap80_services_get_status(struct ap80_service_status *status)
{
    status->wifi_interface = access(WIFI_INTERFACE, F_OK) == 0;
    status->wpa_supplicant = process_running("wpa_supplicant");
    status->dhcp_client = process_running("udhcpc");
    status->bluetoothd = process_running("bluetoothd");
    status->bluealsa = process_running("bluealsa");
}

static unsigned int enabled_services(void)
{
    struct ap80_service_status status;
    ap80_services_get_status(&status);

    unsigned int enabled = 0;
    if (status.wpa_supplicant || status.dhcp_client)
        enabled |= AP80_SERVICE_WIFI;
    if (status.bluetoothd || status.bluealsa)
        enabled |= AP80_SERVICE_BLUETOOTH;
    return enabled;
}

#ifdef AP80_ENABLE_WIRELESS_TRANSITIONS
static int run_script(const char *script, const char *argument)
{
    pid_t pid = fork();
    if (pid < 0)
        return -errno;

    if (pid == 0)
    {
        if (argument)
            execl(script, script, argument, (char *)NULL);
        else
            execl(script, script, (char *)NULL);
        _exit(127);
    }

    int status;
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
            return -errno;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -EIO;
    return 0;
}
#endif

int ap80_services_claim(unsigned int services)
{
    if (services == 0 || (services & ~AP80_SERVICE_ALL) != 0)
        return -EINVAL;
    if (claimed_services & services)
        return -EALREADY;

    if (claimed_services == 0)
        initially_enabled = enabled_services();
    claimed_services |= services;
    return 0;
}

int ap80_services_set_enabled(enum ap80_service service, bool enabled)
{
    if ((service != AP80_SERVICE_WIFI &&
         service != AP80_SERVICE_BLUETOOTH) ||
        !(claimed_services & service))
        return -EPERM;

#ifndef AP80_ENABLE_WIRELESS_TRANSITIONS
    (void)enabled;
    return -EOPNOTSUPP;
#else
    if (service == AP80_SERVICE_WIFI)
        return run_script(enabled ? WIFI_UP_SCRIPT : WIFI_DOWN_SCRIPT, NULL);
    return run_script(BT_INIT_SCRIPT, enabled ? "start" : "stop");
#endif
}

int ap80_services_prepare_suspend(void)
{
    resume_services = enabled_services() & claimed_services;
    int result = 0;

    if (resume_services & AP80_SERVICE_WIFI)
        result = ap80_services_set_enabled(AP80_SERVICE_WIFI, false);
    if (result == 0 && (resume_services & AP80_SERVICE_BLUETOOTH))
        result = ap80_services_set_enabled(AP80_SERVICE_BLUETOOTH, false);
    return result;
}

int ap80_services_resume(void)
{
    int result = 0;

    if (resume_services & AP80_SERVICE_WIFI)
        result = ap80_services_set_enabled(AP80_SERVICE_WIFI, true);
    if (result == 0 && (resume_services & AP80_SERVICE_BLUETOOTH))
        result = ap80_services_set_enabled(AP80_SERVICE_BLUETOOTH, true);
    if (result == 0)
        resume_services = 0;
    return result;
}

int ap80_services_handoff(void)
{
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
            result = ap80_services_set_enabled(service, wanted);
            if (result != 0)
                return result;
        }
    }

    claimed_services = 0;
    initially_enabled = 0;
    resume_services = 0;
    return 0;
}

unsigned int ap80_services_claimed(void)
{
    return claimed_services;
}
