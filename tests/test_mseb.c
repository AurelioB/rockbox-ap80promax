#include "mseb.h"
#include "mseb_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double response_db(const struct mseb_biquad coefs[], unsigned int mask,
                          unsigned int sample_rate, double frequency)
{
    return 10.0 * log10(mseb_response_magnitude_squared(coefs, mask,
                                                        sample_rate,
                                                        frequency));
}

static void expect_near(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance);
}

static void assert_stable_biquad(const struct mseb_biquad *coefs)
{
    assert(isfinite(coefs->b0));
    assert(isfinite(coefs->b1));
    assert(isfinite(coefs->b2));
    assert(isfinite(coefs->a1));
    assert(isfinite(coefs->a2));

    /* Jury stability conditions for z^2 + a1*z + a2. */
    assert(1.0 + coefs->a1 + coefs->a2 > 0.0);
    assert(1.0 - coefs->a1 + coefs->a2 > 0.0);
    assert(1.0 - coefs->a2 > 0.0);
}

static void test_model(void)
{
    static const struct mseb_filter_spec expected[MSEB_NUM_FILTERS] =
    {
        { MSEB_TEMPERATURE,      MSEB_FILTER_HIGH_SHELF,    33,  4425, -60 },
        { MSEB_TEMPERATURE,      MSEB_FILTER_HIGH_SHELF,   231,  4425, -60 },
        { MSEB_TEMPERATURE,      MSEB_FILTER_HIGH_SHELF,  1617,  4425, -60 },
        { MSEB_TEMPERATURE,      MSEB_FILTER_HIGH_SHELF, 11319,  4425, -60 },
        { MSEB_BASS_EXTENSION,   MSEB_FILTER_LOW_SHELF,     70,  7289, 250 },
        { MSEB_BASS_TEXTURE,     MSEB_FILTER_PEAK,          100,  8571, 200 },
        { MSEB_NOTE_THICKNESS,   MSEB_FILTER_PEAK,          200,  6667, 120 },
        { MSEB_VOCALS,           MSEB_FILTER_PEAK,          650,  4041, 125 },
        { MSEB_FEMALE_OVERTONES, MSEB_FILTER_PEAK,         3000, 14140, 100 },
        { MSEB_SIBILANCE_LF,     MSEB_FILTER_PEAK,         5800,  9991, 100 },
        { MSEB_SIBILANCE_HF,     MSEB_FILTER_PEAK,         9200,  9991, 100 },
        { MSEB_IMPULSE_RESPONSE, MSEB_FILTER_PEAK,         7500,  4041, 100 },
        { MSEB_AIR,              MSEB_FILTER_HIGH_SHELF,  10000,  7289, 250 },
    };

    for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
    {
        const struct mseb_filter_spec *actual = mseb_get_filter_spec(i);
        assert(actual != NULL);
        assert(memcmp(actual, &expected[i], sizeof(*actual)) == 0);
    }
    assert(mseb_get_filter_spec(MSEB_NUM_FILTERS) == NULL);

    int values[MSEB_NUM_PARAMETERS] = { 0 };
    values[MSEB_TEMPERATURE] = 20;
    assert(mseb_filter_gain_millidb(0, values) == -1200);
    assert(mseb_filter_gain_millidb(3, values) == -1200);
    values[MSEB_TEMPERATURE] = -20;
    assert(mseb_filter_gain_millidb(0, values) == 1200);
    values[MSEB_BASS_EXTENSION] = 100;
    assert(mseb_filter_gain_millidb(4, values) == 25000);
    values[MSEB_VOCALS] = -100;
    assert(mseb_filter_gain_millidb(7, values) == -12500);
}

static void test_ranges_and_rates(void)
{
    assert(mseb_clamp_value(-101) == -100);
    assert(mseb_clamp_value(101) == 100);
    assert(mseb_range_limit(MSEB_RANGE_FINE) == 20);
    assert(mseb_range_limit(MSEB_RANGE_MIDDLING) == 40);
    assert(mseb_range_limit(MSEB_RANGE_EXCESSIVE) == 100);
    assert(mseb_clamp_for_range(75, MSEB_RANGE_FINE) == 20);
    assert(mseb_clamp_for_range(-75, MSEB_RANGE_MIDDLING) == -40);

    struct mseb_settings settings =
    {
        .enabled = true,
        .range = MSEB_RANGE_FINE,
        .values = { 75, -75, 10 },
    };
    mseb_normalize_settings(&settings);
    assert(settings.values[0] == 20);
    assert(settings.values[1] == -20);
    assert(settings.values[2] == 10);

    assert(!mseb_rate_supported(31999));
    assert(mseb_rate_supported(32000));
    assert(mseb_rate_supported(44100));
    assert(mseb_rate_supported(96000));
    assert(!mseb_rate_supported(96001));
}

static void test_biquads_and_headroom(void)
{
    const unsigned int sample_rate = 48000;
    struct mseb_biquad coefs[MSEB_NUM_FILTERS] = { 0 };

    const struct mseb_filter_spec *bass_texture = mseb_get_filter_spec(5);
    mseb_make_biquad(bass_texture, 20000, sample_rate, &coefs[5]);
    expect_near(response_db(coefs, 1u << 5, sample_rate, 100.0), 20.0, 0.001);
    assert(mseb_calculate_precut(coefs, 1u << 5, sample_rate) >= 2010);
    assert(mseb_calculate_precut(coefs, 1u << 5, sample_rate) <= 2011);

    memset(coefs, 0, sizeof(coefs));
    const struct mseb_filter_spec *bass_extension = mseb_get_filter_spec(4);
    mseb_make_biquad(bass_extension, 25000, sample_rate, &coefs[4]);
    expect_near(response_db(coefs, 1u << 4, sample_rate, 0.0), 25.0, 0.001);
    expect_near(response_db(coefs, 1u << 4, sample_rate,
                            sample_rate / 2.0), 0.0, 0.001);
    assert(mseb_calculate_precut(coefs, 1u << 4, sample_rate) >= 2510);
    assert(mseb_calculate_precut(coefs, 1u << 4, sample_rate) <= 2511);

    memset(coefs, 0, sizeof(coefs));
    const struct mseb_filter_spec *air = mseb_get_filter_spec(12);
    mseb_make_biquad(air, 25000, sample_rate, &coefs[12]);
    expect_near(response_db(coefs, 1u << 12, sample_rate, 0.0), 0.0, 0.001);
    expect_near(response_db(coefs, 1u << 12, sample_rate,
                            sample_rate / 2.0), 25.0, 0.001);

    /* At 96 kHz the Air shelf is still rising above 20 kHz. Headroom must
     * therefore include the digital Nyquist, not merely the audible band. */
    memset(coefs, 0, sizeof(coefs));
    mseb_make_biquad(air, 25000, 96000, &coefs[12]);
    assert(mseb_calculate_precut(coefs, 1u << 12, 96000) >= 2512);
    assert(mseb_calculate_precut(coefs, 1u << 12, 96000) <= 2513);

    memset(coefs, 0, sizeof(coefs));
    for (unsigned int i = 0; i < 4; i++)
        mseb_make_biquad(mseb_get_filter_spec(i), -6000, sample_rate,
                         &coefs[i]);
    expect_near(response_db(coefs, 0xf, sample_rate, 0.0), 0.0, 0.001);
    expect_near(response_db(coefs, 0xf, sample_rate,
                            sample_rate / 2.0), -24.0, 0.001);
    assert(mseb_calculate_precut(coefs, 0xf, sample_rate) == 0);

    assert(mseb_calculate_precut(coefs, 0, sample_rate) == 0);
    assert(mseb_precut_gain_q24(0) == 1 << 24);
    assert(mseb_precut_gain_q24(4800) > 0);
    assert(mseb_precut_gain_q24(6000) > 0);
    assert(mseb_precut_gain_q24(6000) < mseb_precut_gain_q24(4800));
}

static void test_all_extremes_and_fixed_formats(void)
{
    static const unsigned int rates[] =
        { 32000, 44100, 48000, 88200, 96000 };

    for (unsigned int r = 0; r < sizeof(rates) / sizeof(rates[0]); r++)
    {
        unsigned int sample_rate = rates[r];

        for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
        {
            const struct mseb_filter_spec *spec = mseb_get_filter_spec(i);

            for (int sign = -1; sign <= 1; sign += 2)
            {
                struct mseb_biquad original[MSEB_NUM_FILTERS] = { 0 };
                struct mseb_biquad quantized[MSEB_NUM_FILTERS] = { 0 };
                int32_t fixed[5];
                uint8_t shift;
                int gain = spec->gain_millidb * 100 * sign;

                mseb_make_biquad(spec, gain, sample_rate, &original[i]);
                assert_stable_biquad(&original[i]);
                mseb_quantize_biquad(&original[i], spec->type, fixed, &shift);
                assert(shift == (spec->type == MSEB_FILTER_PEAK ? 4 : 6));
                mseb_load_quantized_biquad(fixed, spec->type, &quantized[i]);
                assert_stable_biquad(&quantized[i]);

                double probe = spec->type == MSEB_FILTER_LOW_SHELF ? 0.0 :
                               spec->type == MSEB_FILTER_HIGH_SHELF ?
                               sample_rate / 2.0 : spec->frequency;
                double expected = response_db(original, 1u << i, sample_rate,
                                              probe);
                double actual = response_db(quantized, 1u << i, sample_rate,
                                            probe);
                expect_near(actual, expected, 0.25);

                int precut = mseb_calculate_precut(quantized, 1u << i,
                                                    sample_rate);
                assert(precut >= 0);
                assert(mseb_precut_gain_q24(precut) > 0);
            }
        }
    }
}

static void build_chain(const int values[MSEB_NUM_PARAMETERS],
                        unsigned int sample_rate,
                        struct mseb_biquad coefs[MSEB_NUM_FILTERS])
{
    for (unsigned int i = 0; i < MSEB_NUM_FILTERS; i++)
    {
        const struct mseb_filter_spec *spec = mseb_get_filter_spec(i);
        struct mseb_biquad original;
        int32_t fixed[5];
        uint8_t shift;

        mseb_make_biquad(spec,
                         mseb_filter_gain_millidb(i, values),
                         sample_rate, &original);
        mseb_quantize_biquad(&original, spec->type, fixed, &shift);
        mseb_load_quantized_biquad(fixed, spec->type, &coefs[i]);
    }
}

static void test_extreme_chain_headroom(void)
{
    const unsigned int sample_rate = 96000;
    const unsigned int full_mask = (1u << MSEB_NUM_FILTERS) - 1;
    struct mseb_biquad coefs[MSEB_NUM_FILTERS];
    const int cancelling[MSEB_NUM_PARAMETERS] =
        { -100, -100, -100, -100, -100, -100, -100, 100, -100, -100 };

    build_chain(cancelling, sample_rate, coefs);
    double final_db = response_db(coefs, full_mask, sample_rate, 10711.1);
    double prefix_db = 10.0 * log10(mseb_prefix_peak_magnitude_squared(
                                coefs, full_mask, sample_rate, 10711.1));
    assert(prefix_db > final_db + 15.0);
    assert(mseb_calculate_precut(coefs, full_mask, sample_rate) > 2500);

    const int maximum[MSEB_NUM_PARAMETERS] =
        { -100, -100, 100, 100, 100, 100, 100, 100, 100, 100 };
    build_chain(maximum, sample_rate, coefs);
    int precut = mseb_calculate_precut(coefs, full_mask, sample_rate);
    assert(precut > 5700);
    assert(mseb_precut_gain_q24(precut) > 0);
    assert(mseb_precut_gain_q24(precut) < mseb_precut_gain_q24(4800));
}

int main(void)
{
    test_model();
    test_ranges_and_rates();
    test_biquads_and_headroom();
    test_all_extremes_and_fixed_formats();
    test_extreme_chain_headroom();
    puts("MSEB tests passed");
    return 0;
}
