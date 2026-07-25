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
| Bluetooth lifecycle | `/etc/init.d/S80_bt_init start/stop` |
| Bluetooth control | BlueZ `bluetoothd` |
| Bluetooth audio | BlueALSA `bluealsa` |

The stock Wi-Fi scripts also own rfkill, interface state, DHCP, and the saved
supplicant configuration. The Bluetooth init script selects firmware from the
SDIO chip-vendor ID, downloads it over `/dev/ttyS0`, starts BlueZ and
BlueALSA, and installs the HiByLink service record. Reimplementing fragments
of either sequence inside a consumer would leave state behind during
suspend, reboot, or a return to HiByOS.

## Rockbox API

`services-ap80promax.c` provides:

- read-only process/interface status;
- explicit Wi-Fi and Bluetooth claims;
- one transition entry point per service;
- paired suspend/resume handling; and
- restoration of the state observed before a HiByOS handoff.

Transitions call only the recovered stock scripts and are compiled behind
`AP80_ENABLE_WIRELESS_TRANSITIONS`. That define is intentionally absent, so
transition requests currently return `-EOPNOTSUPP` and cannot power either
radio. Read-only status and ownership logic remain available for development.

Enabling the define requires device tests covering repeated start/stop,
failed association, DHCP renewal, suspend/resume, process failure, reboot,
and return to the original player. Scan/connect UI and every network or
Bluetooth consumer remain separate work after that gate.
