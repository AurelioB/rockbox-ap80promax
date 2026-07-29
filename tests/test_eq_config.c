#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "eq_config.h"

static bool setting_equals(const struct eq_band_setting *left,
                           const struct eq_band_setting *right)
{
    return left->cutoff == right->cutoff &&
           left->q == right->q &&
           left->gain == right->gain &&
           left->type == right->type;
}

static void assert_parse(const char *text, int cutoff, int q, int gain,
                         enum eq_filter_type type)
{
    struct eq_band_setting actual = { 0 };
    const struct eq_band_setting expected = {
        .cutoff = cutoff,
        .q = q,
        .gain = gain,
        .type = type,
    };

    assert(eq_config_parse(text, &actual));
    assert(setting_equals(&actual, &expected));
}

static void assert_parse_failure_is_atomic(const char *text)
{
    const struct eq_band_setting sentinel = {
        .cutoff = 1234,
        .q = 23,
        .gain = -45,
        .type = EQ_FILTER_HIGH_SHELF,
    };
    struct eq_band_setting actual = sentinel;

    assert(!eq_config_parse(text, &actual));
    assert(setting_equals(&actual, &sentinel));
}

static void test_canonical_values(void)
{
    assert_parse("32, 7, -15, LOW_SHELF",
                 32, 7, -15, EQ_FILTER_LOW_SHELF);
    assert_parse("125,10,+25,peak", 125, 10, 25, EQ_FILTER_PEAK);
    assert_parse(" 16000 , 7 , 30 , high_shelf \t",
                 16000, 7, 30, EQ_FILTER_HIGH_SHELF);
}

static void test_legacy_migration(void)
{
    const enum eq_filter_type types[] = {
        EQ_FILTER_LOW_SHELF,
        EQ_FILTER_PEAK,
        EQ_FILTER_HIGH_SHELF,
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    {
        struct eq_band_setting setting = { 0 };
        assert(eq_config_parse_legacy("80, 9, -12", types[i], &setting));
        assert(setting.cutoff == 80);
        assert(setting.q == 9);
        assert(setting.gain == -12);
        assert(setting.type == types[i]);
    }

    struct eq_band_setting sentinel = {
        .cutoff = 1,
        .q = 2,
        .gain = 3,
        .type = EQ_FILTER_PEAK,
    };
    struct eq_band_setting actual = sentinel;
    assert(!eq_config_parse_legacy("80, 9", EQ_FILTER_LOW_SHELF, &actual));
    assert(setting_equals(&actual, &sentinel));
    assert(!eq_config_parse_legacy("80, 9, -12, PEAK",
                                   EQ_FILTER_PEAK, &actual));
    assert(setting_equals(&actual, &sentinel));
    assert(!eq_config_parse_legacy("80, 9, -12",
                                   EQ_FILTER_TYPE_COUNT, &actual));
    assert(setting_equals(&actual, &sentinel));

    assert(eq_config_parse_legacy("80, 0, -12",
                                  EQ_FILTER_PEAK, &actual));
    assert(actual.q == EQ_Q_MIN);

    actual = sentinel;
    assert(!eq_config_parse_legacy("19, 7, 0",
                                   EQ_FILTER_LOW_SHELF, &actual));
    assert(setting_equals(&actual, &sentinel));
    assert(!eq_config_parse_legacy("80, 65, 0",
                                   EQ_FILTER_PEAK, &actual));
    assert(setting_equals(&actual, &sentinel));
    assert(!eq_config_parse_legacy("80, 7, 241",
                                   EQ_FILTER_HIGH_SHELF, &actual));
    assert(setting_equals(&actual, &sentinel));
}

static void test_malformed_values(void)
{
    static const char *const malformed[] = {
        "",
        "32, 7, 0",
        "32, 7, 0,",
        "32, 7, 0, NOTCH",
        "32, 7, 0, PEAK, extra",
        "32, seven, 0, PEAK",
        "32Hz, 7, 0, PEAK",
        "19, 7, 0, PEAK",
        "22041, 7, 0, PEAK",
        "32, 0, 0, PEAK",
        "32, 65, 0, PEAK",
        "32, 7, -241, PEAK",
        "32, 7, 241, PEAK",
        "2147483648, 7, 0, PEAK",
        "-2147483649, 7, 0, PEAK",
        "2147483647, 1, -2147483648, PEAK",
        "32, 7, 0, PEAK trailing",
    };

    for (size_t i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
        assert_parse_failure_is_atomic(malformed[i]);

    struct eq_band_setting setting = { 0 };
    assert(!eq_config_parse(NULL, &setting));
    assert(!eq_config_parse("32, 7, 0, PEAK", NULL));
}

static void test_round_trip(void)
{
    const struct eq_band_setting expected = {
        .cutoff = 105,
        .q = 7,
        .gain = -16,
        .type = EQ_FILTER_LOW_SHELF,
    };
    struct eq_band_setting actual = { 0 };
    char buffer[64];
    char short_buffer[8];

    assert(eq_config_format(&expected, buffer, sizeof(buffer)));
    assert(strcmp(buffer, "105, 7, -16, LOW_SHELF") == 0);
    assert(eq_config_parse(buffer, &actual));
    assert(setting_equals(&actual, &expected));

    assert(!eq_config_format(&expected, short_buffer, sizeof(short_buffer)));
    assert(!eq_config_format(NULL, buffer, sizeof(buffer)));
    assert(!eq_config_format(&expected, NULL, sizeof(buffer)));

    struct eq_band_setting invalid = expected;
    invalid.type = EQ_FILTER_TYPE_COUNT;
    assert(!eq_config_format(&invalid, buffer, sizeof(buffer)));

    invalid = expected;
    invalid.gain = EQ_GAIN_MAX + 1;
    assert(!eq_config_format(&invalid, buffer, sizeof(buffer)));
}

int main(void)
{
    /* Skins address cutoff/Q/gain as integer offsets 0/1/2. */
    assert(offsetof(struct eq_band_setting, cutoff) == 0);
    assert(offsetof(struct eq_band_setting, q) == sizeof(int));
    assert(offsetof(struct eq_band_setting, gain) == 2 * sizeof(int));
    assert(offsetof(struct eq_band_setting, type) == 3 * sizeof(int));

    test_canonical_values();
    test_legacy_migration();
    test_malformed_values();
    test_round_trip();

    puts("EQ config tests passed");
    return 0;
}
