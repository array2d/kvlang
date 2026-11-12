#!/bin/sh
set -eu

left_bin=$1
right_bin=$2

left_out=$("$left_bin")
right_out=$("$right_bin")

if [ "$left_out" = "$right_out" ]; then
    echo "wire golden: identical ($(printf '%s\n' "$left_out" | wc -l) cases)"
    exit 0
fi

echo "wire golden: MISMATCH" >&2
left_tmp=$(mktemp)
right_tmp=$(mktemp)
printf '%s\n' "$left_out" > "$left_tmp"
printf '%s\n' "$right_out" > "$right_tmp"
diff -u "$left_tmp" "$right_tmp" >&2 || true
rm -f "$left_tmp" "$right_tmp"
exit 1
