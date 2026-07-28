#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
ap80_dist=${AP80_DIST_DIR:-"$ap80_workspace_root/dist"}
ap80_zip="$ap80_dist/rockbox-ap80promax.zip"
ap80_updater="$ap80_dist/ap80max_rb.upt"
ap80_updater_checksum="$ap80_dist/SHA256SUMS-updater"
ap80_launcher=${AP80_LAUNCHER:-"$ap80_rockbox_root/tools/r1_patcher/hiby_player-rockbox.sh"}
ap80_updater_inspect=
ap80_zip_application=

ap80_cleanup()
{
    [ -z "$ap80_updater_inspect" ] ||
        rm -rf "$ap80_updater_inspect"
    [ -z "$ap80_zip_application" ] ||
        rm -f "$ap80_zip_application"
}

trap 'ap80_status=$?; ap80_cleanup; exit "$ap80_status"' 0
trap 'exit 1' HUP INT TERM

ap80_verify_updater()
{
    for ap80_tool in 7z unsquashfs
    do
        command -v "$ap80_tool" >/dev/null 2>&1 || {
            echo "Cannot inspect updater without $ap80_tool" >&2
            exit 1
        }
    done

    [ -f "$ap80_launcher" ] || {
        echo "Missing current recovery launcher: $ap80_launcher" >&2
        exit 1
    }

    ap80_updater_inspect=$(mktemp -d \
        "${TMPDIR:-/tmp}/ap80-updater-verify.XXXXXX")
    mkdir -p "$ap80_updater_inspect/iso" \
             "$ap80_updater_inspect/rootfs"
    7z -o"$ap80_updater_inspect/iso" x "$ap80_updater" >/dev/null

    set -- "$ap80_updater_inspect/iso/ota_v0"/rootfs.squashfs.*
    if [ ! -f "$1" ]; then
        echo "Updater contains no rootfs payload" >&2
        exit 1
    fi
    cat "$@" > "$ap80_updater_inspect/rootfs.squashfs"
    unsquashfs -f -d "$ap80_updater_inspect/rootfs" \
        "$ap80_updater_inspect/rootfs.squashfs" >/dev/null

    ap80_embedded_bootloader="$ap80_updater_inspect/rootfs/usr/bin/bootloader.rb"
    ap80_embedded_launcher="$ap80_updater_inspect/rootfs/usr/bin/hiby_player.sh"
    ap80_stock_launcher="$ap80_updater_inspect/rootfs/usr/bin/hiby_player.stock.sh"

    if ! cmp "$ap80_dist/bootloader.ap80promax" \
             "$ap80_embedded_bootloader" >/dev/null 2>&1
    then
        echo "Updater embeds a bootloader different from the dist artifact" >&2
        exit 1
    fi
    if ! cmp "$ap80_launcher" "$ap80_embedded_launcher" >/dev/null 2>&1
    then
        echo "Updater embeds a launcher different from the current source" >&2
        exit 1
    fi
    if [ ! -x "$ap80_stock_launcher" ]; then
        echo "Updater does not preserve an executable stock launcher" >&2
        exit 1
    fi

    if ! strings "$ap80_embedded_bootloader" |
         grep -Fq '/.rockbox/rb_bl_success'
    then
        echo "Embedded bootloader is missing successful-boot gating" >&2
        exit 1
    fi
    if ! strings "$ap80_embedded_bootloader" |
         grep -Fq '/.rockbox/rb_bl_attempt'
    then
        echo "Embedded bootloader is missing failed-attempt gating" >&2
        exit 1
    fi
    if ! grep -Fq 'rb_bl_success' "$ap80_embedded_launcher" ||
       ! grep -Fq 'rb_bl_attempt' "$ap80_embedded_launcher"; then
        echo "Embedded launcher is missing successful-boot gating" >&2
        exit 1
    fi
}

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
   [ -f "$ap80_updater" ] ||
   [ -f "$ap80_updater_checksum" ]; then
    for ap80_required in \
        "$ap80_updater" \
        "$ap80_updater_checksum"
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
    ap80_verify_updater
fi

unzip -tqq "$ap80_zip"

ap80_zip_application=$(mktemp \
    "${TMPDIR:-/tmp}/ap80-zip-application.XXXXXX")
unzip -p "$ap80_zip" .rockbox/rockbox.ap80promax \
    > "$ap80_zip_application"
if ! cmp "$ap80_dist/rockbox.ap80promax" \
         "$ap80_zip_application" >/dev/null 2>&1
then
    echo "Release ZIP application differs from the dist executable" >&2
    exit 1
fi

if ! strings "$ap80_dist/bootloader.ap80promax" |
     grep -Fq '/.rockbox/rb_bl_success'
then
    echo "AP80 bootloader is missing successful-boot gating" >&2
    exit 1
fi
if ! strings "$ap80_dist/bootloader.ap80promax" |
     grep -Fq '/.rockbox/rb_bl_attempt'
then
    echo "AP80 bootloader is missing failed-attempt gating" >&2
    exit 1
fi

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
    case "$ap80_theme" in
        OneBit_Orange)
            ap80_theme_icon=modone_Orange_ap80.bmp
            ap80_theme_viewer_icon=modone_viewer_Orange_ap80.bmp
            ;;
        OneBit_Green)
            ap80_theme_icon=modone_Green_ap80.bmp
            ap80_theme_viewer_icon=modone_viewer_Green_ap80.bmp
            ;;
        *)
            ap80_theme_icon=modone_ap80.bmp
            ap80_theme_viewer_icon=modone_viewer_ap80.bmp
            ;;
    esac

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
        "iconset: /.rockbox/icons/$ap80_theme_icon" \
        "viewers iconset: /.rockbox/icons/$ap80_theme_viewer_icon"
    do
        if ! unzip -p "$ap80_zip" ".rockbox/themes/$ap80_theme.cfg" |
             grep -Fqx "$ap80_theme_setting"
        then
            echo "Release theme is missing AP80 sizing: $ap80_theme" >&2
            exit 1
        fi
    done
done

for ap80_skin in \
    OneBitMono \
    OneBit_Orange \
    OneBit_Green \
    OneBitMono2 \
    OneBit_OLED \
    OneBit_VFD2 \
    OneBit_VFD_ALT
do
    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.sbs" |
         grep -Eq '^%Vl\([^,]+,12,10,(160|188),48,-\)'
    then
        echo "Release theme has an undersized header title: $ap80_skin" >&2
        exit 1
    fi

    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.sbs" |
         grep -Eq '^%Vi\(menu[^,]*,12,90,336,450,-\)'
    then
        echo "Release theme does not use the visible menu width: $ap80_skin" >&2
        exit 1
    fi

    for ap80_skin_type in sbs wps
    do
        if ! unzip -p "$ap80_zip" \
             ".rockbox/wps/$ap80_skin.$ap80_skin_type" |
             grep -Eq '^%Vl\([^,]+,(278|280),21,(46|48),20,3\)'
        then
            echo "Release theme is missing readable battery status: $ap80_skin.$ap80_skin_type" >&2
            exit 1
        fi
    done

    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.wps" |
         grep -Fqx '%T(120,0,120,80,menu)'
    then
        echo "Release theme does not use the full-width touch grid: $ap80_skin" >&2
        exit 1
    fi

    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.sbs" |
         grep -Fqx '%T(0,0,360,80,resumeplayback)'
    then
        echo "Release theme mini-player does not open Now Playing: $ap80_skin" >&2
        exit 1
    fi
done

for ap80_skin in \
    OneBitMono \
    OneBit_Orange \
    OneBit_Green \
    OneBitMono2 \
    OneBit_VFD2 \
    OneBit_VFD_ALT
do
    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.wps" |
         grep -Fqx '%Vl(metadata,40,360,140,30,-)'
    then
        echo "Release WPS does not use the AP80 content width: $ap80_skin" >&2
        exit 1
    fi
done

for ap80_icon in \
    modone_ap80.bmp \
    modone_viewer_ap80.bmp \
    modone_Orange_ap80.bmp \
    modone_viewer_Orange_ap80.bmp \
    modone_Green_ap80.bmp \
    modone_viewer_Green_ap80.bmp
do
    if ! printf '%s\n' "$ap80_zip_entries" |
         grep -qx ".rockbox/icons/$ap80_icon"
    then
        echo "Release ZIP is missing AP80 list icons: $ap80_icon" >&2
        exit 1
    fi
done

for ap80_variant in Orange Green
do
    for ap80_asset in battery.bmp playmodes.bmp progressbar.bmp usb.bmp
    do
        if ! printf '%s\n' "$ap80_zip_entries" |
             grep -qx ".rockbox/wps/OneBit_$ap80_variant/$ap80_asset"
        then
            echo "Release ZIP is missing OneBit palette art: OneBit_$ap80_variant/$ap80_asset" >&2
            exit 1
        fi
    done
done

for ap80_theme in \
    "Snappy" \
    "Snappy Clear Skies" \
    "Snappy Lemonade" \
    "Snappy Neon Ivy" \
    "Snappy Red-Eye Flight" \
    "Snappy Wild Berry" \
    "SNARTYGREY"
do
    if ! printf '%s\n' "$ap80_zip_entries" |
         grep -qx ".rockbox/themes/$ap80_theme.cfg"
    then
        echo "Release ZIP is missing imported theme: $ap80_theme" >&2
        exit 1
    fi
    if ! unzip -p "$ap80_zip" ".rockbox/themes/$ap80_theme.cfg" |
         grep -Fqx "font: /.rockbox/fonts/29-KodeMono-SemiBold.fnt"
    then
        echo "Release theme is missing readable Kode Mono type: $ap80_theme" >&2
        exit 1
    fi
    if ! unzip -p "$ap80_zip" ".rockbox/themes/$ap80_theme.cfg" |
         grep -Fqx "list padding: auto"
    then
        echo "Release theme does not enable touch-sized rows: $ap80_theme" >&2
        exit 1
    fi
done

for ap80_skin in Snappy SNARTYGREY
do
    for ap80_skin_type in sbs wps
    do
        if ! printf '%s\n' "$ap80_zip_entries" |
             grep -qx ".rockbox/wps/$ap80_skin.$ap80_skin_type"
        then
            echo "Release ZIP is missing imported skin: $ap80_skin.$ap80_skin_type" >&2
            exit 1
        fi
    done

    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.wps" |
         grep -Fqx '%T(120,560,120,80,play)'
    then
        echo "Imported WPS lacks AP80 transport targets: $ap80_skin" >&2
        exit 1
    fi
    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.sbs" |
         grep -Fqx '%T(0,0,120,80,cancel)'
    then
        echo "Imported SBS lacks the AP80 back target: $ap80_skin" >&2
        exit 1
    fi
    if ! unzip -p "$ap80_zip" ".rockbox/wps/$ap80_skin.sbs" |
         grep -Fqx '%T(0,0,360,80,resumeplayback)'
    then
        echo "Imported SBS mini-player does not open Now Playing: $ap80_skin" >&2
        exit 1
    fi
done

for ap80_asset in \
    ".rockbox/icons/Snappy_ap80.bmp" \
    ".rockbox/icons/Snappy_viewer_ap80.bmp" \
    ".rockbox/icons/SNARTYGREY_ap80.bmp" \
    ".rockbox/icons/SNARTYGREY_viewer_ap80.bmp" \
    ".rockbox/wps/Snappy/pb_backdrop_ap80.bmp" \
    ".rockbox/wps/SNARTYGREY/pb_back_ap80.bmp" \
    ".rockbox/docs/KodeMono-OFL.txt"
do
    if ! printf '%s\n' "$ap80_zip_entries" | grep -qx "$ap80_asset"
    then
        echo "Release ZIP is missing imported theme asset: $ap80_asset" >&2
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
