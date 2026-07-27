#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
ap80_dist=${AP80_DIST_DIR:-"$ap80_workspace_root/dist"}
ap80_input_update=${AP80_STOCK_UPDATE:-"$ap80_workspace_root/ap80max.upt"}
ap80_normal_build=${AP80_NORMAL_BUILD:-"$ap80_rockbox_root/build-ap80"}
ap80_boot_build=${AP80_BOOT_BUILD:-"$ap80_rockbox_root/build-ap80-boot"}
ap80_application="$ap80_dist/rockbox.ap80promax"
ap80_zip="$ap80_dist/rockbox-ap80promax.zip"
ap80_bootloader="$ap80_dist/bootloader.ap80promax"
ap80_built_application="$ap80_normal_build/rockbox.ap80promax"
ap80_built_zip="$ap80_normal_build/rockbox.zip"
ap80_built_bootloader="$ap80_boot_build/bootloader.ap80promax"
ap80_patcher="$ap80_rockbox_root/tools/r1_patcher/r1_patcher.sh"
ap80_launcher="$ap80_rockbox_root/tools/r1_patcher/hiby_player-rockbox.sh"
ap80_audio_patcher="$ap80_rockbox_root/tools/ap80promax/patch-dop256-modules.py"
ap80_package_work="$ap80_dist/patcher"
ap80_output="$ap80_dist/ap80max_rb.upt"
ap80_checksum="$ap80_dist/SHA256SUMS-updater"
ap80_output_tmp="$ap80_dist/.ap80max_rb.upt.tmp"
ap80_checksum_tmp="$ap80_dist/.SHA256SUMS-updater.tmp"
ap80_candidate="$ap80_package_work/ap80max_rb.upt"
ap80_inspect=
ap80_package_complete=0

ap80_cleanup()
{
    [ -z "$ap80_inspect" ] || rm -rf "$ap80_inspect"
    rm -f "$ap80_output_tmp" "$ap80_checksum_tmp"
    if [ "$ap80_package_complete" -ne 1 ]; then
        rm -f "$ap80_output" "$ap80_checksum"
    fi
}

trap 'ap80_status=$?; ap80_cleanup; exit "$ap80_status"' 0
trap 'exit 1' HUP INT TERM

mkdir -p "$ap80_package_work"

# Never let a failed packaging attempt fall back to a previous updater.
rm -f "$ap80_output" "$ap80_checksum" "$ap80_output_tmp" \
      "$ap80_checksum_tmp" "$ap80_candidate"

for ap80_required in \
    "$ap80_input_update" \
    "$ap80_application" \
    "$ap80_zip" \
    "$ap80_bootloader" \
    "$ap80_dist/SHA256SUMS" \
    "$ap80_built_application" \
    "$ap80_built_zip" \
    "$ap80_built_bootloader" \
    "$ap80_patcher" \
    "$ap80_launcher" \
    "$ap80_audio_patcher"
do
    if [ ! -f "$ap80_required" ]; then
        echo "Missing required file: $ap80_required" >&2
        exit 1
    fi
done

ap80_require_build_current()
{
    ap80_build_dir=$1
    ap80_description=$2

    (cd "$ap80_build_dir" && make -q >/dev/null 2>&1) &&
        return 0

    echo "Current sources would rebuild the $ap80_description" >&2
    echo "Run tools/ap80promax/build.sh before package.sh" >&2
    exit 1
}

# Matching files are insufficient if both dist and the build directory are
# stale. Ask make whether either configured binary needs rebuilding. The
# Rockbox zip target is always out of date, so ZIP freshness is instead bound
# below by exact file comparison and verify.sh's embedded-application check.
ap80_require_build_current "$ap80_normal_build" "Rockbox application"
ap80_require_build_current "$ap80_boot_build" "bootloader"

# First validate the dist set as a release, without accepting an updater left
# by an earlier source state.
AP80_REQUIRE_UPDATER=0 AP80_LAUNCHER="$ap80_launcher" \
    "$ap80_rockbox_root/tools/ap80promax/verify.sh"

ap80_require_match()
{
    ap80_built_file=$1
    ap80_dist_file=$2
    ap80_description=$3

    if ! cmp "$ap80_built_file" "$ap80_dist_file" >/dev/null 2>&1; then
        echo "Dist $ap80_description does not match the current build output" >&2
        echo "Run tools/ap80promax/build.sh before package.sh" >&2
        exit 1
    fi
}

ap80_require_match "$ap80_built_application" "$ap80_application" \
    "Rockbox application"
ap80_require_match "$ap80_built_zip" "$ap80_zip" "Rockbox ZIP"
ap80_require_match "$ap80_built_bootloader" "$ap80_bootloader" \
    "bootloader"

cp "$ap80_input_update" "$ap80_package_work/ap80max.upt"
cp "$ap80_bootloader" "$ap80_package_work/bootloader.ap80promax"

(
    cd "$ap80_package_work"
    HIBY_ROOTFS_PATCHER="$ap80_audio_patcher" \
        "$ap80_patcher" ap80max.upt bootloader.ap80promax
)

[ -s "$ap80_candidate" ] || {
    echo "Patcher did not produce a non-empty engineering updater" >&2
    exit 1
}

# Inspect the updater that will actually be flashed, rather than trusting only
# the intermediate build files.
ap80_inspect=$(mktemp -d "${TMPDIR:-/tmp}/ap80-updater-inspect.XXXXXX")
mkdir -p "$ap80_inspect/iso" "$ap80_inspect/rootfs"
7z -o"$ap80_inspect/iso" x "$ap80_candidate" >/dev/null
cat "$ap80_inspect/iso/ota_v0"/rootfs.squashfs.* \
    > "$ap80_inspect/rootfs.squashfs"
unsquashfs -f -d "$ap80_inspect/rootfs" \
    "$ap80_inspect/rootfs.squashfs" >/dev/null

cmp "$ap80_bootloader" "$ap80_inspect/rootfs/usr/bin/bootloader.rb"
cmp "$ap80_launcher" "$ap80_inspect/rootfs/usr/bin/hiby_player.sh"
test -x "$ap80_inspect/rootfs/usr/bin/hiby_player.stock.sh"
strings "$ap80_inspect/rootfs/usr/bin/bootloader.rb" |
    grep -q '/tmp/rockbox-bootloader-ready'
strings "$ap80_inspect/rootfs/usr/bin/bootloader.rb" |
    grep -q '/tmp/rockbox-handoff-started'
strings "$ap80_inspect/rootfs/usr/bin/bootloader.rb" |
    grep -q '/.rockbox/rb_bl_success'
strings "$ap80_inspect/rootfs/usr/bin/bootloader.rb" |
    grep -q '/.rockbox/rb_bl_attempt'
if strings "$ap80_inspect/rootfs/usr/bin/bootloader.rb" |
    grep -q '/usr/bin/bt_suspend'
then
    echo "AP80 bootloader must not invoke the unbounded bt_suspend helper" >&2
    exit 1
fi
grep -q 'rockbox-force-hiby' \
    "$ap80_inspect/rootfs/usr/bin/hiby_player.sh"
grep -q 'readiness timeout' \
    "$ap80_inspect/rootfs/usr/bin/hiby_player.sh"
grep -q 'app handoff timeout' \
    "$ap80_inspect/rootfs/usr/bin/hiby_player.sh"
grep -q 'rb_bl_success' \
    "$ap80_inspect/rootfs/usr/bin/hiby_player.sh"
grep -q 'rb_bl_attempt' \
    "$ap80_inspect/rootfs/usr/bin/hiby_player.sh"
"$ap80_audio_patcher" "$ap80_inspect/rootfs"

cp "$ap80_candidate" "$ap80_output_tmp"
mv "$ap80_output_tmp" "$ap80_output"

(
    cd "$ap80_dist"
    sha256sum ap80max_rb.upt
) > "$ap80_checksum_tmp"
mv "$ap80_checksum_tmp" "$ap80_checksum"

echo "Patched engineering updater written to $ap80_output"
echo "Embedded bootloader, launcher fallback, and stock launcher verified"
if ! AP80_REQUIRE_UPDATER=1 AP80_LAUNCHER="$ap80_launcher" \
     "$ap80_rockbox_root/tools/ap80promax/verify.sh"
then
    echo "Completed updater failed release verification" >&2
    exit 1
fi

ap80_package_complete=1
