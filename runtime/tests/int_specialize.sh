#!/usr/bin/env bash
# 整数族同型特化：结果必须与通用实现逐字节一致（含回退路径与边界）。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
boot() { KVSPACE="$1" "$KVLANG" -c 'print("boot")' >/dev/null 2>&1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

want=$(cat <<'OUT'
i8wrapl -128
i8div -128
i8mod 0
i8neg -128
i8abs -128
i8sub -56
i8mul -16
i8gt true
i8eq false
i16wrap -32768
i16div -32768
i32wrap -2147483648
i32div -2147483648
u8wrap 0
u8sub 255
u8div 66
u8mod 2
u8gt true
u16wrap 0
u32wrap 0
u64max 18446744073709551615
u64div 0
u64mod 18446744073709551615
u64gt true
u64shr 18446744073709551615
u64and 1
u64xor 18446744073709551614
u64wrap 0
mixed 310
OUT
)

for scheme in shm fs; do
	dsn="$scheme://$tmp/$scheme"
	boot "$dsn"
	if ! "$LAYOUT" "$ROOT/runtime/tests/int_specialize.kv" "$dsn" >/dev/null 2>&1; then
		echo "FAIL: layout $scheme"
		exit 1
	fi
	out=$(KVSPACE="$dsn" "$KVLANG" test 2>/dev/null)
	if [ "$out" != "$want" ]; then
		echo "FAIL: $scheme output differs"
		diff <(printf '%s\n' "$want") <(printf '%s\n' "$out")
		exit 1
	fi

	dsn2="$scheme://$tmp/z-$scheme"
	boot "$dsn2"
	"$LAYOUT" "$ROOT/runtime/tests/int_zero.kv" "$dsn2" >/dev/null 2>&1 || {
		echo "FAIL: layout zero $scheme"
		exit 1
	}
	err=$(KVSPACE="$dsn2" "$KVLANG" test 2>&1 >/dev/null)
	case "$err" in
	*"ZeroDivisionError: division by zero"*) ;;
	*)
		echo "FAIL: $scheme zero-division message: $err"
		exit 1
		;;
	esac
done
echo "PASS: int_specialize shm/fs"
