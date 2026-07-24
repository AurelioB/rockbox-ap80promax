#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
ap80_dist=${AP80_DIST_DIR:-"$ap80_workspace_root/dist"}
ap80_input_update=${AP80_STOCK_UPDATE:-"$ap80_workspace_root/ap80max.upt"}
ap80_bootloader="$ap80_dist/bootloader.ap80promax"
ap80_patcher="$ap80_rockbox_root/tools/r1_patcher/r1_patcher.sh"
ap80_package_work="$ap80_dist/patcher"
ap80_output="$ap80_dist/ap80max_rb.upt"

for ap80_required in \
    "$ap80_input_update" \
    "$ap80_bootloader" \
    "$ap80_patcher"
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

(
    cd "$ap80_dist"
    sha256sum ap80max_rb.upt > SHA256SUMS-updater
)

echo "Patched engineering updater written to $ap80_output"
AP80_REQUIRE_UPDATER=1 "$ap80_rockbox_root/tools/ap80promax/verify.sh"
