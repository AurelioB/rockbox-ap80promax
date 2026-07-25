# DSD support on the Hidizs AP80 Pro Max

## Goal

Prefer bit-perfect playback of uncompressed stereo DSF and DSDIFF files, with
a deterministic PCM fallback if the direct endpoint cannot be configured.
The direct implementation follows the transport choices observed in the
stock `hiby_player`:

| Source | Preferred transport | ALSA carrier | Reason |
| --- | --- | --- | --- |
| DSD64 (2.8224 MHz) | DoP | 176.4 kHz | Fits below the 384 kHz device limit |
| DSD128 (5.6448 MHz) | DoP | 352.8 kHz | Fits below the 384 kHz device limit |
| DSD256 (11.2896 MHz) | native fallback | 352.8 kHz | DoP would require 705.6 kHz |
| DSD512 | unsupported | 705.6 kHz native | Exceeds the stock ALSA limit |

“Native fallback” is intentional: selecting the stock player's DoP route for
DSD256 produces `DOP ERR_RATE rejected` followed by `DOP recover to native`.

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
4. The AP80 transport packer emits:
   - DoP: two DSD bytes per channel plus alternating `0x05`/`0xfa` markers.
   - Native: the same nibble lookup/interleave transform found in the stock
     player.
5. `DOP_EN` is enabled only for actual DoP marker framing. It remains disabled
   for the DSD256 native fallback.
6. On stop, seek, error, or end-of-file, the DSD endpoint is closed and normal
   PCM parameters are restored.

The synchronous direct path deliberately gives up gapless DSD-to-PCM
transitions and software DSP. Hardware DAC volume remains available.

If direct setup fails, the same codec converts DSD64/128/256 to interleaved
29-bit PCM at 176.4 kHz. A third-order CIC stage reduces the stream to
352.8 kHz and a 31-tap half-band FIR filters and decimates it to the output
rate. The converter is streaming, keeps independent channel state, and uses
only GPL-compatible in-tree code. It does not load the stock
`libdsd2pcm.so`, whose private ABI and redistribution terms remain unknown.
DSD512 and DST-compressed DSDIFF remain unsupported.

## Implementation phases

- [x] Reverse-engineer stock route selection, DoP layout, native transform,
      and `DOP_EN` behavior.
- [x] Define a target-independent codec-to-output DSD contract.
- [x] Add AP80 route selection and bitstream packers.
- [x] Add direct ALSA DSD open/write/pause/close support.
- [x] Add DSF and uncompressed DSDIFF metadata parsing.
- [x] Add the DSF/DSDIFF streaming codec with seeking.
- [x] Add deterministic host tests for route selection and byte packing.
- [x] Add and test an in-tree DSD64/128/256-to-PCM fallback.
- [ ] Validate ALSA parameters and both physical outputs on an AP80 Pro Max.
- [ ] Add a user-facing transport preference after the automatic route has
      been validated on hardware.

## Device validation checklist

Use known-good stereo test files whose rates are independently verified.

1. Start at low hardware volume with DSD64.
2. Confirm the ALSA rate is 176400 and `DOP_EN` is 1.
3. Confirm clean playback on both 3.5 mm and 4.4 mm outputs.
4. Repeat with DSD128; ALSA must report 352800 and `DOP_EN` must remain 1.
5. Play DSD256; ALSA must report 352800 and `DOP_EN` must be 0.
6. Pause, resume, seek, skip to PCM, and skip from PCM back to each DSD rate.
7. Confirm that voice prompts do not mix into DSD playback.
8. Confirm that DSD512 and DST-compressed DSDIFF fail safely without noise.
9. Check the Rockbox log for short writes, ALSA recovery, or transport setup
   failures.
10. Force a direct-endpoint failure and confirm the log reports the 176.4 kHz
    PCM fallback without a crash or burst of noise.

The native packing and `/32` carrier rule mirror the stock binary, but they
remain the highest-risk part until measured on the device.
