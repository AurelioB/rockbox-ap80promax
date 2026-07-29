#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_warble_build=${AP80_WARBLE_BUILD:-"$ap80_rockbox_root/build-warble-ap80"}
ap80_jobs=${AP80_JOBS:-$(getconf _NPROCESSORS_ONLN)}
ap80_host_cc=${HOSTCC:-cc}

mkdir -p "$ap80_warble_build"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -DKINETIC_SCROLL_HOST_TEST \
    -DKINETIC_SCROLL_ACCEL_A0_Q8=81920 \
    -DKINETIC_SCROLL_BRAKE_A0_Q8=81920 \
    -DKINETIC_SCROLL_DECEL_A0_Q8=819200 \
    -DKINETIC_SCROLL_PRESS_A0_Q8=14336000 \
    -DKINETIC_SCROLL_STOP_VELOCITY_Q8=1280 \
    -I"$ap80_rockbox_root/apps/gui" \
    "$ap80_rockbox_root/tests/test_kinetic_scroll.c" \
    "$ap80_rockbox_root/apps/gui/kinetic_scroll.c" \
    -o "$ap80_warble_build/test-kinetic-scroll"
"$ap80_warble_build/test-kinetic-scroll"

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

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -I"$ap80_rockbox_root/lib/rbcodec/codecs/lib" \
    "$ap80_rockbox_root/tests/test_dsd2pcm.c" \
    "$ap80_rockbox_root/lib/rbcodec/codecs/lib/dsd2pcm.c" \
    -o "$ap80_warble_build/test-dsd2pcm"
"$ap80_warble_build/test-dsd2pcm"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -pthread \
    -I"$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax" \
    "$ap80_rockbox_root/tests/test_ap80promax_services.c" \
    "$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/services-ap80promax.c" \
    -o "$ap80_warble_build/test-ap80-services"
"$ap80_warble_build/test-ap80-services"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -pthread -DAP80_WIFI_TEST \
    -I"$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax" \
    "$ap80_rockbox_root/tests/test_ap80promax_wifi.c" \
    "$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/wifi-ap80promax.c" \
    "$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/services-ap80promax.c" \
    -o "$ap80_warble_build/test-ap80-wifi"
"$ap80_warble_build/test-ap80-wifi"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -pthread -DSIMULATOR \
    -I"$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax" \
    "$ap80_rockbox_root/tests/test_ap80promax_wifi.c" \
    "$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/wifi-ap80promax.c" \
    -o "$ap80_warble_build/test-ap80-wifi-simulator"
"$ap80_warble_build/test-ap80-wifi-simulator"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -pthread -DAP80_BLUETOOTH_TEST \
    -DAP80_BT_ROUTE_MONITOR_MS=20 -DAP80_BT_ROUTE_MISSES=1 \
    -I"$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax" \
    "$ap80_rockbox_root/tests/test_ap80promax_bluetooth.c" \
    "$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/bluetooth-ap80promax.c" \
    -o "$ap80_warble_build/test-ap80-bluetooth"
"$ap80_warble_build/test-ap80-bluetooth"

"$ap80_host_cc" -std=gnu11 -Wall -Wextra -Werror \
    -pthread -DSIMULATOR \
    -I"$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax" \
    "$ap80_rockbox_root/tests/test_ap80promax_bluetooth.c" \
    "$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/bluetooth-ap80promax.c" \
    -o "$ap80_warble_build/test-ap80-bluetooth-simulator"
"$ap80_warble_build/test-ap80-bluetooth-simulator"

"$ap80_rockbox_root/tests/test_ap80promax_controls.sh"
"$ap80_rockbox_root/tests/test_ap80promax_onebit_art.sh"
"$ap80_rockbox_root/tests/test_ap80promax_snappy_snarygrey.sh"
"$ap80_rockbox_root/tests/test_ap80promax_usb_update.sh"
"$ap80_rockbox_root/tests/test_ap80promax_recovery.sh"

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
