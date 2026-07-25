#!/bin/sh
set -eu

ap80_script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$ap80_script_dir/rockbox-ap80promax.zip" ]; then
    ap80_default_dist=$ap80_script_dir
else
    ap80_rockbox_root=$(CDPATH= cd -- "$ap80_script_dir/../.." && pwd)
    ap80_workspace_root=${AP80_WORKSPACE_ROOT:-$(dirname "$ap80_rockbox_root")}
    ap80_default_dist=$ap80_workspace_root/dist
fi
ap80_dist=${AP80_DIST_DIR:-"$ap80_default_dist"}
ap80_zip=${AP80_UPDATE_ZIP:-"$ap80_dist/rockbox-ap80promax.zip"}
ap80_device_script="$ap80_script_dir/update-device.sh"
ap80_adb=${AP80_ADB:-adb}
ap80_serial=${AP80_ADB_SERIAL:-}
ap80_remote_zip=/data/mnt/sd_0/.rockbox-usb-update.zip
ap80_remote_script=/tmp/rockbox-update-device.sh
ap80_mode=update

ap80_usage()
{
    cat <<EOF
Usage: $0 [--zip RELEASE.zip]
       $0 --rollback

Update Rockbox over USB without reflashing the AP80 Pro Max bootloader.
Choose TOOLS -> USB Rockbox update first. On an older bootloader, choose
TOOLS -> ADB start instead.

Environment:
  AP80_ADB          adb executable path
  AP80_ADB_SERIAL   select a specific adb serial
  AP80_UPDATE_ZIP   default release ZIP
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --zip)
            [ "$#" -ge 2 ] || { ap80_usage >&2; exit 2; }
            ap80_zip=$2
            shift 2
            ;;
        --rollback)
            ap80_mode=rollback
            shift
            ;;
        --help|-h)
            ap80_usage
            exit 0
            ;;
        *)
            ap80_usage >&2
            exit 2
            ;;
    esac
done

ap80_adb_run()
{
    if [ -n "$ap80_serial" ]; then
        "$ap80_adb" -s "$ap80_serial" "$@"
    else
        "$ap80_adb" -d "$@"
    fi
}

command -v "$ap80_adb" >/dev/null 2>&1 ||
    { echo "adb was not found; set AP80_ADB to its path" >&2; exit 1; }
[ -f "$ap80_device_script" ] ||
    { echo "Missing device helper: $ap80_device_script" >&2; exit 1; }

if [ "$ap80_mode" = update ]; then
    [ -f "$ap80_zip" ] ||
        { echo "Missing release ZIP: $ap80_zip" >&2; exit 1; }
    unzip -tqq "$ap80_zip"

    ap80_entries=$(unzip -Z1 "$ap80_zip")
    printf '%s\n' "$ap80_entries" |
        grep -qx '.rockbox/rockbox.ap80promax' ||
        { echo "Release ZIP has no AP80 Pro Max executable" >&2; exit 1; }
    printf '%s\n' "$ap80_entries" |
        grep -qx '.rockbox/rockbox-info.txt' ||
        { echo "Release ZIP has no rockbox-info.txt" >&2; exit 1; }

    while IFS= read -r ap80_entry; do
        case "$ap80_entry" in
            .rockbox|.rockbox/|.rockbox/*) ;;
            *)
                echo "Unsafe path outside .rockbox: $ap80_entry" >&2
                exit 1
                ;;
        esac
        case "/$ap80_entry/" in
            *"/../"*|*"/./"*|*\\*)
                echo "Unsafe path in release ZIP: $ap80_entry" >&2
                exit 1
                ;;
        esac
    done <<EOF
$ap80_entries
EOF

    ap80_local_target=$(unzip -p "$ap80_zip" .rockbox/rockbox-info.txt |
        sed -n 's/^Target: //p')
    ap80_local_id=$(unzip -p "$ap80_zip" .rockbox/rockbox-info.txt |
        sed -n 's/^Target id: //p')
    [ "$ap80_local_target" = hidizsap80promax ] &&
        [ "$ap80_local_id" = 125 ] ||
        { echo "Release ZIP is not for the AP80 Pro Max" >&2; exit 1; }
fi

echo "Waiting for the AP80 Pro Max USB update mode..."
ap80_adb_run wait-for-usb-device
ap80_adb_run get-state | grep -qx device ||
    { echo "The selected adb device is not ready" >&2; exit 1; }

ap80_adb_run shell test -f \
    /data/mnt/sd_0/.rockbox/rockbox-info.txt ||
    { echo "No installed Rockbox tree found on the AP80 SD card" >&2; exit 1; }
ap80_remote_target=$(ap80_adb_run shell \
    "sed -n 's/^Target: //p' /data/mnt/sd_0/.rockbox/rockbox-info.txt" |
    tr -d '\r')
[ "$ap80_remote_target" = hidizsap80promax ] ||
    { echo "Connected device is not an AP80 Pro Max Rockbox install" >&2; exit 1; }

ap80_adb_run push "$ap80_device_script" "$ap80_remote_script" >/dev/null
ap80_adb_run shell chmod 755 "$ap80_remote_script"

if [ "$ap80_mode" = rollback ]; then
    ap80_adb_run shell sh "$ap80_remote_script" --rollback
else
    ap80_local_sum=$(sha256sum "$ap80_zip" | sed 's/ .*//')
    ap80_adb_run push "$ap80_zip" "$ap80_remote_zip" >/dev/null
    ap80_remote_sum=$(ap80_adb_run shell sha256sum "$ap80_remote_zip" |
        sed 's/ .*//' | tr -d '\r')
    [ "$ap80_remote_sum" = "$ap80_local_sum" ] ||
        { echo "USB transfer checksum mismatch" >&2; exit 1; }
    ap80_adb_run shell sh "$ap80_remote_script" "$ap80_remote_zip"
fi

echo "Success. The player can now restart into the updated Rockbox."
