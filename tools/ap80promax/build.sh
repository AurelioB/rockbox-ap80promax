#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
ap80_toolchain_bin=${AP80_TOOLCHAIN_BIN:-"$ap80_workspace_root/toolchain/bin"}
ap80_normal_build=${AP80_NORMAL_BUILD:-"$ap80_rockbox_root/build-ap80"}
ap80_boot_build=${AP80_BOOT_BUILD:-"$ap80_rockbox_root/build-ap80-boot"}
ap80_dist=${AP80_DIST_DIR:-"$ap80_workspace_root/dist"}
ap80_jobs=${AP80_JOBS:-$(getconf _NPROCESSORS_ONLN)}

if [ ! -x "$ap80_toolchain_bin/mipsel-rockbox-linux-gnu-gcc" ]; then
    echo "Missing Rockbox MIPS toolchain at $ap80_toolchain_bin" >&2
    echo "Build target y with tools/rockboxdev.sh first." >&2
    exit 1
fi

PATH="$ap80_toolchain_bin:$PATH"
export PATH

mkdir -p "$ap80_normal_build" "$ap80_boot_build" "$ap80_dist"

(
    cd "$ap80_normal_build"
    "$ap80_rockbox_root/tools/configure" \
        --target=hidizsap80promax \
        --type=n \
        --no-ccache

    # Serialize capability and language generation before the parallel build.
    # Otherwise an existing build directory can race lang_core.c after target
    # feature changes.
    make "$ap80_normal_build/firmware/common/config.o"
    make "$ap80_normal_build/apps/genlang-features"
    make -j"$ap80_jobs"
    make zip
)

(
    cd "$ap80_boot_build"
    "$ap80_rockbox_root/tools/configure" \
        --target=hidizsap80promax \
        --type=b \
        --no-ccache
    make -j"$ap80_jobs"
)

cp "$ap80_normal_build/rockbox.ap80promax" "$ap80_dist/"
cp "$ap80_normal_build/rockbox.zip" "$ap80_dist/rockbox-ap80promax.zip"
cp "$ap80_boot_build/bootloader.ap80promax" "$ap80_dist/"
cp "$ap80_rockbox_root/tools/ap80promax/update-usb.sh" "$ap80_dist/"
cp "$ap80_rockbox_root/tools/ap80promax/update-device.sh" "$ap80_dist/"

(
    cd "$ap80_dist"
    sha256sum \
        rockbox.ap80promax \
        rockbox-ap80promax.zip \
        bootloader.ap80promax \
        update-usb.sh \
        update-device.sh > SHA256SUMS
)

echo "Device artifacts written to $ap80_dist"
"$ap80_rockbox_root/tools/ap80promax/verify.sh"
