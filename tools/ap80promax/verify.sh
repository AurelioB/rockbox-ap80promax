#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
ap80_dist=${AP80_DIST_DIR:-"$ap80_workspace_root/dist"}
ap80_zip="$ap80_dist/rockbox-ap80promax.zip"

for ap80_required in \
    "$ap80_dist/rockbox.ap80promax" \
    "$ap80_zip" \
    "$ap80_dist/bootloader.ap80promax" \
    "$ap80_dist/update-usb.sh" \
    "$ap80_dist/update-device.sh" \
    "$ap80_dist/SHA256SUMS"
do
    if [ ! -f "$ap80_required" ]; then
        echo "Missing release artifact: $ap80_required" >&2
        exit 1
    fi
done

(
    cd "$ap80_dist"
    sha256sum -c SHA256SUMS
)

for ap80_script in \
    "$ap80_dist/update-usb.sh" \
    "$ap80_dist/update-device.sh"
do
    if [ ! -x "$ap80_script" ]; then
        echo "USB update helper is not executable: $ap80_script" >&2
        exit 1
    fi
    sh -n "$ap80_script"
done

if [ "${AP80_REQUIRE_UPDATER:-0}" = "1" ] ||
   [ -f "$ap80_dist/ap80max_rb.upt" ] ||
   [ -f "$ap80_dist/SHA256SUMS-updater" ]; then
    for ap80_required in \
        "$ap80_dist/ap80max_rb.upt" \
        "$ap80_dist/SHA256SUMS-updater"
    do
        if [ ! -f "$ap80_required" ]; then
            echo "Incomplete updater artifacts: $ap80_required" >&2
            exit 1
        fi
    done
    (
        cd "$ap80_dist"
        sha256sum -c SHA256SUMS-updater
    )
fi

unzip -tqq "$ap80_zip"

ap80_zip_entries=$(unzip -Z1 "$ap80_zip")
for ap80_theme in \
    OneBitMono \
    OneBitMono2 \
    OneBit_Green \
    OneBit_Orange \
    OneBit_OLED \
    OneBit_VFD2 \
    OneBit_VFD_ALT
do
    if ! printf '%s\n' "$ap80_zip_entries" |
         grep -qx ".rockbox/themes/$ap80_theme.cfg"
    then
        echo "Release ZIP is missing theme preset: $ap80_theme" >&2
        exit 1
    fi
    if ! unzip -p "$ap80_zip" ".rockbox/themes/$ap80_theme.cfg" |
         grep -qx "list padding: auto"
    then
        echo "Release theme does not enable touch-sized rows: $ap80_theme" >&2
        exit 1
    fi
    for ap80_theme_setting in \
        "font: /.rockbox/fonts/21-LanaPixel.fnt" \
        "iconset: /.rockbox/icons/modone_ap80.bmp" \
        "viewers iconset: /.rockbox/icons/modone_viewer_ap80.bmp"
    do
        if ! unzip -p "$ap80_zip" ".rockbox/themes/$ap80_theme.cfg" |
             grep -Fqx "$ap80_theme_setting"
        then
            echo "Release theme is missing AP80 sizing: $ap80_theme" >&2
            exit 1
        fi
    done
done

for ap80_skin in OneBitMono OneBitMono2 OneBit_OLED OneBit_VFD2 OneBit_VFD_ALT
do
    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.sbs" |
         grep -Eq '^%Vl\([^,]+,20,10,240,48,-\)'
    then
        echo "Release theme has an undersized header title: $ap80_skin" >&2
        exit 1
    fi

    for ap80_skin_type in sbs wps
    do
        if ! unzip -p "$ap80_zip" \
             ".rockbox/wps/$ap80_skin.$ap80_skin_type" |
             grep -Eq '^%Vl\([^,]+,378,21,50,20,3\)'
        then
            echo "Release theme is missing readable battery status: $ap80_skin.$ap80_skin_type" >&2
            exit 1
        fi
    done

    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.wps" |
         grep -Fqx '%T(160,0,160,80,menu)'
    then
        echo "Release theme does not use the full-width touch grid: $ap80_skin" >&2
        exit 1
    fi
done

for ap80_skin in OneBitMono OneBitMono2 OneBit_VFD2 OneBit_VFD_ALT
do
    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.wps" |
         grep -Fqx '%Vl(metadata,20,360,220,30,-)'
    then
        echo "Release WPS does not use the AP80 content width: $ap80_skin" >&2
        exit 1
    fi
done

for ap80_icon in modone_ap80.bmp modone_viewer_ap80.bmp
do
    if ! printf '%s\n' "$ap80_zip_entries" |
         grep -qx ".rockbox/icons/$ap80_icon"
    then
        echo "Release ZIP is missing AP80 list icons: $ap80_icon" >&2
        exit 1
    fi
done

for ap80_elf in \
    "$ap80_dist/rockbox.ap80promax" \
    "$ap80_dist/bootloader.ap80promax"
do
    ap80_file_desc=$(file "$ap80_elf")
    case "$ap80_file_desc" in
        *"ELF 32-bit LSB"*"MIPS"*) ;;
        *)
            echo "Unexpected target executable: $ap80_file_desc" >&2
            exit 1
            ;;
    esac
done

if ! strings "$ap80_dist/rockbox.ap80promax" |
     grep -Fq '/etc/init.d/T90adb'
then
    echo "Rockbox application is missing the AP80 vendor ADB path" >&2
    exit 1
fi

echo "AP80 Pro Max release artifacts verified"
