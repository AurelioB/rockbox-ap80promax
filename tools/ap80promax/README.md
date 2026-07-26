# AP80 Pro Max developer tools

These scripts build and validate the `hidizsap80promax` target without
committing the proprietary firmware, cross-toolchain, or generated artifacts.

By default the scripts expect this checkout to live in the workspace layout
used during bring-up:

```text
workspace/
  ap80max.upt
  dist/
  toolchain/
  rockbox/
```

The locations can be overridden with:

- `AP80_TOOLCHAIN_BIN`
- `AP80_DIST_DIR`
- `AP80_STOCK_UPDATE`
- `AP80_SIM_ROOT`
- `AP80_CHECKWPS_BUILD`
- `AP80_JOBS`

Run `build.sh` for the application, bootloader, data ZIP, checksums, and
release validation. Run `package.sh` only when a legally obtained stock
`ap80max.upt` is available locally; it creates the engineering updater but
does not flash anything.

`test.sh` checks both legacy and extended sample-rate enumerations, tests the
streaming DSD-to-PCM converter, builds the target-specific Warble codec
runner, executes the direct and fallback DSD fixtures, exercises USB update,
rollback, corrupt-archive, and wrong-target handling, then builds the
simulator and validates all installed OneBit skins. The recovery test also
exercises the SD bypass marker, early-exit fallback, hung-bootloader watchdog,
and the bootloader-to-application handoff watchdog.

After the bootloader has been installed once, `update-usb.sh` updates only
the SD card's `/.rockbox` tree over the boot menu's ADB connection. It stages
and validates the release before switching directories, preserves user data,
and retains the previous complete installation for `--rollback`. It never
writes a firmware partition or replaces the installed bootloader.

`simulator.sh` builds the 360 x 640 Rockbox application simulator and validates
the installed OneBit WPS/SBS files with a target-specific `checkwps`. The Linux
framebuffer reports a 480 x 640 scanout, but the physical panel, stock UI, and
CST8xx touchscreen expose a 360 x 640 logical display. The simulator therefore
uses the visible logical geometry. On hardware, Rockbox leaves the vendor
scanout mode unchanged and copies 360-pixel logical rows using its 480-pixel
stride. The simulator is useful for UI, theme, metadata, and touch testing,
but it does not emulate the AP80 kernel, DAC, jack detection,
USB, wireless hardware, or DSD transport.
