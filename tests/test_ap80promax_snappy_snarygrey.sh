#!/bin/sh
set -eu

ap80_rockbox_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

for ap80_font in \
    12-KodeMono-SemiBold.fnt \
    15-KodeMono-SemiBold.fnt \
    22-KodeMono-SemiBold.fnt \
    29-KodeMono-SemiBold.fnt \
    72-KodeMono-Bold.fnt
do
    test -s "$ap80_rockbox_root/fonts/$ap80_font"
done

test -s "$ap80_rockbox_root/licenses/fonts/KodeMono-OFL.txt"
grep -Fq "SIL OPEN FONT LICENSE Version 1.1" \
    "$ap80_rockbox_root/licenses/fonts/KodeMono-OFL.txt"

for ap80_skin in Snappy SNARTYGREY
do
    ap80_wps="$ap80_rockbox_root/wps/$ap80_skin.360x640x16.wps"
    ap80_sbs="$ap80_rockbox_root/wps/$ap80_skin.360x640x16.sbs"

    test -s "$ap80_wps"
    test -s "$ap80_sbs"
    grep -Fqx '%T(0,0,120,80,browse)' "$ap80_wps"
    grep -Fqx '%T(120,0,120,80,menu)' "$ap80_wps"
    grep -Fqx '%T(240,0,120,80,contextmenu)' "$ap80_wps"
    grep -Fqx '%T(120,560,120,80,play)' "$ap80_wps"
    grep -Fqx '%T(0,0,120,80,cancel)' "$ap80_sbs"
    grep -Fqx '%T(240,0,120,80,contextmenu)' "$ap80_sbs"
    grep -Fqx '%T(0,0,360,80,resumeplayback)' "$ap80_sbs"
done

grep -Fqx '%T(12,62,336,28,volume)' \
    "$ap80_rockbox_root/wps/Snappy.360x640x16.wps"
grep -Fqx '%T(78,20,120,28,volume)' \
    "$ap80_rockbox_root/wps/SNARTYGREY.360x640x16.wps"

grep -Fq '%Cl(2,2,182,182,l,t)' \
    "$ap80_rockbox_root/wps/Snappy.360x640x16.wps"
grep -Fq '%Cl(2,2,158,158,l,t)' \
    "$ap80_rockbox_root/wps/SNARTYGREY.360x640x16.wps"

for ap80_theme in \
    Snappy \
    "Snappy Clear Skies" \
    "Snappy Lemonade" \
    "Snappy Neon Ivy" \
    "Snappy Red-Eye Flight" \
    "Snappy Wild Berry" \
    SNARTYGREY
do
    grep -Fqx "Name: $ap80_theme" "$ap80_rockbox_root/wps/WPSLIST"
done

for ap80_icon in \
    Snappy_ap80.bmp \
    Snappy_viewer_ap80.bmp \
    SNARTYGREY_ap80.bmp \
    SNARTYGREY_viewer_ap80.bmp
do
    test -s "$ap80_rockbox_root/icons/$ap80_icon"
done

file "$ap80_rockbox_root/icons/Snappy_ap80.bmp" |
    grep -Fq "36 x 768 x 1"
file "$ap80_rockbox_root/icons/Snappy_viewer_ap80.bmp" |
    grep -Fq "36 x 288 x 1"
file "$ap80_rockbox_root/icons/SNARTYGREY_ap80.bmp" |
    grep -Fq "36 x 768 x 1"
file "$ap80_rockbox_root/icons/SNARTYGREY_viewer_ap80.bmp" |
    grep -Fq "36 x 288 x 1"

echo "AP80 Snappy and SNARTYGREY theme tests passed"
