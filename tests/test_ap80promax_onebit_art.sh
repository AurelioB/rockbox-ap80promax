#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ap80_recolor="$ap80_rockbox_root/tools/ap80promax/recolor-onebit.pl"

"$ap80_recolor" --verify \
    "$ap80_rockbox_root/wps/OneBitMono" \
    "$ap80_rockbox_root/wps/OneBit_Orange" \
    ff6500 390c00
"$ap80_recolor" --verify \
    "$ap80_rockbox_root/wps/OneBitMono" \
    "$ap80_rockbox_root/wps/OneBit_Green" \
    4aff4a 001400

for ap80_variant in Orange Green
do
    case "$ap80_variant" in
        Orange) ap80_fg=ff6500; ap80_bg=390c00 ;;
        Green) ap80_fg=4aff4a; ap80_bg=001400 ;;
    esac

    "$ap80_recolor" --verify \
        "$ap80_rockbox_root/icons/modone_ap80.bmp" \
        "$ap80_rockbox_root/icons/modone_${ap80_variant}_ap80.bmp" \
        "$ap80_fg" "$ap80_bg"
    "$ap80_recolor" --verify \
        "$ap80_rockbox_root/icons/modone_viewer_ap80.bmp" \
        "$ap80_rockbox_root/icons/modone_viewer_${ap80_variant}_ap80.bmp" \
        "$ap80_fg" "$ap80_bg"

    for ap80_size in 360x640x16 480x640x16
    do
        cmp \
            "$ap80_rockbox_root/wps/OneBitMono.$ap80_size.wps" \
            "$ap80_rockbox_root/wps/OneBit_${ap80_variant}.$ap80_size.wps"

        sed "s/%Vf($ap80_fg)/%Vf(ffffff)/" \
            "$ap80_rockbox_root/wps/OneBit_${ap80_variant}.$ap80_size.sbs" |
            cmp - "$ap80_rockbox_root/wps/OneBitMono.$ap80_size.sbs"
    done
done

echo "AP80 OneBit artwork tests passed"
