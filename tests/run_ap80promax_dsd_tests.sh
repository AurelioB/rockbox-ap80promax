#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin="${TMPDIR:-/tmp}/test_ap80promax_dsd"

cc -std=c99 -Wall -Wextra -Werror \
    -I"$repo_dir/firmware/export" \
    -I"$repo_dir/firmware/target/hosted/hiby/ap80promax" \
    "$repo_dir/tests/test_ap80promax_dsd.c" \
    "$repo_dir/firmware/target/hosted/hiby/ap80promax/dsd-ap80promax.c" \
    -o "$test_bin"

"$test_bin"
