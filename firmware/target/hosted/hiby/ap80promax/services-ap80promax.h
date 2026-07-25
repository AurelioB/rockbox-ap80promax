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

enum ap80_service
{
    AP80_SERVICE_WIFI = 1 << 0,
    AP80_SERVICE_BLUETOOTH = 1 << 1,
    AP80_SERVICE_ALL = AP80_SERVICE_WIFI | AP80_SERVICE_BLUETOOTH,
};

struct ap80_service_status
{
    bool wifi_interface;
    bool wpa_supplicant;
    bool dhcp_client;
    bool bluetoothd;
    bool bluealsa;
};

void ap80_services_get_status(struct ap80_service_status *status);
int ap80_services_claim(unsigned int services);
int ap80_services_set_enabled(enum ap80_service service, bool enabled);
int ap80_services_prepare_suspend(void);
int ap80_services_resume(void);
int ap80_services_handoff(void);
unsigned int ap80_services_claimed(void);

#endif
