#include <assert.h>

#ifndef TEST_HW_SAMPR_CAPS
#define TEST_HW_SAMPR_CAPS 0xffffu
#endif

#define HW_SAMPR_CAPS TEST_HW_SAMPR_CAPS
#include "pcm_sampr.h"

/* Existing master-list indexes and capability bits are persistent ABI. */
_Static_assert(FREQ_192 == 0, "FREQ_192 index changed");
_Static_assert(FREQ_176 == 1, "FREQ_176 index changed");
_Static_assert(FREQ_44 == 6, "FREQ_44 index changed");
_Static_assert(FREQ_8 == 13, "FREQ_8 index changed");
_Static_assert(SAMPR_CAP_ALL_192 == 0x3fffu,
               "legacy sample-rate capability bits changed");

_Static_assert(FREQ_384 == 14, "FREQ_384 must use an appended index");
_Static_assert(FREQ_352 == 15, "FREQ_352 must use an appended index");
_Static_assert(SAMPR_CAP_ALL_384 == 0xffffu,
               "extended sample-rate capability mask is incomplete");

#if TEST_HW_SAMPR_CAPS == 0x3fffu
/* A legacy target must retain its compact hardware enumeration exactly. */
_Static_assert(HW_FREQ_192 == 0, "legacy HW_FREQ_192 changed");
_Static_assert(HW_FREQ_176 == 1, "legacy HW_FREQ_176 changed");
_Static_assert(HW_FREQ_44 == 6, "legacy HW_FREQ_44 changed");
_Static_assert(HW_FREQ_8 == 13, "legacy HW_FREQ_8 changed");
_Static_assert(HW_NUM_FREQ == 14, "legacy HW_NUM_FREQ changed");
#elif TEST_HW_SAMPR_CAPS == 0xffffu
/* Extended targets still expose their hardware list in descending order. */
_Static_assert(HW_FREQ_384 == 0, "HW_FREQ_384 is not first");
_Static_assert(HW_FREQ_352 == 1, "HW_FREQ_352 is not second");
_Static_assert(HW_FREQ_192 == 2, "HW_FREQ_192 ordering is wrong");
_Static_assert(HW_FREQ_44 == 8, "HW_FREQ_44 ordering is wrong");
_Static_assert(HW_NUM_FREQ == 16, "extended HW_NUM_FREQ is wrong");
#else
#error "unsupported TEST_HW_SAMPR_CAPS"
#endif

int main(void)
{
    assert(SAMPR_384 == 384000);
    assert(SAMPR_352 == 352800);
    return 0;
}
