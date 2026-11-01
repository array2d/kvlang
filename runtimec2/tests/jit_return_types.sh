#!/usr/bin/env bash
# JIT 写参提交的类型回归：float64/bool/int64 返回值在三档下逐字节一致。
# 三档：默认（M1+JIT2）、RC2_NOJIT2（仅 M1）、RC2_NOJIT（纯解释器）。
# usage: jit_return_types.sh <byteseek> <repo-root>
set -uo pipefail
BIN=${1:?usage: jit_return_types.sh <byteseek> <repo-root>}
ROOT=${2:?usage: jit_return_types.sh <byteseek> <repo-root>}
KV="$ROOT/runtimec2/tests/fixtures/jit2/typed_returns.kv"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
run() {  # run <tag> <env...>
    local tag=$1
    shift
    rm -f "$TMP/$tag.shm"*
    env KVSPACE="shm://$TMP/$tag.shm" "$@" "$BIN" "$KV" \
        > "$TMP/$tag.out" 2> "$TMP/$tag.err"
    echo $? > "$TMP/$tag.rc"
}

run jit
run nojit2 RC2_NOJIT2=1
run interp RC2_NOJIT=1 RC2_NOJIT2=1

want='x = 6.0 y = true z = false w = 6'
if ! grep -qxF "$want" "$TMP/jit.out"; then
    echo "FAIL: default output '$want' missing"
    cat "$TMP/jit.out"
    fail=1
fi
for tag in nojit2 interp; do
    if ! diff -q "$TMP/jit.out" "$TMP/$tag.out" > /dev/null ||
       [ "$(cat "$TMP/jit.rc")" != "$(cat "$TMP/$tag.rc")" ]; then
        echo "FAIL: jit vs $tag differ"
        diff "$TMP/jit.out" "$TMP/$tag.out" | head -6
        fail=1
    fi
done
[ "$fail" = 0 ] && echo "OK typed_returns"
exit $fail
