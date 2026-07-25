#!/bin/sh

BOOTLOADER=${RB_BOOTLOADER:-/usr/bin/bootloader.rb}
READY=${RB_READY:-/tmp/rockbox-bootloader-ready}
SD_ROOT=${RB_SD_ROOT:-/data/mnt/sd_0}
HIBY_PLAYER=${RB_HIBY_PLAYER:-/usr/bin/hiby_player}
ADB_INIT=${RB_ADB_INIT:-/etc/init.d/T90adb}
KILLALL=${RB_KILLALL:-killall}
SLEEP=${RB_SLEEP:-sleep}
WATCHDOG_SECONDS=${RB_WATCHDOG_SECONDS:-15}
FORCE_HIBY="$SD_ROOT/rockbox-force-hiby"
LOG=${RB_LOG:-/tmp/rockbox-bootloader.log}

start_hiby()
{
    rm -f "$READY"
    exec "$HIBY_PLAYER"
}

"$KILLALL" hiby_player >/dev/null 2>&1
"$KILLALL" -9 hiby_player >/dev/null 2>&1
"$KILLALL" bootloader.rb >/dev/null 2>&1
"$KILLALL" -9 bootloader.rb >/dev/null 2>&1

# A visible empty file named rockbox-force-hiby on the SD card permanently
# bypasses Rockbox until the user removes it.
if [ -f "$FORCE_HIBY" ]; then
    start_hiby
fi

# Start ADB before Rockbox touches the framebuffer. This is intentionally
# enabled for recovery builds so a failed boot can still be inspected by USB.
if [ -x "$ADB_INIT" ]; then
    "$ADB_INIT" start >/tmp/rockbox-adb.log 2>&1 &
fi

if [ -z "${RB_LOG:-}" ] && [ -d "$SD_ROOT" ]; then
    LOG="$SD_ROOT/rockbox-bootloader.log"
fi

rm -f "$READY"
"$BOOTLOADER" >>"$LOG" 2>&1 &
BOOTLOADER_PID=$!

# The bootloader creates READY only after its system, LCD, font, button and
# backlight initialization succeeds.
COUNT=0
while [ "$COUNT" -lt "$WATCHDOG_SECONDS" ]; do
    if [ -f "$READY" ]; then
        wait "$BOOTLOADER_PID"
        start_hiby
    fi

    if ! kill -0 "$BOOTLOADER_PID" 2>/dev/null; then
        wait "$BOOTLOADER_PID"
        start_hiby
    fi

    "$SLEEP" 1
    COUNT=$((COUNT + 1))
done

kill "$BOOTLOADER_PID" >/dev/null 2>&1
"$SLEEP" 1
kill -9 "$BOOTLOADER_PID" >/dev/null 2>&1
wait "$BOOTLOADER_PID" 2>/dev/null
echo "Rockbox bootloader readiness timeout; starting HiBy" >>"$LOG"
start_hiby
