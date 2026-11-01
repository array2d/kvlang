#!/usr/bin/env bash
# 第二阶段 A/B：默认档（JIT2 开）与 RC2_NOJIT2=1（JIT2 关）逐值对拍，
# 并检查目标函数确实进 JIT2。
# usage: jit2_ab.sh <byteseek> <repo-root>
set -uo pipefail
BIN=${1:?usage: jit2_ab.sh <byteseek> <repo-root>}
ROOT=${2:?usage: jit2_ab.sh <byteseek> <repo-root>}
FIX="$ROOT/runtimec2/tests/fixtures/jit2"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
run_case() {
    local name=$1
    local func=$2
    local expect=${3:-accept}
    for mode in jit2 nojit; do
        local dsn="shm://$TMP/$name-${mode}.shm"
        rm -f "$TMP/$name-${mode}.shm"*
        if [ "$mode" = nojit ]; then
            KVSPACE="$dsn" RC2_NOJIT2=1 "$BIN" "$FIX/$name.kv" \
                > "$TMP/$name-$mode.out" 2> "$TMP/$name-$mode.err"
        else
            KVSPACE="$dsn" RC2_JITDBG=1 "$BIN" "$FIX/$name.kv" \
                > "$TMP/$name-$mode.out" 2> "$TMP/$name-$mode.err"
        fi
        echo $? > "$TMP/$name-$mode.rc"
    done
    local rj rn
    rj=$(cat "$TMP/$name-jit2.rc")
    rn=$(cat "$TMP/$name-nojit.rc")
    if [ "$rj" != "$rn" ]; then
        echo "FAIL $name: exit code jit2=$rj nojit=$rn"
        fail=1
        return
    fi
    if ! diff -q "$TMP/$name-jit2.out" "$TMP/$name-nojit.out" > /dev/null; then
        echo "FAIL $name: stdout differs"
        fail=1
        return
    fi
    if [ "$expect" = accept ]; then
        if ! grep -q "\[jit2dbg accept /lib/$func " "$TMP/$name-jit2.err"; then
            echo "FAIL $name: /lib/$func did not enter JIT2"
            fail=1
            return
        fi
        echo "OK $name (/lib/$func via JIT2, exit=$rj)"
    else
        if ! grep -q "\[jit2dbg reject /lib/$func .*reason=11" "$TMP/$name-jit2.err"; then
            echo "FAIL $name: /lib/$func should be rejected by HandlerDensity"
            fail=1
            return
        fi
        echo "OK $name (/lib/$func rejected by HandlerDensity, exit=$rj)"
    fi
}

run_case zero_param zero accept
run_case multi_write pair accept
run_case multi_read_loop mix accept
run_case no_return touch accept
run_case native_cold cold accept
run_case native_hot hot reject
run_case float_ops float_mix accept

exit $fail
