#!/usr/bin/env bash
# 特化派发资格：kind 稳定推断只影响是否发 <kind>·<op>，不改变运行时 kind。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/byteseek}"
boot() {
	if ! KVSPACE="$1" "$KVLANG" -c 'print("boot")' >/dev/null; then
		echo "FAIL: boot $1 (KVLANG=$KVLANG)" >&2
		exit 1
	fi
}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
boot "fs://$tmp/db"
out=$("$LAYOUT" printlib "$ROOT/runtime/tests/specialize_gate.kv" /lib "fs://$tmp/db" 2>/dev/null | tr -d '\0')

want_stable() {
	case "$out" in
	*"$1"*) ;;
	*) echo "FAIL: 应特化但未出现 '$1'"; exit 1 ;;
	esac
}
# Stable: loop local, declared param, and typed `int64` array element (`compactarray·at`).
want_stable "int64·add(stable, 1)"
want_stable "int64·add(*[0,-1], 1)"
want_stable "int64·add(elem, 1)"
# Unstable: dynamic kind changes or address-taken locals emit generic opcodes.
case "$out" in
*"·add(unstable"*) echo "FAIL: unstable 不应特化"; exit 1 ;;
esac
case "$out" in
*"·add(addr"*) echo "FAIL: 取址变量不应特化"; exit 1 ;;
esac
echo "PASS: specialize gate"
