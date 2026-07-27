#!/bin/sh
set -eu

ap80_repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ap80_updater="$ap80_repo/tools/ap80promax/update-device.sh"
ap80_host_updater="$ap80_repo/tools/ap80promax/update-usb.sh"
ap80_fake_adb="$ap80_repo/tests/ap80promax_fake_adb.sh"
ap80_test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ap80-usb-update.XXXXXX")
trap 'rm -rf "$ap80_test_dir"' EXIT HUP INT TERM

ap80_root="$ap80_test_dir/sd"
ap80_markers="$ap80_test_dir/markers"
ap80_package="$ap80_test_dir/package"
mkdir -p "$ap80_root/.rockbox" "$ap80_markers" "$ap80_package/.rockbox"

printf '%s\n' \
    'Target: hidizsap80promax' \
    'Target id: 125' \
    'Version: old' > "$ap80_root/.rockbox/rockbox-info.txt"
printf 'old binary\n' > "$ap80_root/.rockbox/rockbox.ap80promax"
printf 'volume: -30\n' > "$ap80_root/.rockbox/config.cfg"
printf 'custom theme\n' > "$ap80_root/.rockbox/custom-theme.cfg"
: > "$ap80_root/.rockbox/rb_bl_success"

printf '%s\n' \
    'Target: hidizsap80promax' \
    'Target id: 125' \
    'Version: new' > "$ap80_package/.rockbox/rockbox-info.txt"
printf 'new binary\n' > "$ap80_package/.rockbox/rockbox.ap80promax"
printf 'new release file\n' > "$ap80_package/.rockbox/release-file.txt"
: > "$ap80_package/.rockbox/rb_bl_success"
: > "$ap80_package/.rockbox/rb_bl_attempt"

(
    cd "$ap80_package"
    zip -qr "$ap80_root/update.zip" .rockbox
)

AP80_UPDATE_ROOT="$ap80_root" \
AP80_UPDATE_MARKER_DIR="$ap80_markers" \
    "$ap80_updater" "$ap80_root/update.zip"

grep -qx 'new binary' "$ap80_root/.rockbox/rockbox.ap80promax"
grep -qx 'volume: -30' "$ap80_root/.rockbox/config.cfg"
grep -qx 'custom theme' "$ap80_root/.rockbox/custom-theme.cfg"
grep -qx 'new release file' "$ap80_root/.rockbox/release-file.txt"
grep -qx 'old binary' "$ap80_root/.rockbox-backup/rockbox.ap80promax"
test ! -e "$ap80_root/.rockbox/rb_bl_success"
test ! -e "$ap80_root/.rockbox/rb_bl_attempt"
test -e "$ap80_root/.rockbox-backup/rb_bl_success"
test -f "$ap80_markers/rockbox-usb-update-ok"
test ! -e "$ap80_root/update.zip"

AP80_UPDATE_ROOT="$ap80_root" \
AP80_UPDATE_MARKER_DIR="$ap80_markers" \
    "$ap80_updater" --rollback

grep -qx 'old binary' "$ap80_root/.rockbox/rockbox.ap80promax"
grep -qx 'new binary' "$ap80_root/.rockbox-failed/rockbox.ap80promax"
test -e "$ap80_root/.rockbox/rb_bl_success"

rm -rf "$ap80_package"
mkdir -p "$ap80_package/.rockbox"
printf '%s\n' \
    'Target: hibyr1' \
    'Target id: 121' > "$ap80_package/.rockbox/rockbox-info.txt"
printf 'wrong binary\n' > "$ap80_package/.rockbox/rockbox.ap80promax"
(
    cd "$ap80_package"
    zip -qr "$ap80_root/wrong-target.zip" .rockbox
)

if AP80_UPDATE_ROOT="$ap80_root" \
   AP80_UPDATE_MARKER_DIR="$ap80_markers" \
       "$ap80_updater" "$ap80_root/wrong-target.zip"; then
    echo "Wrong-target update unexpectedly succeeded" >&2
    exit 1
fi

grep -qx 'old binary' "$ap80_root/.rockbox/rockbox.ap80promax"
test -f "$ap80_markers/rockbox-usb-update-error"

printf 'not a zip\n' > "$ap80_root/damaged.zip"
if AP80_UPDATE_ROOT="$ap80_root" \
   AP80_UPDATE_MARKER_DIR="$ap80_markers" \
       "$ap80_updater" "$ap80_root/damaged.zip"; then
    echo "Damaged update unexpectedly succeeded" >&2
    exit 1
fi

grep -qx 'old binary' "$ap80_root/.rockbox/rockbox.ap80promax"
echo "AP80 USB update tests passed"

rm -rf "$ap80_root" "$ap80_package" "$ap80_markers"
mkdir -p "$ap80_root/.rockbox" "$ap80_package/.rockbox" "$ap80_markers"
printf '%s\n' \
    'Target: hidizsap80promax' \
    'Target id: 125' \
    'Version: host-old' > "$ap80_root/.rockbox/rockbox-info.txt"
printf 'host old binary\n' > "$ap80_root/.rockbox/rockbox.ap80promax"
printf 'host setting\n' > "$ap80_root/.rockbox/config.cfg"
: > "$ap80_root/.rockbox/rb_bl_success"
printf '%s\n' \
    'Target: hidizsap80promax' \
    'Target id: 125' \
    'Version: host-new' > "$ap80_package/.rockbox/rockbox-info.txt"
printf 'host new binary\n' > "$ap80_package/.rockbox/rockbox.ap80promax"
(
    cd "$ap80_package"
    zip -qr "$ap80_test_dir/host-update.zip" .rockbox
)

AP80_ADB="$ap80_fake_adb" \
AP80_FAKE_ROOT="$ap80_root" \
AP80_FAKE_TMP="$ap80_markers" \
    "$ap80_host_updater" --zip "$ap80_test_dir/host-update.zip"
grep -qx 'host new binary' "$ap80_root/.rockbox/rockbox.ap80promax"
grep -qx 'host setting' "$ap80_root/.rockbox/config.cfg"
test ! -e "$ap80_root/.rockbox/rb_bl_success"

AP80_ADB="$ap80_fake_adb" \
AP80_FAKE_ROOT="$ap80_root" \
AP80_FAKE_TMP="$ap80_markers" \
    "$ap80_host_updater" --rollback
grep -qx 'host old binary' "$ap80_root/.rockbox/rockbox.ap80promax"
test -e "$ap80_root/.rockbox/rb_bl_success"
echo "AP80 USB host workflow tests passed"
