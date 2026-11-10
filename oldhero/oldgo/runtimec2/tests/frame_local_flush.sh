#!/usr/bin/env bash
# 回归：嵌套调用触发 Stop（外部 handoff/println）时，调用方帧的纯内存
# int64/bool 局部必须先落 kvspace，否则 resume 后读空。
# 用法：frame_local_flush.sh <byteseek>
set -uo pipefail
BYTESEEK=${1:?usage: frame_local_flush.sh <byteseek>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FIX="$ROOT/tests/fixtures/frame_local_flush"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/frame-flush.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

FAIL=0
check() {
	local name=$1 want=$2
	local d="$TMP/$name"
	mkdir -p "$d"
	local out
	out=$(KVSPACE="shm://$d/store.shm" "$BYTESEEK" "$FIX/$name.kv" 2>/dev/null)
	if [ "$out" = "$want" ]; then
		printf 'OK   %s\n' "$name"
	else
		printf 'FAIL %s\n  got:  %q\n  want: %q\n' "$name" "$out" "$want"
		FAIL=1
	fi
}

check local_int64 $'other\na = 7'
check local_bool $'other\nb = true'
check control_preprinted $'before = 7\nother\na = 7'

if [ "$FAIL" -ne 0 ]; then
	echo "FAILURES PRESENT"
	exit 1
fi
echo "ALL PASS"
