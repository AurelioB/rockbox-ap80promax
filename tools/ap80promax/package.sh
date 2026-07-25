#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
ap80_dist=${AP80_DIST_DIR:-"$ap80_workspace_root/dist"}
ap80_input_update=${AP80_STOCK_UPDATE:-"$ap80_workspace_root/ap80max.upt"}
ap80_bootloader="$ap80_dist/bootloader.ap80promax"
ap80_patcher="$ap80_rockbox_root/tools/r1_patcher/r1_patcher.sh"
ap80_launcher="$ap80_rockbox_root/tools/r1_patcher/hiby_player-rockbox.sh"
ap80_package_work="$ap80_dist/patcher"
ap80_output="$ap80_dist/ap80max_rb.upt"

for ap80_required in \
    "$ap80_input_update" \
    "$ap80_bootloader" \
    "$ap80_patcher" \
    "$ap80_launcher"
do
    if [ ! -f "$ap80_required" ]; then
        echo "Missing required file: $ap80_required" >&2
        exit 1
    fi
done

mkdir -p "$ap80_package_work"
cp "$ap80_input_update" "$ap80_package_work/ap80max.upt"
cp "$ap80_bootloader" "$ap80_package_work/bootloader.ap80promax"

(
    cd "$ap80_package_work"
    "$ap80_patcher" ap80max.upt bootloader.ap80promax
)

cp "$ap80_package_work/ap80max_rb.upt" "$ap80_output"

# Inspect the updater that will actually be flashed, rather than trusting only
# the intermediate build files.
ap80_inspect=$(mktemp -d "${TMPDIR:-/tmp}/ap80-updater-inspect.XXXXXX")
trap 'rm -rf "$ap80_inspect"' EXIT HUP INT TERM
mkdir -p "$ap80_inspect/iso" "$ap80_inspect/rootfs"
7z -o"$ap80_inspect/iso" x "$ap80_output" >/dev/null
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

(
    cd "$ap80_dist"
    sha256sum ap80max_rb.upt > SHA256SUMS-updater
)

echo "Patched engineering updater written to $ap80_output"
echo "Embedded bootloader, launcher fallback, and stock launcher verified"
AP80_REQUIRE_UPDATER=1 "$ap80_rockbox_root/tools/ap80promax/verify.sh"
