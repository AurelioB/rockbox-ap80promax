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
"$fixture_tool" "$work_dir/test.dsf" "$work_dir/test.dff" \
    "$work_dir/test.flac"

if [ ! -x "$warble" ]; then
    echo "missing $warble; configure the AP80 warble build first" >&2
    exit 1
fi

printf '\001\020\002\040\004\100\010\200' > "$work_dir/expected.raw"
"$warble" -r "$work_dir/test.dsf" "$work_dir/dsf.raw" \
    2> "$work_dir/dsf.metadata"
"$warble" -r "$work_dir/test.dff" "$work_dir/dff.raw" \
    2> "$work_dir/dff.metadata"

cmp "$work_dir/expected.raw" "$work_dir/dsf.raw"
cmp "$work_dir/expected.raw" "$work_dir/dff.raw"

grep -Fqx "Title: DSF Metadata" "$work_dir/dsf.metadata"
grep -Fqx "Artist: Rockbox" "$work_dir/dsf.metadata"
grep -Fqx "Album: DSD Tests" "$work_dir/dsf.metadata"
grep -Fqx "ID3 version: 5" "$work_dir/dsf.metadata"
grep -Fq "Embedded album art: type 3" "$work_dir/dsf.metadata"

grep -Fqx "Title: DFF Metadata" "$work_dir/dff.metadata"
grep -Fqx "Artist: Rockbox" "$work_dir/dff.metadata"
grep -Fqx "Album: DSD Tests" "$work_dir/dff.metadata"
grep -Fqx "ID3 version: 4" "$work_dir/dff.metadata"
grep -Fq "Embedded album art: type 3" "$work_dir/dff.metadata"

"$warble" -r "$work_dir/test.flac" "$work_dir/flac.raw" \
    2> "$work_dir/flac.metadata"
grep -Fq "Embedded album art: type 3" "$work_dir/flac.metadata"

"$warble" -r -c dsdpcm=1 "$work_dir/test.dsf" "$work_dir/dsf-pcm.raw"
"$warble" -r -c dsdpcm=1 "$work_dir/test.dff" "$work_dir/dff-pcm.raw"
test "$(wc -c < "$work_dir/dsf-pcm.raw")" -eq 16
cmp "$work_dir/dsf-pcm.raw" "$work_dir/dff-pcm.raw"
