/***************************************************************************
 * Hidizs AP80 Pro Max DSD transport selection and packing.
 ***************************************************************************/

#include "dsd-ap80promax.h"

#include <string.h>

enum dsd_transport ap80promax_dsd_select_transport(unsigned int dsd_rate)
{
    if (dsd_rate == 0 || dsd_rate % 8 != 0)
        return DSD_TRANSPORT_ERROR;

    /* Use DoP only where its carrier fits. The stock player recovers to a
     * native path for DSD256, but selecting that path without all of the
     * stock player's private hardware-state transitions produced full-scale
     * static on an AP80 Pro Max. Returning ERROR here makes the codec use its
     * safe, in-tree DSD-to-PCM fallback. Keep the native packer available for
     * controlled development, but never select it automatically. */
    if (dsd_rate / 16 <= AP80PROMAX_DSD_MAX_ALSA_RATE)
        return DSD_TRANSPORT_DOP;

    return DSD_TRANSPORT_ERROR;
}

unsigned int ap80promax_dsd_carrier_rate(unsigned int dsd_rate,
                                         enum dsd_transport transport)
{
    if (transport == DSD_TRANSPORT_DOP)
        return dsd_rate / 16;
    if (transport == DSD_TRANSPORT_NATIVE)
        return dsd_rate / 32;
    return 0;
}

size_t ap80promax_dsd_pack_dop(uint8_t *dst, size_t dst_size,
                               const uint8_t *src, size_t src_size,
                               uint8_t *marker)
{
    if (!dst || !src || !marker || (src_size & 3) ||
        dst_size < src_size * 2)
        return 0;

    uint8_t dop_marker = *marker;
    uint8_t *out = dst;

    for (size_t i = 0; i < src_size; i += 4)
    {
        /* Exact layout used by hiby_player for 24 valid bits in a
         * little-endian 32-bit ALSA slot. */
        *out++ = src[i + 2];
        *out++ = src[i + 0];
        *out++ = dop_marker;
        *out++ = 0;

        *out++ = src[i + 3];
        *out++ = src[i + 1];
        *out++ = dop_marker;
        *out++ = 0;

        dop_marker = 0xff - dop_marker;
    }

    *marker = dop_marker;
    return src_size * 2;
}

static const uint8_t native_even_nibble[16] =
{
    0x00, 0x02, 0x08, 0x0a, 0x20, 0x22, 0x28, 0x2a,
    0x80, 0x82, 0x88, 0x8a, 0xa0, 0xa2, 0xa8, 0xaa,
};

static const uint8_t native_odd_nibble[16] =
{
    0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15,
    0x40, 0x41, 0x44, 0x45, 0x50, 0x51, 0x54, 0x55,
};

size_t ap80promax_dsd_pack_native(uint8_t *dst, size_t dst_size,
                                  const uint8_t *src, size_t src_size)
{
    if (!dst || !src || (src_size & 1) || dst_size < src_size)
        return 0;

    for (size_t i = 0; i < src_size; i += 2)
    {
        uint8_t even = src[i + 0];
        uint8_t odd = src[i + 1];

        dst[i + 0] = native_even_nibble[even & 0x0f] |
                     native_odd_nibble[odd & 0x0f];
        dst[i + 1] = native_even_nibble[even >> 4] |
                     native_odd_nibble[odd >> 4];
    }

    return src_size;
}
