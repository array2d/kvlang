#!/usr/bin/env bash
# 定长标量 body-only 写的语义回归：重复写、首次赋值、类型变化、同 key 多指令写。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
boot() { KVSPACE="$1" "$KVLANG" -c 'print("boot")' >/dev/null 2>&1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
want=$'a 600\nb hello\nc 6\nd 5\ne 50.0\ng 9\nh 55'
for scheme in shm fs; do
	dsn="$scheme://$tmp/$scheme"
	boot "$dsn"
	if ! "$LAYOUT" "$ROOT/runtime/tests/body_write.kv" "$dsn" >/dev/null 2>&1; then
		echo "FAIL: layout $scheme"
		exit 1
	fi
	out=$(KVSPACE="$dsn" "$KVLANG" test 2>/dev/null)
	if [ "$out" != "$want" ]; then
		echo "FAIL: $scheme output"
		printf '%s\n' "$out"
		exit 1
	fi
done
echo "PASS: body_write shm/fs"
