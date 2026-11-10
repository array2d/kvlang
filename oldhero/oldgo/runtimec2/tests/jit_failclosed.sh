#!/usr/bin/env bash
# JIT fail-closed 端到端：同一 fixture 分别用 JIT（默认）与 RC2_NOJIT=1 跑，
# 断言 stdout 与退出码一致，且不出现 SIGSEGV。
# usage: jit_failclosed.sh <byteseek> <repo-root>
set -uo pipefail
BIN=${1:?usage: jit_failclosed.sh <byteseek> <repo-root>}
ROOT=${2:?usage: jit_failclosed.sh <byteseek> <repo-root>}
FIX="$ROOT/runtimec2/tests/fixtures/jit"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
run_case() {
    local name=$1
    local kv="$FIX/$name.kv"
    for mode in jit nojit; do
        local dsn="shm://$TMP/$name-${mode}.shm"
        rm -f "$TMP/$name-${mode}.shm"*
        if [ "$mode" = nojit ]; then
            KVSPACE="$dsn" RC2_NOJIT=1 "$BIN" "$kv" \
                > "$TMP/$name-$mode.out" 2> "$TMP/$name-$mode.err"
        else
            KVSPACE="$dsn" "$BIN" "$kv" \
                > "$TMP/$name-$mode.out" 2> "$TMP/$name-$mode.err"
        fi
        echo $? > "$TMP/$name-$mode.rc"
    done
    local rj rn
    rj=$(cat "$TMP/$name-jit.rc")
    rn=$(cat "$TMP/$name-nojit.rc")
    if [ "$rj" = "139" ] || [ "$rn" = "139" ]; then
        echo "FAIL $name: SIGSEGV (jit=$rj nojit=$rn)"
        fail=1
        return
    fi
    if [ "$rj" != "$rn" ]; then
        echo "FAIL $name: exit code jit=$rj nojit=$rn"
        fail=1
        return
    fi
    if ! diff -q "$TMP/$name-jit.out" "$TMP/$name-nojit.out" > /dev/null; then
        echo "FAIL $name: stdout differs"
        diff "$TMP/$name-jit.out" "$TMP/$name-nojit.out" | head -6
        fail=1
        return
    fi
    local jit_needle=${2:-}
    local nojit_needle=${3:-}
    if [ -n "$jit_needle" ] && ! grep -q "$jit_needle" "$TMP/$name-jit.err"; then
        echo "FAIL $name: JIT stderr missing '$jit_needle'"
        fail=1
        return
    fi
    if [ -n "$nojit_needle" ] && ! grep -q "$nojit_needle" "$TMP/$name-nojit.err"; then
        echo "FAIL $name: NOJIT stderr missing '$nojit_needle'"
        fail=1
        return
    fi
    echo "OK $name (exit=$rj)"
}

run_case callee_saved_7
run_case register_pressure_9
run_case read_before_write
# guard 必须真正写 *err_out：JIT 报 runaway，而不是把 16M 次后的残值当成功结果。
run_case guard_runaway "runaway loop (jit)" "runaway loop (fast)"

exit $fail
