#!/usr/bin/env bash
# ABI 对齐验收：16 个 C ABI 符号在 c2 与 C 版中都存在；c2 不得多导出 kvlang*。
set -euo pipefail
C2=${1:?usage: symbols.sh <libkvlang_runtimec2.so>}
C=${KVLANG_C_SO:-$(dirname "$C2")/libkvlang_runtime.so}

EXPECT="kvlangDefRwir
kvlangRwirextHandoff
kvlangRwirextNextPc
kvlangRwirextParams
kvlangRwirextResolveRead
kvlangRwirextResolveReadPath
kvlangRwirextResolveWrite
kvlangLangtypeValid
kvlangLangtypeMatch
kvlangRuntimeConnect
kvlangRuntimeDisconnect
kvlangRuntimeExecutePc
kvlangRuntimeKvspaceHandle
kvlangRuntimeBootstrap
kvlangRuntimeExecuteVthread
kvlangRuntimeExecute"

expect_n=$(printf '%s\n' "$EXPECT" | wc -l)
[ "$expect_n" -eq 16 ] || { echo "FAIL: expect list != 16"; exit 1; }

a=$(nm -D --defined-only "$C" 2>/dev/null | awk '$2=="T"{print $3}')
b=$(nm -D --defined-only "$C2" 2>/dev/null | awk '$2=="T"{print $3}')

missing_c=""
missing_c2=""
while IFS= read -r n; do
    [ -n "$n" ] || continue
    grep -qx "$n" <<<"$a" || missing_c="$missing_c $n"
    grep -qx "$n" <<<"$b" || missing_c2="$missing_c2 $n"
done <<<"$EXPECT"

if [ -n "$missing_c" ]; then
    echo "FAIL: C runtime missing:$missing_c"; exit 1
fi
if [ -n "$missing_c2" ]; then
    echo "FAIL: c2 missing:$missing_c2"; exit 1
fi

extra=$(grep '^kvlang' <<<"$b" | sort | comm -13 <(printf '%s\n' "$EXPECT" | sort) -)
if [ -n "$extra" ]; then
    echo "FAIL: c2 exports extra kvlang symbols:"; echo "$extra"; exit 1
fi
echo "OK symbol parity (16)"
