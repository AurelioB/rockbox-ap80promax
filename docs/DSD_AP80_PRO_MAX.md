# DSD support on the Hidizs AP80 Pro Max

## Goal

Prefer bit-perfect playback of uncompressed stereo DSF and DSDIFF files, with
a deterministic PCM fallback if the direct endpoint cannot be configured.
Rockbox conservatively selects transports that fit the product's advertised
ALSA carrier-rate table:

| Source | Preferred transport | ALSA carrier | Reason |
| --- | --- | --- | --- |
| DSD64 (2.8224 MHz) | DoP | 176.4 kHz | Fits below the 384 kHz device limit |
| DSD128 (5.6448 MHz) | DoP | 352.8 kHz | Fits below the 384 kHz device limit |
| DSD256 (11.2896 MHz) | Native DSD | 352.8 kHz | HiBy `/32` native framing |
| DSD512 | unsupported | 1.4112 MHz DoP | Exceeds the patched ALSA limit |

The product-specific stock route table does not expose three distinct analog
transports. In `usr/resource/ot_devices.json`, `AnalogDsdD2p`,
`AnalogDsdDop`, and `AnalogDsdNative` all map to the same `"dop"` backend,
whose rate list ends at 384 kHz. A DSD256 DoP carrier would be 705.6 kHz.
Live tracing of the bundled player with the DSD256 test file confirmed the
result: `libdsd2pcm.so` is loaded, ALSA receives signed 32-bit PCM,
`DOP_EN` remains off, and the kernel reports `es9219_dai_prepare pcm`.

The generic `hiby_player` binary still contains `DOP ERR_RATE rejected`,
`DOP recover to native`, a `/32` carrier calculation, and an in-place native
bit-interleave transform. The AP80 product route does not exercise that path,
but disassembly at AP80 `0x740180` and R3 II `0x73f9a0` provides its byte-exact
contract: each adjacent interleaved `L/R` byte pair becomes one `S16_LE` word,
with the first byte in the even serial bit positions and the second in the odd
positions. A later Rockbox revision mislabeled the two tables and reversed
those positions, producing full-scale static on hardware.

The bundled `codec_es9219_v2.ko` does establish one useful fact:
`es9219_dai_prepare` selects the DAC's DSD branch whenever the `DOP_EN` mixer
state is nonzero. That switch alone is insufficient to bring up a valid
native stream.

### HiBy R3 II 2025 donor firmware

The HiBy R3 II 2025 is an unusually close reference: X1600E, dual ES9219C,
HiByOS, and advertised native DSD256. Its firmware clarifies what that claim
means in the product audio path:

- its `soc_aic.ko` is byte-identical to the AP80 Pro Max module;
- its active `ot_devices_2025.json` maps `AnalogDsdD2p`, `AnalogDsdDop`, and
  `AnalogDsdNative` to `"dop"`;
- unlike the AP80 table, it advertises 705.6 and 768 kHz;
- its ES9219 DAI rate mask includes the corresponding ALSA rate bits; and
- its machine driver uses an HBC3000 FPGA to provide the audio clocks.

The useful donor behavior is therefore bit-perfect DSD256 carried as
705.6 kHz DoP, not the dormant generic native packer. The AP80 has no
HBC3000 module, but its direct-clock machine driver already derives the
22.5792/24.576 MHz clocks for lower rate families. At 705.6/768 kHz the
X1600 AIC requires at least 64 clock cycles per stereo frame, matching its
two 32-bit I2S slots. The engineering updater applies two hash-locked
vendor-module patches: advertise 705.6/768 kHz in the ES9219 DAI and request
45.1584/49.152 MHz from the existing clock rule for those rates.

Physical testing of the donor-derived 705.6 kHz DoP route on the AP80 produced
background static and periodic stalls even after the transmit clock was
measured at the correct 45.1584 MHz. The current single-tester build therefore
uses the lower-bandwidth generic native route for DSD256. If ALSA rejects that
endpoint, the codec closes it and falls back to the in-tree 176.4 kHz PCM
converter.

## Architecture

DSD must not enter Rockbox's fixed-point PCM DSP or mixer. EQ, replay gain,
crossfeed, software volume, fades, voice prompts, and mixing all change bits
and would destroy either DoP markers or the native bitstream.

The DSD codec therefore owns a small, synchronous output path:

1. The metadata parser recognizes `.dsf` and `.dff`, records the DSD bit rate,
   channel count, sample count, duration, and data offset.
2. The codec reads uncompressed stereo DSD and normalizes it to interleaved,
   MSB-first bytes: `L0 R0 L1 R1`.
3. A codec API callback stops PCM/voice playback and opens the AP80's ALSA
   endpoint in the selected DSD transport.
4. For validated direct modes, the AP80 transport packer emits:
   - DoP: two DSD bytes per channel plus alternating `0x05`/`0xfa` markers.
   - Native (diagnostic only): the dormant generic HiBy nibble
     lookup/interleave transform. Automatic selection is disabled, and this
     transform has not been validated as an AP80 native transport.
5. `DOP_EN` selects the ES9219 DSD branch for DoP and diagnostic native
   playback. Native DSD is not selected during ordinary production playback.
6. On stop, seek, error, or end-of-file, the DSD endpoint is closed and normal
   PCM parameters are restored.

The synchronous direct path deliberately gives up gapless DSD-to-PCM
transitions and software DSP. Hardware DAC volume remains available.

The ALSA period sizing also mirrors the stock player: 16,384 frames at a
176.4 kHz carrier and 32,768 frames at 352.8 kHz, with four periods buffered.
The previous fixed 4,096-frame period repeatedly underrran and re-prepared the
DAC during direct DSD.

Direct writes are staged to one negotiated ALSA period. In particular,
In the attempted DSD256 DoP route, each 8,192-byte DSF chunk contained only
2,048 carrier frames; submitting each chunk separately required sixteen ALSA
writes per 32,768-frame period and created about 474 `WRITEI` ioctls per
second. Period-aligned writes preserved the same DoP bytes while reducing
that load to about 30 writes per second. This optimization alone did not
remove the repeatable high-rate stalls. Native DSD256 uses half the carrier
rate and half the output bytes.

The first high-rate machine-driver patch reused the stock instruction that
zeroed an unsupported clock, replacing it with a shift intended to calculate
`rate * 64`. Disassembly and live clock tracing found that the preceding MIPS
branch-delay instruction had already replaced the rate in `v0` with the high
half of the `snd_soc_dai_set_sysclk` address. Shifting that address by five
requested approximately 132.120451 MHz; shifting it by six requested
approximately 264.238410 MHz. Neither value was derived from the 705.6 kHz
carrier.

The corrected hash-locked patch performs `rate * 64` one instruction earlier,
while `v0` still contains the ALSA rate, and replaces the later stock zeroing
instruction with a no-op. This requests 45.1584 MHz for 705.6 kHz and
49.152 MHz for 768 kHz without changing the existing 352.8/384 kHz branches.

If direct setup fails, the same codec converts DSD64/128/256 to interleaved
29-bit PCM at 176.4 kHz. A third-order CIC stage reduces the stream to
352.8 kHz and a 31-tap half-band FIR filters and decimates it to the output
rate. The converter is streaming, keeps independent channel state, and uses
only GPL-compatible in-tree code. It does not load the stock
`libdsd2pcm.so`, whose private ABI and redistribution terms remain unknown.
DSD512 and DST-compressed DSDIFF remain unsupported.

## Implementation phases

- [x] Reverse-engineer the product route table, DoP layout, dormant generic
      native transform, and `DOP_EN` behavior.
- [x] Trace stock DSD256 playback and confirm its signed 32-bit PCM fallback.
- [x] Compare the R3 II 2025 donor firmware and recover its 705.6 kHz
      DSD256-over-DoP route.
- [x] Add hash-locked AP80 module patches.
- [x] Test the 705.6 kHz DSD256 DoP route on AP80 hardware and reject it after
      static and periodic stalls.
- [x] Define a target-independent codec-to-output DSD contract.
- [x] Add AP80 route selection and bitstream packers.
- [x] Add direct ALSA DSD open/write/pause/close support.
- [x] Add DSF and uncompressed DSDIFF metadata parsing.
- [x] Add the DSF/DSDIFF streaming codec with seeking.
- [x] Add deterministic host tests for route selection and byte packing.
- [x] Add and test an in-tree DSD64/128/256-to-PCM fallback.
- [x] Recover the generic HiBy native framing and clock contract from both
      the bundled AP80 player and the R3 II donor player.
- [ ] Validate the byte-exact automatic native DSD256 route on the AP80.
- [ ] Validate ALSA parameters and both physical outputs on an AP80 Pro Max.
- [ ] Add a user-facing transport preference after the automatic route has
      been validated on hardware.

## Device validation checklist

Use known-good stereo test files whose rates are independently verified.

1. Start at low hardware volume with DSD64.
2. Confirm the ALSA rate is 176400 and `DOP_EN` is 1.
3. Confirm clean playback on both 3.5 mm and 4.4 mm outputs.
4. Repeat with DSD128; ALSA must report 352800 and `DOP_EN` must remain 1.
5. Play DSD256; ALSA must report 352800, `DOP_EN` must be 1, playback must
   remain continuous, and output must contain no static. If the endpoint
   cannot be configured, it must fall back cleanly to PCM at 176400.
6. Pause, resume, seek, skip to PCM, and skip from PCM back to each DSD rate.
7. Confirm that voice prompts do not mix into DSD playback.
8. Confirm that DSD512 and DST-compressed DSDIFF fail safely without noise.
9. Check the Rockbox log for short writes, ALSA recovery, or transport setup
   failures.
10. Force a direct-endpoint failure and confirm the log reports the 176.4 kHz
    PCM fallback without a crash or burst of noise.

The native packing and `/32` carrier rule mirror dormant code in the generic
HiBy binaries, not a route used by the AP80 product configuration. The
bit-lane assignment matches both recovered routines byte for byte and is
selected for DSD256 in the single-tester build.
