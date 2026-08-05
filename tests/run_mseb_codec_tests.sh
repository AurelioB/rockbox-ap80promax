#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/rockbox-mseb-tests.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
warble_build=${AP80_WARBLE_BUILD:-"$repo_dir/build-warble-ap80"}
warble="$warble_build/warble.hidizsap80promax"
fixture_tool="$work_dir/make_mseb_fixture"
rms_tool="$work_dir/check_mseb_wav"
host_cc=${HOSTCC:-cc}

"$host_cc" -std=c99 -Wall -Wextra -Werror \
    "$repo_dir/tests/make_mseb_fixture.c" -lm -o "$fixture_tool"
"$host_cc" -std=c99 -Wall -Wextra -Werror \
    "$repo_dir/tests/check_mseb_wav.c" -lm -o "$rms_tool"
"$fixture_tool" "$work_dir/impulse.wav"

if [ ! -x "$warble" ]; then
    echo "missing $warble; configure the AP80 warble build first" >&2
    exit 1
fi

"$warble" "$work_dir/impulse.wav" "$work_dir/bypass.wav" 2>/dev/null
"$warble" -c mseb=1 "$work_dir/impulse.wav" \
    "$work_dir/neutral.wav" 2>/dev/null
cmp "$work_dir/bypass.wav" "$work_dir/neutral.wav"
"$warble" -c mseb0=100:mseb=0 "$work_dir/impulse.wav" \
    "$work_dir/disabled.wav" 2>/dev/null
cmp "$work_dir/bypass.wav" "$work_dir/disabled.wav"

parameter=0
while [ "$parameter" -lt 10 ]; do
    "$warble" -c "mseb${parameter}=100:mseb=1" \
        "$work_dir/impulse.wav" "$work_dir/positive-${parameter}.wav" \
        2>/dev/null
    "$warble" -c "mseb${parameter}=-100:mseb=1" \
        "$work_dir/impulse.wav" "$work_dir/negative-${parameter}.wav" \
        2>/dev/null
    ! cmp -s "$work_dir/bypass.wav" "$work_dir/positive-${parameter}.wav"
    ! cmp -s "$work_dir/bypass.wav" "$work_dir/negative-${parameter}.wav"
    ! cmp -s "$work_dir/positive-${parameter}.wav" \
             "$work_dir/negative-${parameter}.wav"
    parameter=$((parameter + 1))
done

"$warble" -c mseb0=999:mseb=1 "$work_dir/impulse.wav" \
    "$work_dir/clamped-high.wav" 2>/dev/null
cmp "$work_dir/clamped-high.wav" "$work_dir/positive-0.wav"
"$warble" -c mseb0=-999:mseb=1 "$work_dir/impulse.wav" \
    "$work_dir/clamped-low.wav" 2>/dev/null
cmp "$work_dir/clamped-low.wav" "$work_dir/negative-0.wav"

check_direction()
{
    frequency=$1
    parameter=$2
    label=$3
    "$fixture_tool" "$work_dir/$label-input.wav" "$frequency" 12000
    "$warble" -c "mseb${parameter}=100:mseb=1" \
        "$work_dir/$label-input.wav" "$work_dir/$label-positive.wav" \
        2>/dev/null
    "$warble" -c "mseb${parameter}=-100:mseb=1" \
        "$work_dir/$label-input.wav" "$work_dir/$label-negative.wav" \
        2>/dev/null
    positive_rms=$("$rms_tool" "$work_dir/$label-positive.wav")
    negative_rms=$("$rms_tool" "$work_dir/$label-negative.wav")
    awk -v positive="$positive_rms" -v negative="$negative_rms" \
        'BEGIN { exit !(positive > negative * 1.25) }'
}

# Exercise the real quantized filter path and verify the audible direction of
# representative peak and shelf controls after automatic normalization.
check_direction 100 2 bass-texture
check_direction 15000 9 air
# Negative Temperature is the cool/bright direction, so invert the comparison.
"$fixture_tool" "$work_dir/temperature-input.wav" 15000 12000
"$warble" -c mseb0=-100:mseb=1 "$work_dir/temperature-input.wav" \
    "$work_dir/temperature-cool.wav" 2>/dev/null
"$warble" -c mseb0=100:mseb=1 "$work_dir/temperature-input.wav" \
    "$work_dir/temperature-warm.wav" 2>/dev/null
cool_rms=$("$rms_tool" "$work_dir/temperature-cool.wav")
warm_rms=$("$rms_tool" "$work_dir/temperature-warm.wav")
awk -v cool="$cool_rms" -v warm="$warm_rms" \
    'BEGIN { exit !(cool > warm * 1.25) }'

# Drive the worst legal 13-filter chain near its measured prefix peak. Doubling
# a coherent input must double the settled output: clipping or fixed-point wrap
# caused by a missing >48 dB precut breaks that relationship.
extreme_config=mseb0=-100:mseb1=-100:mseb2=100:mseb3=100:mseb4=100:mseb5=100:mseb6=100:mseb7=100:mseb8=100:mseb9=100:mseb=1
"$fixture_tool" "$work_dir/extreme-low-input.wav" 9525 15000
"$fixture_tool" "$work_dir/extreme-high-input.wav" 9525 30000
"$warble" -c "$extreme_config" "$work_dir/extreme-low-input.wav" \
    "$work_dir/extreme-low.wav" 2>/dev/null
"$warble" -c "$extreme_config" "$work_dir/extreme-high-input.wav" \
    "$work_dir/extreme-high.wav" 2>/dev/null
"$warble" "$work_dir/extreme-high-input.wav" \
    "$work_dir/extreme-bypass.wav" 2>/dev/null
low_rms=$("$rms_tool" "$work_dir/extreme-low.wav")
high_rms=$("$rms_tool" "$work_dir/extreme-high.wav")
bypass_rms=$("$rms_tool" "$work_dir/extreme-bypass.wav")
awk -v low="$low_rms" -v high="$high_rms" -v bypass="$bypass_rms" \
    'BEGIN {
        exit !(low > 1000.0 &&
               high > low * 1.98 && high < low * 2.02 &&
               high > bypass * 0.95 && high < bypass * 1.001)
    }'

echo "MSEB codec tests passed"
