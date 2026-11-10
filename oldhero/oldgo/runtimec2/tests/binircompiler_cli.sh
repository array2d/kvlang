#!/usr/bin/env bash
# binircompiler 可执行入口验收：
#   1. --dump 与运行时 RC2_DUMP_BINIR=1 对同一通用编译逐行一致；
#   2. --check 往返通过；--out 字节以 binir magic 0x4642 开头；
#   3. --compare 把 flex(x:) 的 `+` 按实参分别解析成 int64·add / float64·add；
#   4. 与 layout 的类型推断对拍（fixa/fixb 孪生函数）；
#   5. 错误路径：不存在的函数 / 缺少实参 / --generic 带实参。
# 用法：binircompiler_cli.sh <binircompiler> <byteseek> <kvlanglayout> <repo-root>
set -uo pipefail
BIN=${1:?usage: binircompiler_cli.sh <binircompiler> <byteseek> <kvlanglayout> <root>}
BYTESEEK=${2:?}
LAYOUT=${3:?}
ROOT=${4:?}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/binircompiler-cli.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
FAIL=0

ok() { printf 'OK   %s\n' "$*"; }
bad() { printf 'FAIL %s\n' "$*"; FAIL=1; }

# ── 1. iops /lib/test：dump 与运行时逐行一致 ──────────────────────────
sed 's/__SCALE__/1/g' "$ROOT/benchmark/cases/iops/iops.kv" >"$TMP/iops.kv"
DSN="shm://$TMP/iops/store.shm"
mkdir -p "$TMP/iops"
# stdlib 先 boot（/lib/println 等 def rwir 声明），与 tutorial 运行路径一致。
KVSPACE=$DSN "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1 || bad "stdlib boot"
"$LAYOUT" "$TMP/iops.kv" "$DSN" >/dev/null 2>&1 || bad "layout iops"

KVSPACE=$DSN RC2_DUMP_BINIR=1 "$BYTESEEK" test >/dev/null 2>"$TMP/run.err" || true
awk 'BEGIN{on=0}
     /^\[binir\] func=/{on=($0 ~ /^\[binir\] func=\/lib\/test /)}
     on' "$TMP/run.err" >"$TMP/run.dump"
[ -s "$TMP/run.dump" ] || bad "runtime produced no /lib/test dump"

"$BIN" --kvspace "$DSN" --func /lib/test --generic --dump \
    >"$TMP/cli.dump" 2>/dev/null
if diff -u "$TMP/run.dump" "$TMP/cli.dump" >"$TMP/dump.diff"; then
    ok "dump parity vs RC2_DUMP_BINIR (/lib/test)"
else
    bad "dump mismatch vs RC2_DUMP_BINIR"; head -20 "$TMP/dump.diff"
fi

# --file 必须自己完成运行时同一套引导（def rwir 注册 + stdlib layout），
# 真实程序（println 等）才能编译；输出与运行时同一程序逐行一致。
"$BIN" --file "$TMP/iops.kv" --func /lib/test --generic --dump \
    >"$TMP/file.dump" 2>/dev/null
if diff -u "$TMP/run.dump" "$TMP/file.dump" >"$TMP/file.diff"; then
    ok "--file boot + dump parity (iops/println)"
else
    bad "--file dump mismatch"; head -20 "$TMP/file.diff"
fi

# --no-boot 跳过引导：println 无声明 → 必须硬失败（exit 1），不许静默。
"$BIN" --file "$TMP/iops.kv" --func /lib/test --generic --no-boot >/dev/null 2>&1
[ $? -eq 1 ] && ok "--file --no-boot rejects undeclared rwir (exit 1)" \
    || bad "--no-boot should fail on undeclared println"

# ── 2. --check / --out ────────────────────────────────────────────────
if "$BIN" --kvspace "$DSN" --func /lib/test --generic --check >/dev/null; then
    ok "--check round-trip"
else
    bad "--check round-trip"
fi

"$BIN" --kvspace "$DSN" --func /lib/test --generic --out "$TMP/test.binir" >/dev/null \
    || bad "--out"
MAGIC=$(od -An -tx1 -N2 "$TMP/test.binir" 2>/dev/null | tr -d ' \n')
if [ "$MAGIC" = "4246" ]; then
    ok "--out starts with binir magic 0x4642"
else
    bad "--out magic: got '$MAGIC'"
fi

# ── 3. --compare：实参类型 → typed 算子 ───────────────────────────────
FDSN="shm://$TMP/spec/store.shm"
mkdir -p "$TMP/spec"
"$LAYOUT" "$ROOT/runtimec2/tests/fixtures/binir/specialize.kv" "$FDSN" \
    >/dev/null 2>&1 || bad "layout specialize.kv"

"$BIN" --kvspace "$FDSN" --func /lib/flex --compare 5 >"$TMP/cmp-int.txt" 2>&1
grep -q 'int64·add' "$TMP/cmp-int.txt" && ok "flex(int64) -> int64·add" \
    || { bad "flex(int64) not specialized"; cat "$TMP/cmp-int.txt"; }

"$BIN" --kvspace "$FDSN" --func /lib/flex --compare 5.5 >"$TMP/cmp-float.txt" 2>&1
grep -q 'float64·add' "$TMP/cmp-float.txt" && ok "flex(float64) -> float64·add" \
    || { bad "flex(float64) not specialized"; cat "$TMP/cmp-float.txt"; }
grep -q 'int64·add' "$TMP/cmp-float.txt" \
    && bad "flex(float64) produced int64·add" || true

# ── 4. 与 layout 的推断对拍（fixa/fixc 孪生函数） ────────────────────
"$BIN" --kvspace "$FDSN" --func /lib/fixa --generic --dump \
    >"$TMP/fixa.dump" 2>/dev/null
grep -q 'int64·add' "$TMP/fixa.dump" && ok "layout fixa = int64·add" \
    || { bad "layout fixa not int64·add"; head -20 "$TMP/fixa.dump"; }
"$BIN" --kvspace "$FDSN" --func /lib/fixc --generic --dump \
    >"$TMP/fixc.dump" 2>/dev/null
grep -q 'float64·add' "$TMP/fixc.dump" && ok "layout fixc = float64·add" \
    || { bad "layout fixc not float64·add"; head -20 "$TMP/fixc.dump"; }

# ── 5. 错误路径 ───────────────────────────────────────────────────────
"$BIN" --kvspace "$DSN" --func /lib/no_such_function >/dev/null 2>&1
[ $? -eq 1 ] && ok "missing function -> exit 1" || bad "missing function exit code"

"$BIN" --kvspace "$DSN" --func /lib/iops >/dev/null 2>&1
[ $? -eq 1 ] && ok "missing arguments -> exit 1" || bad "missing arguments exit code"

"$BIN" --kvspace "$DSN" --func /lib/test --generic 5 >/dev/null 2>&1
[ $? -eq 2 ] && ok "--generic with literals -> exit 2" || bad "--generic with literals"

# ── 6. --file 现场 layout（dlopen libkvlanglayout.so） ────────────────
if "$BIN" --file "$ROOT/runtimec2/tests/fixtures/binir/specialize.kv" \
        --func flex 5 --dump >"$TMP/file.dump" 2>/dev/null; then
    grep -q 'int64·add' "$TMP/file.dump" && ok "--file layout + specialize" \
        || bad "--file did not specialize"
else
    bad "--file mode failed"
fi

if [ "$FAIL" -ne 0 ]; then
    echo "FAILURES PRESENT"
    exit 1
fi
echo "ALL PASS"
