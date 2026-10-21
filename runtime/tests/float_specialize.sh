#!/usr/bin/env bash
# float32 同型特化：结果必须与通用实现逐字节一致（含实际-kind 回退与收窄）。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

want=$(cat <<'OUT'
f32add 4.0
f32sub -1.0
f32mul 3.75
f32div 1.6666666269302368
f32max 2.5
f32min 1.5
f32neg -1.5
f32abs 1.5
f32lt true
f32eq false
f32round 0.30000001192092896
f32prec 16777216.0
f32uncast 4.0
mixed 1.75
sqrt64 1.224744871391589
sqrt32 1.224744871391589
nan_eq false
nan_neq true
nan_lt false
nan_max 1.0
nan32_eq false
neg_zero -0.0
abs_zero 0.0
OUT
)

for scheme in shm fs; do
	dsn="$scheme://$tmp/$scheme"
	if ! "$LAYOUT" "$ROOT/runtime/tests/float_specialize.kv" "$dsn" >/dev/null 2>&1; then
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
	"$LAYOUT" "$ROOT/runtime/tests/float_zero.kv" "$dsn2" >/dev/null 2>&1 || {
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
echo "PASS: float_specialize shm/fs"
