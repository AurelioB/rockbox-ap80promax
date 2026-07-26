#!/bin/sh
set -eu

ap80_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ap80_launcher="$ap80_repo/tools/r1_patcher/hiby_player-rockbox.sh"
ap80_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ap80-recovery.XXXXXX")
trap 'rm -rf "$ap80_test_dir"' EXIT HUP INT TERM

mkdir -p "$ap80_test_dir/sd"

cat > "$ap80_test_dir/hiby-player" <<'EOF'
#!/bin/sh
: > "$RB_TEST_HIBY_STARTED"
EOF
cat > "$ap80_test_dir/noop" <<'EOF'
#!/bin/sh
exit 0
EOF
cat > "$ap80_test_dir/bootloader-exits" <<'EOF'
#!/bin/sh
: > "$RB_TEST_BOOTLOADER_STARTED"
exit 1
EOF
cat > "$ap80_test_dir/bootloader-hangs" <<'EOF'
#!/bin/sh
: > "$RB_TEST_BOOTLOADER_STARTED"
while :; do :; done
EOF
cat > "$ap80_test_dir/bootloader-handoff-hangs" <<'EOF'
#!/bin/sh
: > "$RB_TEST_BOOTLOADER_STARTED"
: > "$RB_READY"
: > "$RB_HANDOFF"
while :; do :; done
EOF
chmod 0755 "$ap80_test_dir/hiby-player" "$ap80_test_dir/noop" \
    "$ap80_test_dir/bootloader-exits" "$ap80_test_dir/bootloader-hangs" \
    "$ap80_test_dir/bootloader-handoff-hangs"

run_launcher()
{
    RB_SD_ROOT="$ap80_test_dir/sd" \
    RB_READY="$ap80_test_dir/ready" \
    RB_HANDOFF="$ap80_test_dir/handoff" \
    RB_APP_READY="$ap80_test_dir/app-ready" \
    RB_HIBY_PLAYER="$ap80_test_dir/hiby-player" \
    RB_KILLALL="$ap80_test_dir/noop" \
    RB_ADB_INIT="$ap80_test_dir/noop" \
    RB_SLEEP="$ap80_test_dir/noop" \
    RB_LOG="$ap80_test_dir/bootloader.log" \
    RB_WATCHDOG_SECONDS=1 \
    RB_HANDOFF_WATCHDOG_SECONDS=1 \
    RB_TEST_HIBY_STARTED="$ap80_test_dir/hiby-started" \
    RB_TEST_BOOTLOADER_STARTED="$ap80_test_dir/bootloader-started" \
        "$ap80_launcher"
}

# A user-created SD marker must bypass the Rockbox process completely.
: > "$ap80_test_dir/sd/rockbox-force-hiby"
RB_BOOTLOADER="$ap80_test_dir/bootloader-exits" run_launcher
test -f "$ap80_test_dir/hiby-started"
test ! -e "$ap80_test_dir/bootloader-started"

# An early bootloader exit must invoke the untouched vendor player.
rm -f "$ap80_test_dir/sd/rockbox-force-hiby" \
    "$ap80_test_dir/hiby-started"
RB_BOOTLOADER="$ap80_test_dir/bootloader-exits" run_launcher
test -f "$ap80_test_dir/bootloader-started"
test -f "$ap80_test_dir/hiby-started"

# A hung bootloader must be killed after the watchdog interval and fall back.
rm -f "$ap80_test_dir/bootloader-started" "$ap80_test_dir/hiby-started"
RB_BOOTLOADER="$ap80_test_dir/bootloader-hangs" run_launcher
test -f "$ap80_test_dir/bootloader-started"
test -f "$ap80_test_dir/hiby-started"
grep -q 'readiness timeout' "$ap80_test_dir/bootloader.log"

# A hang after menu initialization but before Rockbox finishes starting must
# also be killed and returned to the vendor player.
rm -f "$ap80_test_dir/bootloader-started" "$ap80_test_dir/hiby-started" \
    "$ap80_test_dir/bootloader.log"
RB_BOOTLOADER="$ap80_test_dir/bootloader-handoff-hangs" run_launcher
test -f "$ap80_test_dir/bootloader-started"
test -f "$ap80_test_dir/hiby-started"
grep -q 'app handoff timeout' "$ap80_test_dir/bootloader.log"

grep -q '#define LCD_WIDTH 360' \
    "$ap80_repo/firmware/export/config/hidizsap80promax.h"
grep -q '#define LCD_FBDEV_WIDTH 480' \
    "$ap80_repo/firmware/export/config/hidizsap80promax.h"
grep -q '#define TOUCHSCREEN_RAW_WIDTH 360' \
    "$ap80_repo/firmware/export/config/hidizsap80promax.h"
grep -q 'vinfo.xres != LCD_FBDEV_WIDTH' \
    "$ap80_repo/firmware/target/hosted/lcd-linuxfb.c"
grep -q 'dst += LCD_FRAMEBUFFER_STRIDE' \
    "$ap80_repo/firmware/target/hosted/lcd-linuxfb.c"
grep -q 'system_notify_boot_complete' \
    "$ap80_repo/apps/main.c"
grep -q 'rockbox-handoff-started' \
    "$ap80_repo/bootloader/hibyos_linux.c"

echo "AP80 recovery launcher tests passed"
