#!/usr/bin/env bash
# 特化 opcode 遇到动态操作数：按实际 kind 回退通用实现（[[赋值转换]]）。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
boot() { KVSPACE="$1" "$KVLANG" -c 'print("boot")' >/dev/null 2>&1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

want=$(cat <<'OUT'
f64add 4.0 float64
i8add 11 int8
typed 5 int64
intviaf64 5 int64
sub 3.0 div 2.5 lt false
OUT
)

for scheme in shm fs; do
	dsn="$scheme://$tmp/$scheme"
	boot "$dsn"
	if ! "$LAYOUT" "$ROOT/runtime/tests/dyn_specialize.kv" "$dsn" >/dev/null 2>&1; then
		echo "FAIL: layout $scheme"
		exit 1
	fi
	out=$(KVSPACE="$dsn" "$KVLANG" test 2>/dev/null)
	if [ "$out" != "$want" ]; then
		echo "FAIL: $scheme output differs"
		diff <(printf '%s\n' "$want") <(printf '%s\n' "$out")
		exit 1
	fi

	dsn2="$scheme://$tmp/e-$scheme"
	boot "$dsn2"
	"$LAYOUT" "$ROOT/runtime/tests/dyn_specialize_err.kv" "$dsn2" >/dev/null 2>&1 || {
		echo "FAIL: layout err $scheme"
		exit 1
	}
	err=$(KVSPACE="$dsn2" "$KVLANG" test 2>&1 >/dev/null)
	case "$err" in
	*"TypeError: expected numeric, got bool"*) ;;
	*)
		echo "FAIL: $scheme bool arithmetic message: $err"
		exit 1
		;;
	esac
done
echo "PASS: dyn_specialize shm/fs"
