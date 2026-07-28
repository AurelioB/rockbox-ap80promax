#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ap80_config="$ap80_rockbox_root/firmware/export/config/hidizsap80promax.h"
ap80_button_h="$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/button-target.h"
ap80_button_c="$ap80_rockbox_root/firmware/target/hosted/hiby/ap80promax/button-ap80promax.c"
ap80_devinput="$ap80_rockbox_root/firmware/target/hosted/button-devinput.c"
ap80_misc="$ap80_rockbox_root/apps/misc.c"

grep -Fqx '#define HAVE_MULTIMEDIA_KEYS' "$ap80_config"
grep -Fqx '#define MULTIMEDIA_VOLUME_STEP VOLUME_WHEEL_STEP' "$ap80_config"
grep -Fqx '#define BUTTON_DEVINPUT_EVENT_HOOK' "$ap80_button_h"
grep -Fqx '#define POWEROFF_BUTTON BUTTON_SCREEN' "$ap80_button_h"
grep -Fq 'if (button_handle_event(event.code, event.value))' "$ap80_devinput"

for ap80_media_action in \
    BUTTON_MULTIMEDIA_VOL_DOWN \
    BUTTON_MULTIMEDIA_VOL_UP \
    BUTTON_MULTIMEDIA_PLAYPAUSE \
    BUTTON_MULTIMEDIA_PREV \
    BUTTON_MULTIMEDIA_NEXT
do
    grep -Fq "button = $ap80_media_action;" "$ap80_button_c"
done

grep -Fq 'screen_was_on = is_backlight_on(true);' "$ap80_button_c"
grep -Fq 'backlight_off();' "$ap80_button_c"
grep -Fq 'return BUTTON_SCREEN;' "$ap80_button_c"
grep -Fq 'case BUTTON_MULTIMEDIA_VOL_DOWN:' "$ap80_misc"
grep -Fq 'case BUTTON_MULTIMEDIA_VOL_UP:' "$ap80_misc"

echo "AP80 global-control tests passed"
