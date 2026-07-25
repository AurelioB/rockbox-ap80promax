#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_warble_build=${AP80_WARBLE_BUILD:-"$ap80_rockbox_root/build-warble-ap80"}
ap80_jobs=${AP80_JOBS:-$(getconf _NPROCESSORS_ONLN)}
ap80_host_cc=${HOSTCC:-cc}

mkdir -p "$ap80_warble_build"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -I"$ap80_rockbox_root/firmware/export" \
    -DTEST_HW_SAMPR_CAPS=0x3fffu \
    "$ap80_rockbox_root/tests/test_pcm_sampr.c" \
    -o "$ap80_warble_build/test-pcm-sampr-legacy"
"$ap80_warble_build/test-pcm-sampr-legacy"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -I"$ap80_rockbox_root/firmware/export" \
    -DTEST_HW_SAMPR_CAPS=0xffffu \
    "$ap80_rockbox_root/tests/test_pcm_sampr.c" \
    -o "$ap80_warble_build/test-pcm-sampr-extended"
"$ap80_warble_build/test-pcm-sampr-extended"

(
    cd "$ap80_warble_build"
    "$ap80_rockbox_root/tools/configure" \
        --target=hidizsap80promax \
        --type=w \
        --no-ccache
    make -j"$ap80_jobs"
)

"$ap80_rockbox_root/tests/run_ap80promax_dsd_tests.sh"
AP80_WARBLE_BUILD="$ap80_warble_build" \
    "$ap80_rockbox_root/tests/run_dsd_codec_tests.sh"

AP80_SIM_BUILD_ONLY=1 \
    "$ap80_rockbox_root/tools/ap80promax/simulator.sh"

echo "AP80 Pro Max host tests passed"
