/***************************************************************************
 * Hidizs AP80 Pro Max DSD transport selection and packing.
 ***************************************************************************/

#include "dsd-ap80promax.h"

#include <string.h>

enum dsd_transport ap80promax_dsd_select_transport(unsigned int dsd_rate)
{
    if (dsd_rate == 0 || dsd_rate % 8 != 0)
        return DSD_TRANSPORT_ERROR;

    /* Keep DSD64/128 on the physically validated DoP route. DSD256 exceeds
     * the AP80 product route's 384 kHz PCM carrier ceiling, so use the
     * generic HiBy native transform at the /32 carrier rate instead. */
    if (dsd_rate / 16 <= AP80PROMAX_DSD_DOP_MAX_ALSA_RATE)
        return DSD_TRANSPORT_DOP;

    if (dsd_rate / 32 <= AP80PROMAX_DSD_NATIVE_MAX_ALSA_RATE)
        return DSD_TRANSPORT_NATIVE;

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

unsigned int ap80promax_dsd_period_frames(unsigned int carrier_rate)
{
    if (carrier_rate <= 11025)
        return 1024;
    if (carrier_rate <= 22050)
        return 2048;
    if (carrier_rate <= 48000)
        return 4096;
    if (carrier_rate <= 96000)
        return 8192;
    if (carrier_rate <= 192000)
        return 16384;
    return 32768;
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
        uint8_t left = src[i + 0];
        uint8_t right = src[i + 1];

        /*
         * Byte-exact equivalent of AP80 hiby_player 0x740180 and R3 II
         * hiby_player 0x73f9a0. The first source byte indexes the 0x00,
         * 0x02, ... table (even serial positions); the second indexes the
         * 0x00, 0x01, ... table (odd serial positions).
         */
        dst[i + 0] = native_even_nibble[left & 0x0f] |
                     native_odd_nibble[right & 0x0f];
        dst[i + 1] = native_even_nibble[left >> 4] |
                     native_odd_nibble[right >> 4];
    }

    return src_size;
}
