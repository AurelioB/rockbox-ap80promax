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

`test.sh` builds the target-specific Warble codec runner, executes the DSD
transport and codec fixtures, then builds the simulator and validates all
installed OneBit skins.

`simulator.sh` builds the 360 x 640 Rockbox application simulator and validates
the installed OneBit WPS/SBS files with a target-specific `checkwps`. It is
useful for UI, theme, metadata, and touch testing, but it does not emulate the
AP80 kernel, DAC, jack detection, USB, wireless hardware, or DSD transport.
