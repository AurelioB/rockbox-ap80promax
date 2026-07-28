# AP80 Pro Max connectivity

This branch adds an AP80 Pro Max-specific connectivity layer to Rockbox. It
uses the radio, BlueZ, BlueALSA, `wpa_supplicant`, and DHCP components already
shipped in the Hidizs firmware; none of those vendor binaries are copied into
the Rockbox ZIP.

## Upstream review audit

Rockbox development uses Gerrit rather than GitHub pull requests. The open
changes relevant to this work were reviewed before implementation:

| Change | Decision |
| --- | --- |
| [7695](https://gerrit.rockbox.org/r/c/rockbox/+/7695), point-mode touchscreen keyboard | Adapt with an explicit target capability and secure-entry mode |
| [3044](https://gerrit.rockbox.org/r/c/rockbox/+/3044), Bluetooth menu WIP | Do not import; reuse only menu terminology |
| [7649](https://gerrit.rockbox.org/r/c/rockbox/+/7649), HiBy Bluetooth target interface | Reuse the target/backend separation |
| [7650](https://gerrit.rockbox.org/r/c/rockbox/+/7650), HiBy Bluetooth and ALSA routing | Rewrite the BlueALSA endpoint ideas for the AP80 backend |
| [7651](https://gerrit.rockbox.org/r/c/rockbox/+/7651), Bluetooth menu/audio integration | Do not import its blocking UI or playback restart |
| [7688](https://gerrit.rockbox.org/r/c/rockbox/+/7688)/[7689](https://gerrit.rockbox.org/r/c/rockbox/+/7689), PCM sink volume metadata | Not a routing dependency; defer until Bluetooth is a separate PCM sink |
| [5393](https://gerrit.rockbox.org/r/c/rockbox/+/5393), kinetic scrolling series | Do not import; it overlaps the AP80 touch-list implementation |

There is no open Rockbox Wi-Fi or X1600 radio change to reuse. The Bluetooth
series is still work in progress, has no positive review or test vote, and its
only reported receiver test reached BlueZ connection without a working audio
route. It also depends on `/var/run/sys_server`, which was unreliable on the
AP80 during hardware testing.

## Architecture

The implementation is split into three layers:

1. `services-ap80promax` remains the only owner of radio power transitions.
   It invokes the validated `wifi_up.sh`/`wifi_down.sh` and
   `bt_resume`/`bt_suspend` pairs with bounded waits, records inherited state,
   and discovers rfkill devices by type instead of index. Its suspend, resume,
   and handoff functions are reserved lifecycle primitives: Rockbox currently
   has neither AP80 suspend-to-RAM nor a process exit into HiByOS, so they are
   intentionally not called from production paths.
2. AP80 Wi-Fi and Bluetooth controllers run blocking vendor operations on
   one background worker each. UI calls copy their arguments into a bounded
   mailbox and return immediately. Immutable snapshots, protected by a mutex
   and generation counter, are the only state exposed to the UI.
3. The Rockbox Connectivity menu displays those snapshots, submits explicit
   operations, and redraws only when a generation changes. Credentials never
   enter a shell command.

Wi-Fi talks directly to the `wpa_supplicant` control socket at
`/var/run/wpa_supplicant/wlan0`. Bluetooth uses bounded `bluetoothctl` and
`bluealsa-cli` subprocesses because the installed Rockbox sysroot does not
currently provide BlueZ D-Bus development headers. No operation uses
`system()`, `popen()`, `sys_server`, or `bt_enable`.

## Initial supported scope

Wi-Fi:

- power on/off;
- scan and display nearby networks;
- connect to open, WPA-PSK, and WPA2-PSK networks;
- hidden-network credential entry;
- save a network only after association succeeds;
- set the active `wpa_supplicant` configuration to mode `0600` before and
  after persisting credentials;
- disconnect and forget saved networks; and
- report SSID, signal, address, operation progress, and errors.

Bluetooth:

- power on/off;
- scan and list BlueZ devices;
- explicit pair, trust, connect, and disconnect;
- wait for an A2DP BlueALSA PCM before changing the audio route;
- show separate BlueZ-connected and audio-ready state plus the active codec;
- forward volume changes to the active receiver; and
- restore the prior analog or S/PDIF endpoint if routing fails or the receiver
  disconnects.

The touchscreen keyboard uses the open point-mode keyboard work as its base.
The feature is opt-in so smaller Rockbox touch targets retain the legacy
keyboard. Password entry is masked, disables spelling/voice feedback, and
wipes its temporary buffer after the request has copied it.

## Deliberate first-version limits

- WPA3, WPA-Enterprise/EAP, WPS, captive-portal login, and IPv6 configuration
  are not implemented.
- Bluetooth is A2DP output only. HiByLink, phone control, Bluetooth input, and
  file transfer are separate features.
- Codec selection is left to BlueALSA for the first hardware-validation pass.
  The UI reports the negotiated codec instead of claiming an unverified one.
- Direct DSD cannot be sent over A2DP. Selecting Bluetooth while a direct DSD
  stream owns ALSA must fail without disturbing the local route; a future
  audio-engine policy may restart that track through PCM conversion.
- Pairing-key persistence under `/var/lib/bluetooth`, rfkill enumeration, the
  shipped `bluetoothctl` command syntax, BlueALSA absolute volume, and receiver
  reconnection all require validation on each AP80 radio-module variant.

## Hardware validation

Before merging this feature into the normal device build:

1. Cycle each radio on/off at least ten times and suspend/resume with Wi-Fi,
   Bluetooth, both, and neither enabled.
2. Join open and WPA2 networks, reject a wrong password, test punctuation and
   a 63-character passphrase, reconnect after reboot, then forget the network.
3. Pair at least an SBC-only and a higher-codec receiver. Verify connect,
   playback, volume, pause, remote disconnect fallback, reconnect, and reboot.
4. Exercise FLAC and high-rate PCM. While direct DSD is playing, verify that a
   Bluetooth route request is rejected cleanly and local playback remains
   recoverable.
5. Confirm that leaving Rockbox for HiByOS restores the radio state inherited
   when a future explicit handoff path is implemented. Do not treat reboot or
   poweroff as that handoff.
