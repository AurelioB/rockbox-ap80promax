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
| DSD256 (11.2896 MHz) | PCM fallback | 176.4 kHz | Native route is quarantined after hardware static |
| DSD512 | unsupported | 705.6 kHz native | Exceeds the stock ALSA limit |

The product-specific stock route table does not expose three distinct analog
transports. In `usr/resource/ot_devices.json`, `AnalogDsdD2p`,
`AnalogDsdDop`, and `AnalogDsdNative` all map to the same `"dop"` backend,
whose rate list ends at 384 kHz. A DSD256 DoP carrier would be 705.6 kHz.
Live tracing of the bundled player with the DSD256 test file confirmed the
result: `libdsd2pcm.so` is loaded, ALSA receives signed 32-bit PCM,
`DOP_EN` remains off, and the kernel reports `es9219_dai_prepare pcm`.

The generic `hiby_player` binary still contains `DOP ERR_RATE rejected`,
`DOP recover to native`, a `/32` carrier calculation, and an in-place native
bit-interleave transform. Those generic code paths are not evidence that this
AP80 firmware configures or exercises them. Disassembly shows that the
transform consumes each adjacent interleaved `L/R` byte pair, not successive
bytes from one channel. Both physical native experiments produced static,
so the AP80-specific native clock and framing contract remains unknown.

The bundled `codec_es9219_v2.ko` does establish one useful fact:
`es9219_dai_prepare` selects the DAC's DSD branch whenever the `DOP_EN` mixer
state is nonzero. That switch alone is insufficient to bring up a valid
native stream.

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
- [x] Define a target-independent codec-to-output DSD contract.
- [x] Add AP80 route selection and bitstream packers.
- [x] Add direct ALSA DSD open/write/pause/close support.
- [x] Add DSF and uncompressed DSDIFF metadata parsing.
- [x] Add the DSF/DSDIFF streaming codec with seeking.
- [x] Add deterministic host tests for route selection and byte packing.
- [x] Add and test an in-tree DSD64/128/256-to-PCM fallback.
- [ ] Recover an AP80-specific native framing and clock contract independently;
      the product firmware does not provide a working reference route.
- [ ] Validate ALSA parameters and both physical outputs on an AP80 Pro Max.
- [ ] Add a user-facing transport preference after the automatic route has
      been validated on hardware.

## Device validation checklist

Use known-good stereo test files whose rates are independently verified.

1. Start at low hardware volume with DSD64.
2. Confirm the ALSA rate is 176400 and `DOP_EN` is 1.
3. Confirm clean playback on both 3.5 mm and 4.4 mm outputs.
4. Repeat with DSD128; ALSA must report 352800 and `DOP_EN` must remain 1.
5. Play DSD256; it must report PCM fallback at 176400 and must never emit
   static. `DOP_EN` must be 0.
6. Pause, resume, seek, skip to PCM, and skip from PCM back to each DSD rate.
7. Confirm that voice prompts do not mix into DSD playback.
8. Confirm that DSD512 and DST-compressed DSDIFF fail safely without noise.
9. Check the Rockbox log for short writes, ALSA recovery, or transport setup
   failures.
10. Force a direct-endpoint failure and confirm the log reports the 176.4 kHz
    PCM fallback without a crash or burst of noise.

The diagnostic native packing and `/32` carrier rule mirror dormant code in
the generic HiBy binary, not a route used by the AP80 product configuration.
Those properties plus ES9219 DSD-mode selection are insufficient for correct
physical output. Production builds keep DSD256 on the in-tree 176.4 kHz PCM
fallback. For controlled validation only, creating the volatile
`/tmp/rockbox-native-dsd-test` marker enables native selection until the next
reboot; ordinary users and persistent configuration cannot enable it.
