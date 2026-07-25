#!/bin/sh
set -eu

: "${AP80_FAKE_ROOT:?}"
: "${AP80_FAKE_TMP:?}"

if [ "${1:-}" = "-d" ]; then
    shift
elif [ "${1:-}" = "-s" ]; then
    shift 2
fi

ap80_map_path()
{
    case "$1" in
        /data/mnt/sd_0/*)
            printf '%s/%s\n' "$AP80_FAKE_ROOT" "${1#/data/mnt/sd_0/}"
            ;;
        /tmp/*)
            printf '%s/%s\n' "$AP80_FAKE_TMP" "${1#/tmp/}"
            ;;
        *)
            return 1
            ;;
    esac
}

case "${1:-}" in
    wait-for-usb-device)
        exit 0
        ;;
    get-state)
        echo device
        ;;
    push)
        cp "$2" "$(ap80_map_path "$3")"
        ;;
    shell)
        shift
        case "${1:-}" in
            test)
                test "$2" "$(ap80_map_path "$3")"
                ;;
            chmod)
                chmod "$2" "$(ap80_map_path "$3")"
                ;;
            sha256sum)
                sha256sum "$(ap80_map_path "$2")"
                ;;
            sh)
                ap80_script=$(ap80_map_path "$2")
                if [ "${3:-}" = "--rollback" ]; then
                    AP80_UPDATE_ROOT="$AP80_FAKE_ROOT" \
                    AP80_UPDATE_MARKER_DIR="$AP80_FAKE_TMP" \
                        sh "$ap80_script" --rollback
                else
                    AP80_UPDATE_ROOT="$AP80_FAKE_ROOT" \
                    AP80_UPDATE_MARKER_DIR="$AP80_FAKE_TMP" \
                        sh "$ap80_script" "$(ap80_map_path "$3")"
                fi
                ;;
            "sed -n 's/^Target: //p' /data/mnt/sd_0/.rockbox/rockbox-info.txt")
                sed -n 's/^Target: //p' \
                    "$AP80_FAKE_ROOT/.rockbox/rockbox-info.txt"
                ;;
            *)
                echo "Unsupported fake adb shell command: $*" >&2
                exit 2
                ;;
        esac
        ;;
    *)
        echo "Unsupported fake adb command: $*" >&2
        exit 2
        ;;
esac
