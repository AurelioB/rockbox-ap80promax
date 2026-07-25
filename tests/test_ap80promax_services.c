#include <assert.h>
#include <errno.h>
#include <stdio.h>

#include "services-ap80promax.h"

int main(void)
{
    struct ap80_service_status status;

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
