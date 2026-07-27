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
./tools/ap80promax/test.sh
./tools/ap80promax/build.sh
./tools/ap80promax/package.sh
```

`test.sh` runs the host codec, service, theme, update, recovery, and simulator
checks. `build.sh` then creates and verifies:

- `dist/rockbox.ap80promax`
- `dist/rockbox-ap80promax.zip`
- `dist/bootloader.ap80promax`
- `dist/update-usb.sh`
- `dist/update-device.sh`
- `dist/SHA256SUMS`

`package.sh` requires a legally obtained `ap80max.upt` matching the installed
firmware. It creates `dist/ap80max_rb.upt` and its checksum without flashing
the player. It then reopens the completed updater and verifies the embedded
bootloader, recovery launcher, preserved stock launcher, readiness watchdog,
and exact match with the current build outputs.

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

The first boot-menu choice waits ten seconds. A saved Tools or HiBy choice,
and Rockbox before its first confirmed successful start, waits five seconds.
After Rockbox reaches its usable UI, later boots use one countdown-free
startup image and a half-second recovery-key sampling window.

To interrupt that fast path, hold Previous, Next, Volume Up, or Volume Down
while powering on and keep it held until the full boot menu appears. The
bootloader reads keys which were already down during the HIDIZS splash, so
the timing does not need to be exact. The bootloader durably arms an attempt
marker before every Rockbox handoff, and only a clean Rockbox shutdown clears
it. A failed handoff, crash, hang followed by a forced power-off, or hard power
cut therefore restores the five-second recovery menu on the following boot.
The held-button override remains available at any time.

This recovery build starts ADB before Rockbox initializes the display. It also
requires the bootloader to report that its system, 360 x 640 display,
font, buttons, and backlight initialized within 15 seconds. Otherwise it kills
the bootloader and starts the untouched HiBy player automatically. Once
Rockbox is selected, the screen displays **STARTING ROCKBOX...** and the
launcher allows 60 seconds for the application and audio path to initialize.
A failed or blocked handoff also falls back to the untouched HiBy player.

## Boot-menu controls

| Control | Boot menu |
| --- | --- |
| Next / Volume Down | Previous choice |
| Previous / Volume Up | Next choice |
| Wheel counter-clockwise / clockwise | Previous / next choice |
| Play | Boot selected choice |

The choices are **ROCKBOX**, **TOOLS**, and **HIBY PLAYER**. The Tools menu
provides USB Rockbox update, ADB start/stop, SD remount, restart, shutdown,
and the vendor recovery request.

## Update Rockbox over USB

The firmware updater is required only for the initial bootloader
installation. Later Rockbox builds can be installed through USB without
flashing firmware or changing the bootloader.

Install Android Platform Tools so the computer has `adb`, then:

1. Download or build `rockbox-ap80promax.zip`, `update-usb.sh`, and
   `update-device.sh`. Keep all three in the same directory.
2. Restart the player into **TOOLS → USB Rockbox update**. If the currently
   installed bootloader predates that menu item, choose **TOOLS → ADB start**
   instead; no extra firmware flash is required.
3. Connect the player directly to the computer with a data-capable USB cable.
4. On Linux, macOS, WSL, or another POSIX shell, run:

   ```sh
   ./update-usb.sh
   ```

   From a source checkout, the same command is:

   ```sh
   ./tools/ap80promax/update-usb.sh
   ```

5. Wait for the computer to report success. The dedicated update screen also
   reports **Update complete**; press Play there to restart. With an older
   bootloader, select **Restart** from the Tools menu.

To install a ZIP from another location, use:

```sh
./update-usb.sh --zip /path/to/rockbox-ap80promax.zip
```

The updater verifies the ZIP and connected target, checks the USB transfer
checksum, copies the current installation into a staging directory, overlays
the new release, and validates it before switching directories. Configuration,
custom themes, the database, and the remembered boot choice are retained.
The previous complete tree remains at `/.rockbox-backup`.

If the new build does not work, return to **TOOLS → USB Rockbox update** (or
**TOOLS → ADB start** on an older bootloader) and run:

```sh
./update-usb.sh --rollback
```

An interrupted transfer or extraction leaves the active `/.rockbox`
directory untouched. Do not use Mass Storage mode for this procedure; the
ADB updater keeps filesystem ownership on the player and avoids concurrent
host/device writes.

## ADB while Rockbox is running

For ordinary diagnostics, select **Settings → General Settings → System →
USB → USB Mode → Android Debug Bridge**, then reconnect the USB cable.
The AP80 target delegates this mode to the same Hidizs `T90adb` service used
by the working bootloader path. Confirm enumeration with:

```sh
adb devices
```

This mode is not an alternative entry point for the Rockbox update helper.
`update-device.sh` refuses to replace the active installation while
`/tmp/rockbox.ap80promax` is running. Use **TOOLS → USB Rockbox update** for
updates and use application ADB only for diagnostics. On the AP80, ADB is an
exclusive USB mode and is not combined with the USB DAC function.

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
bottom transport controls. In an OneBit menu, the left 120 x 80 header area is
back/cancel and the right 120 x 80 area opens the context menu. These match the
stock UI's 90 x 60 logical targets after panel scaling.

OneBit menus use nearly the full display width, with 20-pixel side margins,
the bundled 21-pixel LanaPixel font, original list glyphs enlarged into
48 x 32 cells, and DPI-sized rows. Their navigation headers use 21-pixel
titles and 14-pixel status values. Now Playing also uses a 440-pixel content
column with 20-pixel side margins while preserving each original theme's
composition and pixel-art treatment. Theme authors should follow
[the AP80 UI usability specification](AP80_PRO_MAX_UI_SPEC.md).

Menus and file lists default to Rockbox's **Point** touchscreen mode:

| Gesture | Action |
| --- | --- | --- |
| Tap an item | Select or open it |
| Hold an item | Open its context menu |
| Drag vertically | Scroll the list |
| Tap the title text, when visible | Back/cancel |
| Tap the title icon, when visible | Main menu |

Now Playing uses the active theme's touch regions. AP80 Cabbie and OneBit
themes provide large regions for play/pause, previous/next, seek, volume,
browser, playlist, and context actions. On OneBit, the WPS header is browser
on the left, main menu in the centre, and context menu on the right.

Rockbox also offers **Settings → General Settings → Display → Touchscreen
Settings → Touchscreen Mode → Grid**. In Grid mode, the screen becomes a 3 x
3 keypad: the middle-left zone is back, centre is select, top/bottom middle
move through items, and top-left opens the menu. The Now Playing grid adds
previous/next on middle-left/right, volume on top/bottom middle, browser on
top-right, and the quick screen on bottom-left.

For a reliable one-handed back action in any list or menu, press the physical
Power button briefly. Holding Power remains the hardware power-off action.

## Sleep, standby, and shutdown

Rockbox turns off the backlight at the configured timeout and powers down the
panel and touch controller two seconds later. Audio and the physical controls
remain active. The default brightness for a fresh configuration is 60 percent,
matching HiByOS rather than running the panel at maximum.

HiByOS additionally enters kernel suspend-to-RAM while idle. This engineering
build does not yet request suspend because analog PCM, S/PDIF, DSD/DoP, storage,
and input must all be proven across resume on the physical player. Until that
gate is complete, use **Settings → General Settings → Startup/Shutdown → Idle
Poweroff** to control stopped/paused battery drain.

When shutting down with the power knob, release it when the Rockbox shutdown
message appears. Rockbox waits for that release before invoking Linux poweroff,
so the PMIC does not interpret one continuing press as both shutdown and a new
hardware action.

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

If the boot menu itself is not usable, shut down the player, put an empty file
named `rockbox-force-hiby` in the SD-card root, reinsert the card, and boot.
The launcher checks this visible marker before starting any Rockbox code and
executes `/usr/bin/hiby_player` directly. The bypass remains active until the
file is removed.

The recovery launcher writes `rockbox-bootloader.log` in the SD-card root
when the card is mounted, or `/tmp/rockbox-bootloader.log` otherwise. ADB is
started before the framebuffer path, so `/tmp/rockbox-adb.log`, kernel logs,
and the bootloader log can still be collected after an early failure.

### Original firmware does not start

Choose **TOOLS → Recovery**. This writes the vendor `recovery` request to
`/proc/jz/reset/reset`. The behavior of that software request still requires
physical validation and should not be treated as a guaranteed unbrick
procedure.

The AP80 Pro Max powered-off updater was confirmed on physical hardware:

1. Format a microSD card as FAT32 and place the single intended updater at
   its root as `ap80max.upt`.
2. Force the player fully off by holding Power.
3. Hold **Previous**, then press and hold **Power**.
4. Keep Previous held until the firmware updater appears.

Do not interrupt an update once progress begins. Do not test updater files
from related HiBy or Hidizs products.

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
