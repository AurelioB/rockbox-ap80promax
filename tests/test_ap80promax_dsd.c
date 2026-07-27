#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../firmware/target/hosted/hiby/ap80promax/dsd-ap80promax.h"

static void test_routes(void)
{
    assert(ap80promax_dsd_select_transport(DSD_RATE_64) ==
           DSD_TRANSPORT_DOP);
    assert(ap80promax_dsd_carrier_rate(DSD_RATE_64,
           DSD_TRANSPORT_DOP) == 176400);

    assert(ap80promax_dsd_select_transport(DSD_RATE_128) ==
           DSD_TRANSPORT_DOP);
    assert(ap80promax_dsd_carrier_rate(DSD_RATE_128,
           DSD_TRANSPORT_DOP) == 352800);

    /* DSD256 uses the generic HiBy native /32 transport on the AP80. */
    assert(ap80promax_dsd_select_transport(DSD_RATE_256) ==
           DSD_TRANSPORT_NATIVE);
    assert(ap80promax_dsd_carrier_rate(DSD_RATE_256,
           DSD_TRANSPORT_NATIVE) == 352800);

    assert(ap80promax_dsd_select_transport(DSD_RATE_512) ==
           DSD_TRANSPORT_ERROR);

    assert(ap80promax_dsd_period_frames(11025) == 1024);
    assert(ap80promax_dsd_period_frames(11026) == 2048);
    assert(ap80promax_dsd_period_frames(176400) == 16384);
    assert(ap80promax_dsd_period_frames(352800) == 32768);
}

static void test_dop(void)
{
    const uint8_t src[] =
    {
        0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88,
    };
    const uint8_t expected[] =
    {
        0x33, 0x11, 0x05, 0x00, 0x44, 0x22, 0x05, 0x00,
        0x77, 0x55, 0xfa, 0x00, 0x88, 0x66, 0xfa, 0x00,
    };
    uint8_t dst[sizeof(expected)];
    uint8_t marker = 0x05;

    assert(ap80promax_dsd_pack_dop(dst, sizeof(dst), src, sizeof(src),
                                  &marker) == sizeof(dst));
    assert(memcmp(dst, expected, sizeof(dst)) == 0);
    assert(marker == 0x05);
}

static void test_native(void)
{
    const uint8_t src[] = { 0x01, 0x02, 0x80, 0x40 };
    /* Each adjacent L/R pair is bit-interleaved into one S16 word. */
    /* hiby_player: first byte on even, second on odd serial positions. */
    const uint8_t expected[] = { 0x06, 0x00, 0x00, 0x90 };
    uint8_t dst[sizeof(src)];

    assert(ap80promax_dsd_pack_native(dst, sizeof(dst), src, sizeof(src)) ==
           sizeof(dst));
    assert(memcmp(dst, expected, sizeof(dst)) == 0);
}

int main(void)
{
    test_routes();
    test_dop();
    test_native();
    return 0;
}
