#!/bin/sh
set -eu

ap80_update_root=${AP80_UPDATE_ROOT:-/data/mnt/sd_0}
ap80_marker_dir=${AP80_UPDATE_MARKER_DIR:-/tmp}
ap80_current="$ap80_update_root/.rockbox"
ap80_backup="$ap80_update_root/.rockbox-backup"
ap80_failed="$ap80_update_root/.rockbox-failed"
ap80_stage="$ap80_update_root/.rockbox-update"
ap80_success_marker="$ap80_marker_dir/rockbox-usb-update-ok"
ap80_error_marker="$ap80_marker_dir/rockbox-usb-update-error"

case "$ap80_update_root" in
    ""|/|/data|/data/mnt)
        echo "Refusing unsafe update root: $ap80_update_root" >&2
        exit 1
        ;;
esac

ap80_fail()
{
    echo "$*" >&2
    mkdir -p "$ap80_marker_dir"
    printf '%s\n' "$*" > "$ap80_error_marker"
    exit 1
}

ap80_validate_install()
{
    ap80_install=$1
    ap80_info="$ap80_install/rockbox-info.txt"

    [ -d "$ap80_install" ] ||
        ap80_fail "Missing Rockbox directory: $ap80_install"
    [ -s "$ap80_install/rockbox.ap80promax" ] ||
        ap80_fail "Missing AP80 Pro Max executable"
    [ -f "$ap80_info" ] ||
        ap80_fail "Missing rockbox-info.txt"
    grep -qx 'Target: hidizsap80promax' "$ap80_info" ||
        ap80_fail "Release target is not hidizsap80promax"
    grep -qx 'Target id: 125' "$ap80_info" ||
        ap80_fail "Release target ID is not 125"
}

mkdir -p "$ap80_marker_dir"
rm -f "$ap80_success_marker" "$ap80_error_marker"

if [ "$ap80_update_root" = /data/mnt/sd_0 ]; then
    for ap80_executable in /proc/[0-9]*/exe; do
        [ "$(readlink "$ap80_executable" 2>/dev/null || :)" != \
          /tmp/rockbox.ap80promax ] ||
            ap80_fail "Restart into TOOLS -> USB Rockbox update first"
    done
fi

if [ "${1:-}" = "--rollback" ]; then
    ap80_validate_install "$ap80_backup"
    rm -rf "$ap80_failed"

    if [ -d "$ap80_current" ]; then
        mv "$ap80_current" "$ap80_failed" ||
            ap80_fail "Could not preserve the current installation"
    fi

    if ! mv "$ap80_backup" "$ap80_current"; then
        [ ! -d "$ap80_failed" ] || mv "$ap80_failed" "$ap80_current"
        ap80_fail "Could not restore the backup installation"
    fi

    sync
    touch "$ap80_success_marker"
    echo "Rockbox rollback complete"
    exit 0
fi

ap80_archive=${1:-}
[ -n "$ap80_archive" ] || ap80_fail "Usage: update-device.sh ARCHIVE.zip"
[ -s "$ap80_archive" ] || ap80_fail "Update archive is missing or empty"
ap80_validate_install "$ap80_current"

rm -rf "$ap80_stage"
mkdir -p "$ap80_stage"

# Seed the staging tree from the current installation so user configuration,
# custom themes, database files, and boot-menu preference survive an update.
cp -a "$ap80_current" "$ap80_stage/.rockbox" ||
    ap80_fail "Could not stage the current installation"

if ! unzip -oq "$ap80_archive" -d "$ap80_stage"; then
    rm -rf "$ap80_stage"
    ap80_fail "Could not extract the update archive"
fi

ap80_validate_install "$ap80_stage/.rockbox"
chmod 755 "$ap80_stage/.rockbox/rockbox.ap80promax" ||
    ap80_fail "Could not make the Rockbox executable runnable"

# The staging directory is on the same SD-card filesystem, so both renames
# are atomic. Keep the previous complete tree for an explicit USB rollback.
rm -rf "$ap80_backup"
mv "$ap80_current" "$ap80_backup" ||
    ap80_fail "Could not preserve the previous installation"

if ! mv "$ap80_stage/.rockbox" "$ap80_current"; then
    mv "$ap80_backup" "$ap80_current"
    rm -rf "$ap80_stage"
    ap80_fail "Could not activate the staged installation"
fi

rm -rf "$ap80_stage"
rm -f "$ap80_archive"
sync
touch "$ap80_success_marker"
echo "Rockbox USB update complete"
