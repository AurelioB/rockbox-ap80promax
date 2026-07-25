# Hidizs AP80 Pro Max installation and recovery

The AP80 Pro Max port is an engineering hosted port. Rockbox runs on the
vendor Linux kernel and the installed boot menu can still start the original
HiBy player.

Read this document completely before installing. Keep an unmodified copy of
the exact official firmware version currently installed on the player.

## Safety boundaries

- Do not flash an updater made from a different AP80 model or firmware
  version.
- Do not interrupt a firmware update or begin one with a low battery.
- The generated updater contains proprietary Hidizs firmware and must not be
  committed or redistributed.
- Line Out is fixed at the stock 0 dB level. Never select it with headphones
  connected.
- S/PDIF, recording, USB behavior, DSD, and recovery still require physical
  validation. Treat this build as recoverable development firmware.

## Build

The default developer layout is documented in `tools/ap80promax/README.md`.
With the MIPS toolchain installed:

```sh
./tools/ap80promax/build.sh
./tools/ap80promax/package.sh
```

`build.sh` creates and verifies:

- `dist/rockbox.ap80promax`
- `dist/rockbox-ap80promax.zip`
- `dist/bootloader.ap80promax`
- `dist/SHA256SUMS`

`package.sh` requires a legally obtained `ap80max.upt` matching the installed
firmware. It creates `dist/ap80max_rb.upt` and its checksum without flashing
the player.

## Install

1. Back up the SD card and copy the original, unmodified `ap80max.upt` to a
   safe computer.
2. Extract `rockbox-ap80promax.zip` into the SD-card root. The card must now
   contain `/.rockbox/rockbox.ap80promax`.
3. Copy `ap80max_rb.upt` to the SD-card root and rename that copy to
   `ap80max.upt`.
4. Safely eject the card, insert it into the player, and boot the original
   firmware.
5. Use the original firmware's **System → Firmware Update → TF card
   upgrade** command.
6. After the update and reboot, select **ROCKBOX** in the boot menu and press
   Play.

The first boot-menu choice waits ten seconds. After a choice has booted
successfully, that choice is remembered and starts after a one-second delay.

## Boot-menu controls

| Control | Boot menu |
| --- | --- |
| Previous / Volume Down | Previous choice |
| Next / Volume Up | Next choice |
| Play | Boot selected choice |

The choices are **ROCKBOX**, **TOOLS**, and **HIBY PLAYER**. The Tools menu
provides ADB start/stop, SD remount, restart, shutdown, and the vendor recovery
request.

## Rockbox physical controls

| Context | Control | Action |
| --- | --- | --- |
| Menus | Previous / Next | Move through items |
| Menus | Play | Select |
| Menus | Hold Play | Context menu |
| Menus | Power | Back/cancel |
| Now Playing | Play | Pause/resume |
| Now Playing | Hold Play | Stop |
| Now Playing | Previous / Next | Previous/next track |
| Now Playing | Hold Previous / Next | Seek backward/forward |
| Anywhere | Encoder / Volume keys | Adjust volume or setting |
| Anywhere | Power + Volume Up | Key lock |
| Recorder | Play | Pause/resume recording |
| Recorder | Next | Start a new file |
| Recorder | Power | Leave recorder |

The touchscreen remains available. The AP80 OneBit themes add large invisible
touch regions for the header, playlist/context actions, volume, seek, and the
bottom transport controls.

## Output modes

Open **Settings → System → Output Mode**:

- **Automatic** follows 3.5 mm and 4.4 mm jack detection and uses analog
  `hw:0,0`.
- **Line Out** selects the fixed 3.5 mm route at the stock 0 dB level.
- **S/PDIF** selects the stock I2S-to-S/PDIF route and digital `hw:0,1`.

Rockbox mutes around route changes. If the requested ALSA endpoint cannot be
opened and configured at the current sample rate, it restores the previous
endpoint, route, playback buffer, and visible setting.

## Recover or uninstall

### Original firmware still boots

1. Select **HIBY PLAYER** in the Rockbox boot menu.
2. Put an unmodified official `ap80max.upt`, matching the player, in the
   SD-card root.
3. Run **System → Firmware Update → TF card upgrade**.
4. After the stock update succeeds, remove `/.rockbox` if Rockbox is no
   longer wanted.

### Rockbox does not start

Select **HIBY PLAYER** from the boot menu. A missing or invalid
`rockbox.ap80promax` does not replace the original player executable.

If storage needs attention, choose **TOOLS** and use **Remount SD** or start
ADB. Do not run unknown scripts from the Tools menu.

### Original firmware does not start

Choose **TOOLS → Recovery**. This writes the vendor `recovery` request to
`/proc/jz/reset/reset`. Recovery behavior and the AP80's powered-off hardware
key combination must be validated before they are treated as guaranteed
unbrick procedures.

If the boot menu itself cannot be reached, stop and use only an official
Hidizs recovery procedure for the exact AP80 Pro Max revision. Do not test
update files from related HiBy or Hidizs products.

## First hardware-validation pass

Use inexpensive headphones, a disposable SD card, and a receiver that clearly
reports its input rate. Record the result of each item in
`docs/AP80_PRO_MAX_ROADMAP.md`:

1. Boot Rockbox, HiBy Player, and Tools repeatedly.
2. Verify 3.5 mm and 4.4 mm insertion/removal and saved volume restoration.
3. Verify the Line Out warning and fixed-level route before connecting an
   amplifier.
4. Verify S/PDIF at 44.1, 48, 88.2, 96, 176.4, and 192 kHz, including live
   switching and a forced endpoint-open failure.
5. Record a short WAV and verify format, duration, channel order, levels, and
   playback restoration.
6. Verify DSD64, DSD128, and DSD256 on each supported physical output.
7. Exercise charge-only, ADB, USB DAC, storage, repeated hotplug, and host
   suspend.
