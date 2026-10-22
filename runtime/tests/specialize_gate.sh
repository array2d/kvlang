#!/usr/bin/env bash
# 特化派发资格：kind 稳定推断只影响是否发 <kind>·<op>，不改变运行时 kind。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
out=$("$LAYOUT" printlib "$ROOT/runtime/tests/specialize_gate.kv" /lib "fs://$tmp/db" 2>/dev/null | tr -d '\0')

want_stable() {
	case "$out" in
	*"$1"*) ;;
	*) echo "FAIL: 应特化但未出现 '$1'"; exit 1 ;;
	esac
}
# 稳定：局部循环变量 + 声明参数
want_stable "int64·add(stable, 1)"
want_stable "int64·add(*[0,-1], 1)"
# 不稳定：动态改 kind / 取址 / 容器元素 → 通用 opcode
case "$out" in
*"·add(unstable"*) echo "FAIL: unstable 不应特化"; exit 1 ;;
esac
case "$out" in
*"·add(addr"*) echo "FAIL: 取址变量不应特化"; exit 1 ;;
esac
case "$out" in
*"·add(elem"*) echo "FAIL: 容器元素不应特化"; exit 1 ;;
esac
echo "PASS: specialize gate"
