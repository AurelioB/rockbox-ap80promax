/***************************************************************************
 * AP80 Pro Max vendor-service ownership boundary.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ***************************************************************************/

#ifndef SERVICES_AP80PROMAX_H
#define SERVICES_AP80PROMAX_H

#include <stdbool.h>
#include <stddef.h>

enum ap80_service
{
    AP80_SERVICE_WIFI = 1 << 0,
    AP80_SERVICE_BLUETOOTH = 1 << 1,
    AP80_SERVICE_ALL = AP80_SERVICE_WIFI | AP80_SERVICE_BLUETOOTH,
};

struct ap80_service_status
{
    bool wifi_interface;
    bool wifi_enabled;
    bool wpa_supplicant;
    bool dhcp_client;
    bool bluetooth_enabled;
    bool bluetoothd;
    bool bluealsa;
};

void ap80_services_get_status(struct ap80_service_status *status);
bool ap80_services_state_matches(const struct ap80_service_status *status,
                                 enum ap80_service service, bool enabled);
int ap80_services_claim(unsigned int services);
int ap80_services_set_enabled(enum ap80_service service, bool enabled);
int ap80_services_prepare_suspend(void);
int ap80_services_resume(void);
int ap80_services_handoff(void);
unsigned int ap80_services_claimed(void);

/*
 * Run a program without involving a shell. argv[0] must be present and the
 * timeout must be non-zero. stdout is copied into the optional NUL-terminated
 * output buffer (and safely truncated); stderr is discarded. The function
 * drains all child output while it waits, returns zero only for exit status
 * zero, and returns a negative errno-style value otherwise.
 */
int ap80_run_argv(const char *path, const char *const argv[],
                  unsigned int timeout_ms, char *output,
                  size_t output_size);

/*
 * Resolve the rfkill state node by reading each entry's type instead of
 * relying on the kernel-assigned rfkill index.
 */
int ap80_find_rfkill_state(const char *root, const char *type,
                           char *state_path, size_t state_path_size);

#endif
