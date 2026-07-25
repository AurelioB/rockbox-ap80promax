#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_sim_build=${AP80_SIM_BUILD:-"$ap80_rockbox_root/build-sim-ap80"}
ap80_checkwps_build=${AP80_CHECKWPS_BUILD:-"$ap80_rockbox_root/build-checkwps-ap80"}
ap80_sim_root=${AP80_SIM_ROOT:-"$ap80_sim_build/simdisk"}
ap80_jobs=${AP80_JOBS:-$(getconf _NPROCESSORS_ONLN)}

mkdir -p "$ap80_sim_build" "$ap80_checkwps_build" "$ap80_sim_root"

(
    cd "$ap80_sim_build"
    "$ap80_rockbox_root/tools/configure" \
        --target=hidizsap80promax \
        --type=s \
        --no-ccache
    make "$ap80_sim_build/firmware/common/config.o"
    make "$ap80_sim_build/apps/genlang-features"
    make -j"$ap80_jobs"
    make zip
)

# Preserve developer media and fixtures already present in simulated storage.
unzip -oq "$ap80_sim_build/rockbox.zip" -d "$ap80_sim_root"

(
    cd "$ap80_checkwps_build"
    "$ap80_rockbox_root/tools/configure" \
        --target=hidizsap80promax \
        --type=c \
        --no-ccache
    make "$ap80_checkwps_build/firmware/common/config.o"
    # checkwps does not track target capability changes strongly enough to
    # invalidate its generated language subset.
    make -B "$ap80_checkwps_build/apps/genlang-features"
    make -j"$ap80_jobs"
)

for ap80_skin in "$ap80_sim_root"/.rockbox/wps/OneBit*.wps \
                 "$ap80_sim_root"/.rockbox/wps/OneBit*.sbs
do
    "$ap80_checkwps_build/checkwps.hidizsap80promax" "$ap80_skin"
done

if [ "${AP80_SIM_BUILD_ONLY:-0}" = "1" ]; then
    echo "Simulator built at $ap80_sim_build/rockboxui"
    echo "Simulated storage prepared at $ap80_sim_root"
    exit 0
fi

if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    echo "No graphical display detected." >&2
    echo "For a headless smoke test, prefix this command with xvfb-run -a." >&2
    exit 1
fi

echo "Simulator storage: $ap80_sim_root"
echo "Keys: Up/Down or Left/Right, Enter/Space, Escape/Backspace, +/-"
echo "F5 writes a 480x640 screen dump into the simulator storage."

exec "$ap80_sim_build/rockboxui" \
    --root "$ap80_sim_root" \
    --nobackground \
    "$@"
