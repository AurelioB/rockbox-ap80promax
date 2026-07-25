#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work_dir="${TMPDIR:-/tmp}/rockbox-dsd-tests"
fixture_tool="$work_dir/make_dsd_fixtures"
warble_build=${AP80_WARBLE_BUILD:-"$repo_dir/build-warble-ap80"}
warble="$warble_build/warble.hidizsap80promax"

mkdir -p "$work_dir"

cc -std=c99 -Wall -Wextra -Werror \
    "$repo_dir/tests/make_dsd_fixtures.c" -o "$fixture_tool"
"$fixture_tool" "$work_dir/test.dsf" "$work_dir/test.dff"

if [ ! -x "$warble" ]; then
    echo "missing $warble; configure the AP80 warble build first" >&2
    exit 1
fi

printf '\001\020\002\040\004\100\010\200' > "$work_dir/expected.raw"
"$warble" -r "$work_dir/test.dsf" "$work_dir/dsf.raw"
"$warble" -r "$work_dir/test.dff" "$work_dir/dff.raw"

cmp "$work_dir/expected.raw" "$work_dir/dsf.raw"
cmp "$work_dir/expected.raw" "$work_dir/dff.raw"

"$warble" -r -c dsdpcm=1 "$work_dir/test.dsf" "$work_dir/dsf-pcm.raw"
"$warble" -r -c dsdpcm=1 "$work_dir/test.dff" "$work_dir/dff-pcm.raw"
test "$(wc -c < "$work_dir/dsf-pcm.raw")" -eq 16
cmp "$work_dir/dsf-pcm.raw" "$work_dir/dff-pcm.raw"
