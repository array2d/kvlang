#!/usr/bin/env bash
# 回归钉子（#607 后的 syncfail）：matmul 段「float64 局部 + compact 数组 + bool
# 循环条件」必须被 JIT2 接受，且同步阶段不得因 bool body 长度 1 而整体回退。
# 同时断言耗时量级：回退到解释器时约 650µs，正常 13–20µs；阈值取 200µs。
# usage: jit2_matmul_sync.sh <byteseek> <repo-root>
set -uo pipefail
BIN=${1:?usage: jit2_matmul_sync.sh <byteseek> <repo-root>}
ROOT=${2:?usage: jit2_matmul_sync.sh <byteseek> <repo-root>}
SRC="$ROOT/benchmark/cases/matmul/matmul.kv"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

sed 's/__SCALE__/8/g' "$SRC" > "$TMP/matmul.kv"
KVSPACE="shm://$TMP/store.shm" RC2_JITDBG=1 "$BIN" "$TMP/matmul.kv" \
    > "$TMP/out" 2> "$TMP/err"
rc=$?

fail=0
if [ "$rc" != 0 ]; then
    echo "FAIL: exit $rc"
    tail -3 "$TMP/err"
    fail=1
fi
if ! grep -q "jit2dbg accept /lib/test" "$TMP/err"; then
    echo "FAIL: JIT2 did not accept /lib/test"
    grep "lib/test" "$TMP/err" | head -4
    fail=1
fi
if grep -q "jit2dbg syncfail" "$TMP/err"; then
    echo "FAIL: syncfail present"
    grep "syncfail" "$TMP/err" | head -3
    fail=1
fi
if ! grep -q "matmul: check = " "$TMP/out"; then
    echo "FAIL: wrong output"
    cat "$TMP/out"
    fail=1
fi
ns=$(awk -F': ' '/__bench_ns:/{print $2}' "$TMP/out")
if [ -z "$ns" ] || [ "$ns" -gt 200000 ]; then
    echo "FAIL: JIT2 fell back (__bench_ns=${ns:-missing} > 200000)"
    fail=1
fi
[ "$fail" = 0 ] && echo "OK matmul jit2 sync (__bench_ns=$ns)"
exit $fail
