#!/usr/bin/env bash
# typed 槽写侧转换（[[赋值转换]]）：标量/容器元素/struct 字段/指针写按声明 kind 编码；
# 动态 kvspace·set 按原 kind 写入；不可转换 → 运行时 TypeError。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

want=$(cat <<'OUT'
i8 -56
u8 200
f32 0.10000000149011612
f64 0.1
arr -56 2
set -56
append -56
map -56
field -56
ptr -56
dynmap 300
dynkv 300
OUT
)

for scheme in shm fs; do
	dsn="$scheme://$tmp/$scheme"
	if ! "$LAYOUT" "$ROOT/runtime/tests/coercion.kv" "$dsn" >/dev/null 2>&1; then
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
	"$LAYOUT" "$ROOT/runtime/tests/coercion_err.kv" "$dsn2" >/dev/null 2>&1 || {
		echo "FAIL: layout err $scheme"
		exit 1
	}
	err=$(KVSPACE="$dsn2" "$KVLANG" test 2>&1 >/dev/null)
	case "$err" in
	*"TypeError: cannot assign char/utf32 to int8"*) ;;
	*)
		echo "FAIL: $scheme mismatch message: $err"
		exit 1
		;;
	esac
done
echo "PASS: coercion shm/fs"
