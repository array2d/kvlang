#!/usr/bin/env bash
# M1 端到端：layout 一份 print("hello") 的程序，再用 runtimec2 的驱动跑一遍。
set -euo pipefail
BIN=${1:?usage: m1_smoke.sh <m1_smoke> [kvlang]}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
KVLANG=${2:-${KVLANG_BIN:-$ROOT/bin/byteseek}}
STORE=$(mktemp -d)/m1.shm
trap 'rm -rf "$(dirname "$STORE")"' EXIT
KVSPACE="shm://$STORE" "$KVLANG" -c 'print("hello")' >/dev/null 2>&1 || true
out=$("$BIN" "shm://$STORE" init)
if [ "$out" != "hello" ]; then
    echo "FAIL: expected hello, got '$out'"
    exit 1
fi
echo "OK m1 print(hello)"

# 参数内存别名写穿（answer-017）：call 结果写进调用方写参寄存器时，
# 值不能只留在 fast 缓存；kvspace 上的 key 必须同步（否则后续读/handoff 读到空）。
ALIAS_DIR=$(mktemp -d)
ALIAS_STORE="$ALIAS_DIR/alias.shm"
cat > "$ALIAS_DIR/alias.kv" <<'KV'
rwfunc child(x:int64) -> (r:int64) {
	x + 1 -> r
}
rwfunc parent(p:int64) -> (o:int64) {
	child(1) -> o
	print(o)
	o -> o
}
rwfunc test() -> () {
	0 -> p
	0 -> w
	parent(p) -> w
	print(w)
}
KV
KVSPACE="shm://$ALIAS_STORE" "$KVLANG" -c 'print("boot")' >/dev/null 2>&1 || true
KVSPACE="shm://$ALIAS_STORE" "$KVLANG" layout "$ALIAS_DIR/alias.kv" >/dev/null
out=$("$BIN" "shm://$ALIAS_STORE" test)
if [ "$out" != $'2\n2' ]; then
    echo "FAIL: aliased write param expected '2\n2', got '$out'"
    rm -rf "$ALIAS_DIR"
    exit 1
fi
rm -rf "$ALIAS_DIR"
echo "OK m1 aliased write param -> kvspace"
