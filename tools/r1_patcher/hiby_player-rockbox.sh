#!/bin/sh

BOOTLOADER=${RB_BOOTLOADER:-/usr/bin/bootloader.rb}
READY=${RB_READY:-/tmp/rockbox-bootloader-ready}
HANDOFF=${RB_HANDOFF:-/tmp/rockbox-handoff-started}
APP_READY=${RB_APP_READY:-/tmp/rockbox-app-ready}
SD_ROOT=${RB_SD_ROOT:-/data/mnt/sd_0}
BOOT_SUCCESS=${RB_BOOT_SUCCESS:-$SD_ROOT/.rockbox/rb_bl_success}
BOOT_ATTEMPT=${RB_BOOT_ATTEMPT:-$SD_ROOT/.rockbox/rb_bl_attempt}
GRACEFUL_EXIT=${RB_GRACEFUL_EXIT:-/tmp/rockbox-graceful-exit}
HIBY_PLAYER=${RB_HIBY_PLAYER:-/usr/bin/hiby_player}
ADB_INIT=${RB_ADB_INIT:-/etc/init.d/T90adb}
KILLALL=${RB_KILLALL:-killall}
SLEEP=${RB_SLEEP:-sleep}
WATCHDOG_SECONDS=${RB_WATCHDOG_SECONDS:-15}
HANDOFF_WATCHDOG_SECONDS=${RB_HANDOFF_WATCHDOG_SECONDS:-60}
FORCE_HIBY="$SD_ROOT/rockbox-force-hiby"
LOG=${RB_LOG:-/tmp/rockbox-bootloader.log}

start_hiby()
{
    rm -f "$READY" "$HANDOFF" "$APP_READY" "$GRACEFUL_EXIT"
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

rm -f "$READY" "$HANDOFF" "$APP_READY" "$GRACEFUL_EXIT"
"$BOOTLOADER" >>"$LOG" 2>&1 &
BOOTLOADER_PID=$!

# Supervise both initialization and the bootloader-to-Rockbox handoff. Once
# the app reports ready, keep watching only for an unexpected process exit.
STATE=initializing
COUNT=0
while :; do
    if ! kill -0 "$BOOTLOADER_PID" 2>/dev/null; then
        if [ ! -f "$GRACEFUL_EXIT" ]; then
            # Any unexpected selector/application exit revokes old health.
            rm -f "$BOOT_SUCCESS"
        fi
        wait "$BOOTLOADER_PID"
        start_hiby
    fi

    if [ -f "$APP_READY" ]; then
        if [ "$STATE" != running ] && [ -d "$SD_ROOT/.rockbox" ]; then
            # A successful launch records health, but the boot attempt remains
            # armed until Rockbox shuts down cleanly. A crash or hard power
            # cut therefore cannot authorize another fast handoff.
            if touch "$BOOT_SUCCESS"; then
                :
            else
                echo "Could not persist Rockbox boot success" >&2
            fi
        fi
        STATE=running
        COUNT=0
    elif [ -f "$HANDOFF" ]; then
        if [ "$STATE" != handoff ]; then
            STATE=handoff
            COUNT=0
        fi
        if [ "$COUNT" -ge "$HANDOFF_WATCHDOG_SECONDS" ]; then
            echo "Rockbox app handoff timeout; starting HiBy" >>"$LOG"
            rm -f "$BOOT_SUCCESS"
            break
        fi
        COUNT=$((COUNT + 1))
    elif [ -f "$READY" ]; then
        STATE=menu
        COUNT=0
    else
        if [ "$COUNT" -ge "$WATCHDOG_SECONDS" ]; then
            echo "Rockbox bootloader readiness timeout; starting HiBy" >>"$LOG"
            rm -f "$BOOT_SUCCESS"
            break
        fi
        COUNT=$((COUNT + 1))
    fi

    "$SLEEP" 1
done

kill "$BOOTLOADER_PID" >/dev/null 2>&1
"$SLEEP" 1
kill -9 "$BOOTLOADER_PID" >/dev/null 2>&1
wait "$BOOTLOADER_PID" 2>/dev/null
start_hiby
