# AP80 Pro Max power-management notes

## Stock firmware behavior

The AP80 Pro Max firmware separates display blanking, standby, and shutdown.
The evidence below comes from the extracted 1.2 firmware image.

- The backlight is exposed at
  `/sys/class/backlight/backlight_pwm0/brightness`.
- The framebuffer's blanking interface powers down the ST7701 panel and the
  CST8xx touch controller.
- The stock player contains an ordered standby path:
  `PM: early suspend`, `/usr/bin/bt_suspend`, `PM: suspend`,
  `echo mem > /sys/power/state`, `PM: resume`, `PM: late resume`, and
  `/usr/bin/bt_resume`.
- `bt_suspend` brings HCI down, stops the firmware loader, and removes power
  through rfkill. `bt_resume` restores the radio firmware and Bluetooth
  services.
- The AXP2101 PMIC, CW2015 fuel gauge, CST8xx touch controller, ES9219 codec,
  and audio-card drivers all provide kernel suspend/resume hooks.
- The power GPIO (`KEY_POWER`) and upper transport GPIO
  (`KEY_NEXTSONG`) are configured with `wakeup=y`. The ADC play/previous
  buttons and encoder are not declared as suspend wake sources.
- The AP80 product configuration disables the stock double-tap-to-wake
  option.
- Stock list rows are normally about 68 to 80 logical pixels high, and its
  Now Playing transport targets are 70 by 70 logical pixels.

## Rockbox policy

Rockbox blanks the framebuffer when the backlight expires and advances it to
full panel power-down two seconds later. This saves the display and touch
power while allowing audio playback and physical transport controls to
continue.

The default AP80 brightness is 60 percent, matching the stock firmware's
boot/UI level rather than driving the backlight at maximum. Idle poweroff
remains enabled by Rockbox with its standard ten-minute default and only
applies when playback is stopped or paused.

Suspend-to-RAM is not enabled during playback. The stock kernel exposes the
mechanism, but ALSA DMA, native DSD/DoP, S/PDIF, storage hotplug, and input
state must all be tested across resume before it is safe to use. A future
standby implementation must:

1. Refuse suspend during playback, recording, USB, update, or storage work.
2. Quiesce Bluetooth and Wi-Fi through their stock service boundaries.
3. Sync storage, request `mem`, and tolerate failure without blank-screen
   lockup.
4. Reload physical button state after resume because release events may be
   lost while suspended.
5. Restore output routing, ALSA format/rate, DSD transport, wireless state,
   framebuffer contents, backlight, and touch in that order.
6. Treat only power and the upper transport GPIO as guaranteed wake sources.

## Shutdown button release

Rockbox's software shutdown threshold is reached while the physical power
knob is still depressed. The AP80 target waits up to five seconds for that
button to be released before blanking the display and invoking
`/sbin/poweroff`. This prevents a continuing PMIC-level long press from
overlapping the final Linux shutdown while preserving a bounded fallback for
a stuck input device.
