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
     * the AP80 product route's 384 kHz PCM carrier ceiling, so carry 32 raw
     * DSD bits in each channel's S32 slot at the /32 carrier rate. */
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

size_t ap80promax_dsd_pack_native(uint8_t *dst, size_t dst_size,
                                  const uint8_t *src, size_t src_size)
{
    if (!dst || !src || (src_size & 7) || dst_size < src_size)
        return 0;

    /*
     * One 352.8 kHz S32_LE stereo frame carries 32 native DSD bits for
     * each channel: exactly 11.2896 MHz DSD256. ALSA words are little
     * endian in memory but the AIC shifts each slot most-significant bit
     * first, so reverse the four byte positions within each channel word.
     *
     * Input:  L0 R0 L1 R1 L2 R2 L3 R3
     * Output: L3 L2 L1 L0 R3 R2 R1 R0
     */
    for (size_t i = 0; i < src_size; i += 8)
    {
        dst[i + 0] = src[i + 6];
        dst[i + 1] = src[i + 4];
        dst[i + 2] = src[i + 2];
        dst[i + 3] = src[i + 0];
        dst[i + 4] = src[i + 7];
        dst[i + 5] = src[i + 5];
        dst[i + 6] = src[i + 3];
        dst[i + 7] = src[i + 1];
    }

    return src_size;
}
