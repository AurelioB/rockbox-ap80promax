# AP80 Pro Max wireless service boundary

The AP80 connectivity feature uses this target-specific ownership boundary so
Wi-Fi and Bluetooth controllers do not start and stop shared vendor services
independently. See `AP80_PRO_MAX_CONNECTIVITY.md` for the asynchronous
controllers, Settings UI, supported operations, and hardware-validation plan.

## Recovered stock contracts

The AP80 Pro Max firmware uses:

| Function | Stock contract |
| --- | --- |
| Wi-Fi up | `/usr/bin/wifi_up.sh` |
| Wi-Fi down | `/usr/bin/wifi_down.sh` |
| Association | `wpa_supplicant` on `wlan0` |
| Address and routes | BusyBox `udhcpc` |
| Bluetooth cold initialization | `/etc/init.d/S80_bt_init start` |
| Bluetooth repeatable transition | `/usr/bin/bt_suspend`, `/usr/bin/bt_resume` |
| Bluetooth control | BlueZ `bluetoothd` |
| Bluetooth audio | BlueALSA `bluealsa` |

The stock Wi-Fi scripts also own rfkill, interface state, DHCP, and the saved
supplicant configuration. The Bluetooth init script selects firmware from the
SDIO chip-vendor ID, downloads it over `/dev/ttyS0`, starts BlueZ and
BlueALSA, and installs the HiByLink service record. Its `stop` action is not
a reversible counterpart: it leaves the long-lived daemons running and only
stops part of the UART transport. Repeated device tests establish
`bt_suspend`/`bt_resume` as the working paired transition. Reimplementing
fragments of either sequence inside a consumer would leave state behind
during suspend, reboot, or a return to HiByOS.

## Rockbox API

`services-ap80promax.c` provides:

- read-only process/interface status;
- explicit Wi-Fi and Bluetooth claims;
- one transition entry point per service;
- dynamically resolved rfkill state;
- bounded, argv-only process execution without a shell;
- reserved paired suspend/resume handling; and
- reserved restoration of the state observed before a future HiByOS handoff.

Those lifecycle functions are building blocks, not active hooks. The AP80
Rockbox application currently has no suspend-to-RAM path and no process exit
into HiByOS; poweroff and reboot are not handoffs. Joining a controller and
running both vendor transitions can exceed the existing forced-poweroff
watchdog, so wiring them into shutdown would create a new shutdown race.

Transitions call only the recovered stock scripts and are compiled behind
`AP80_ENABLE_WIRELESS_TRANSITIONS`. The AP80 target enables that gate after
three repeated Wi-Fi cycles and three repeated Bluetooth suspend/resume
cycles succeeded on hardware. An enabled state requires the radio plus its
control/audio daemons, preventing a dead daemon from being mistaken for a
working service. Bluetooth-off follows the stock contract: rfkill and the
firmware loader are stopped while BlueZ and BlueALSA remain resident for a
fast resume.

Saved Wi-Fi credentials remain in the stock
`/usr/data/wpa_supplicant.conf` (or the absolute path selected by
`env_wifi_wpa_supplicant_conf`). Rockbox changes that file to owner-only mode
`0600` both before and after `SAVE_CONFIG`.

The R1 Bluetooth proof of concept is not used unchanged. It assumes that
`/var/run/sys_server` remains a persistent command socket and uses the stock
`bt_enable` recovery path. On the AP80, the socket became stale or one-shot
during testing, while `bt_enable` could terminate BlueZ when the adapter was
unavailable. The AP80 controllers instead use the validated lifecycle here,
the `wpa_supplicant` control socket, and bounded BlueZ/BlueALSA commands.
