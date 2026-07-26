/***************************************************************************
 * Hidizs AP80 Pro Max DSD transport helpers.
 ***************************************************************************/

#ifndef DSD_AP80PROMAX_H
#define DSD_AP80PROMAX_H

#include "dsd.h"
#include <stddef.h>
#include <stdint.h>

#define AP80PROMAX_DSD_MAX_ALSA_RATE 384000u

enum dsd_transport ap80promax_dsd_select_transport(unsigned int dsd_rate);
unsigned int ap80promax_dsd_carrier_rate(unsigned int dsd_rate,
                                         enum dsd_transport transport);

/* Input is L0 R0 L1 R1. Output is S24_LE-in-32-bit-slot stereo DoP. */
size_t ap80promax_dsd_pack_dop(uint8_t *dst, size_t dst_size,
                               const uint8_t *src, size_t src_size,
                               uint8_t *marker);

/* Mirrors hiby_player's in-place two-byte native DSD transform. */
size_t ap80promax_dsd_pack_native(uint8_t *dst, size_t dst_size,
                                  const uint8_t *src, size_t src_size);

#endif /* DSD_AP80PROMAX_H */
