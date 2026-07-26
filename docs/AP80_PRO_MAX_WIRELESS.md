# AP80 Pro Max wireless service boundary

Wireless consumers are not enabled in this engineering build. The source has
a target-specific ownership boundary so future Wi-Fi, HiByLink, network
playback, and Bluetooth work does not start and stop shared vendor services
independently.

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
- paired suspend/resume handling; and
- restoration of the state observed before a HiByOS handoff.

Transitions call only the recovered stock scripts and are compiled behind
`AP80_ENABLE_WIRELESS_TRANSITIONS`. The AP80 target enables that gate after
three repeated Wi-Fi cycles and three repeated Bluetooth suspend/resume
cycles succeeded on hardware. Bluetooth enabled-state follows rfkill, and
Wi-Fi enabled-state follows the interface `IFF_UP` flag; the BlueZ and
BlueALSA daemons intentionally remain alive while the radio is suspended.

The R1 Bluetooth proof of concept cannot yet be enabled unchanged. It assumes
that `/var/run/sys_server` remains a persistent command socket and uses the
stock `bt_enable` recovery path. On the AP80, the socket became stale or
one-shot during testing, while `bt_enable` could terminate BlueZ when the
adapter was unavailable. Scan/connect UI and Bluetooth audio routing remain
blocked on a reliable AP80 control plane; the validated radio lifecycle
below it is now available to future consumers.
