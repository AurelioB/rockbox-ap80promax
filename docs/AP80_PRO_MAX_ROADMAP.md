# AP80 Pro Max feature roadmap

This roadmap separates source-complete work from the checks which necessarily
require a physical player.

## Release and hosted-platform gates

- [x] Build the application, bootloader, data ZIP, and engineering updater
      from one source state.
- [x] Verify ELF architecture, ZIP integrity, seven AP80 OneBit presets, and
      SHA-256 manifests with `tools/ap80promax/verify.sh`.
- [x] Add a no-reflash USB update and rollback workflow using the boot
      menu's ADB gadget, staged same-filesystem directory swaps, target
      validation, and transfer checksums.
- [x] Read USB cable presence from the vendor `usb/online` power-supply node
      without changing the indicator LED from a polling function.
- [ ] Validate USB update, interrupted transfer, rollback, and repeated
      updates on the physical player.
- [ ] Exercise charge-only, ADB, USB-DAC, storage, repeated hotplug, and host
      suspend on the device.

The vendor update and generated artifacts are deliberately not part of the
source repository.

## Power management

- [x] Use framebuffer blanking to power down the panel and touch controller
      after the backlight expires while preserving playback.
- [x] Match the stock 60-percent default brightness instead of maximum output.
- [x] Wait for the physical power knob to be released before Linux poweroff,
      avoiding overlap between Rockbox shutdown and the PMIC long-press path.
- [x] Recover HiByOS's ordered suspend/resume path, radio service boundaries,
      kernel driver hooks, and actual wake-source GPIOs.
- [ ] Measure stopped, paused, PCM, DoP, native DSD, and powered-off current on
      the physical player.
- [ ] Enable idle suspend-to-RAM only after repeated resume tests cover ALSA,
      routing, storage, touch, wake buttons, USB, and wireless restoration.

## Audio

### Digital microphone recording

- [x] Recover the stock ALSA contract: `hw:0,0`, interleaved stereo
      `S16_LE`, 16 kHz.
- [x] Enable only the proven microphone source and sample rate.
- [x] Add recording key mappings and a fixed-gain codec description; the
      stock digital-microphone path exposes no verified gain control.
- [x] Build all recording encoders and the portrait recorder UI.
- [x] Keep capture in the stock `S16_LE` format even though playback uses
      32-bit ALSA slots, and restore the playback endpoint/rate on exit.
- [ ] Validate WAV headers, duration, channel order, levels, and restoration
      of playback on hardware.

### Analog output and line-out

Stock-player disassembly establishes the exact `Output Port Switch` mapping:

| Value | Route |
| ---: | --- |
| 1 | fixed 3.5 mm line-out |
| 2 | 3.5 mm headphone |
| 3 | 4.4 mm balanced headphone |
| 4 | I2S to S/PDIF |
| 5 | no analog route |

- [x] Keep automatic 3.5/4.4 mm jack routing.
- [x] Add a mutually exclusive `Automatic / Line Out / S/PDIF` selector.
      Line-out mutes around route changes, fixes the output at the stock 0 dB
      level, ignores jack rerouting while active, and restores the saved
      headphone volume when disabled.
- [ ] Validate the warning and transition behavior with inexpensive hardware
      before treating line-out as safe.

### S/PDIF

The stock configuration identifies digital ALSA card 0/device 1, supports
44.1 through 192 kHz, and pairs it with output-route value 4. This is not the
TCS1421 interface: that driver controls USB-C source/sink/DRP role.

- [x] Add an atomic hosted-ALSA endpoint switch between analog `hw:0,0` and
      digital `hw:0,1`, coordinated with the asynchronous PCM callback.
- [x] Expose S/PDIF with deterministic mute, route, stop/reopen/restart, and
      rollback behavior. A failed endpoint open restores the visible setting
      and last working route.
- [ ] Validate 44.1 through 192 kHz, live route changes, receiver lock, and
      rollback behavior on physical S/PDIF hardware.

### PCM precision and rates

- [x] Use the AP80 driver's supported `S24_LE` format: 24 valid bits in
      32-bit storage slots.
- [x] Initialize the S16-to-S24 expansion multiplier at unity while retaining
      the ES9219 hardware volume controls.
- [x] Retain the proven Rockbox rate range through 192 kHz.
- [x] Extend Rockbox's global sample-rate enumeration for 352.8/384 kHz. This
      is core work, not an AP80-only target define, and must preserve every
      other target's indices and bit-identical output.

## DSD

- [x] Parse uncompressed stereo DSF and DSDIFF.
- [x] Bypass the PCM DSP/mixer and provide direct DoP/native transport.
- [x] Use DoP at DSD64/128.
- [x] Quarantine the DSD256 native route after real hardware produced static;
      use the in-tree 176.4 kHz PCM fallback until every stock hardware-state
      transition is reproduced.
- [x] Reject DST-compressed DSDIFF and DSD512 without opening the endpoint.
- [x] Add host fixtures for metadata, route choice, byte order, markers,
      seeking, and malformed input.
- [ ] Validate DSD64/128 direct DoP and DSD256 PCM fallback on both physical
      outputs.
- [ ] Add `Automatic / Prefer DoP / Prefer native` only after both transports
      are measured. Unsupported preferences must visibly fall back.
- [x] Add explicit DSD-to-PCM fallback. Do not link the bundled
      `libdsd2pcm.so` until its private ABI and redistribution terms are known;
      an in-tree GPL-compatible decimator is the safe alternative.

## Wireless and services

- [x] Put ownership of `cywdhd`, `wpa_supplicant`, DHCP, routes,
      suspend/resume, and HiByOS handoff behind a small hosted platform API.
- [x] Validate repeated `wifi_up`/`wifi_down` and
      `bt_suspend`/`bt_resume` transitions and enable the guarded platform
      calls. Track interface/rfkill state rather than long-lived daemons.
- [ ] Replace or repair the higher-level AP80 Bluetooth control plane. The R1
      proof of concept assumes a persistent `/var/run/sys_server`, but the
      AP80 service became stale/one-shot during hardware tests; its
      `bt_enable` recovery also terminated BlueZ.
- [ ] Add scan/connect/status UI independently of consumers.
- [ ] Add file transfer, HiByLink, internet radio, and AirPlay as separate
      consumers.
- [ ] Treat Tidal/Qobuz as separate protocol, credential, and licensing work.
- [ ] Rebase the BlueALSA proof of concept only after wired audio is stable.

## UI and upstream integration

- [x] Provide physical 360 x 640 Cabbie and original-file OneBit variants
      using the same logical coordinates as the panel and touch controller.
- [x] Add touch regions while retaining physical-button behavior.
- [x] Match stock-sized header and transport touch targets and use DPI-aware
      rows without altering OneBit artwork or visible layout.
- [x] Make plugin layouts bounds-safe at portrait resolution.
- [x] Add a simulator target for menus, themes, touch, metadata, and plugins.
- [x] Add the engineering installation, controls, recovery, and validation
      guide in `docs/AP80_PRO_MAX_INSTALL.md`.
- [x] Add CI for the simulator, target build, DSD fixtures, theme validation,
      and release verifier.
- [ ] Add Rockbox Utility support only after updater/recovery validation.

The target remains an engineering port until the physical validation items
above are complete.
