#!/usr/bin/env bash
# T-120 A：lazy operand 只读 view。
# 1) typed 行自身 body 快写后，后续读必须是最新值；
# 2) copy 整值写使 view 失效后，值仍必须正确；
# 3) 外部同 key/同类型/同尺寸写，恢复执行后必须可见。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
KVC="${KVSPACE_CLI:-kvspace}"
command -v "$KVC" >/dev/null 2>&1 || { echo "skip: no kvspace CLI"; exit 77; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# 1) 自身快写
out=$(KVSPACE="shm://$tmp/db1" "$KVLANG" "$ROOT/runtime/tests/read_view_ownwrite.kv" 2>/dev/null)
grep -q 'read_view_ownwrite:1000' <<<"$out" || fail "ownwrite: $out"

# 2) copy 慢写失效
out=$(KVSPACE="shm://$tmp/db2" "$KVLANG" "$ROOT/runtime/tests/read_view_invalidate.kv" 2>/dev/null)
grep -q 'read_view_invalidate:201' <<<"$out" || fail "invalidate: $out"

# 3) global/绝对 key 不提升，但值正确
out=$(KVSPACE="shm://$tmp/db3" "$KVLANG" "$ROOT/runtime/tests/read_view_global.kv" 2>/dev/null)
grep -q 'read_view_global:8' <<<"$out" || fail "global: $out"

# 4) 外部同 key 写可见（独立 store，避免沿用 global 子测试的 /viewtest/i）
export KVSPACE="shm://$tmp/db4"
"$KVLANG" -c 'print("boot")' >/dev/null 2>&1
"$LAYOUT" "$ROOT/runtime/tests/read_view_external.kv" "$KVSPACE" >/dev/null 2>&1 ||
	fail "layout"
vid=$("$KVLANG" create worker) || fail "create"
ST=$(printf '/vthread/%s/\xe2\x80\xa5status' "$vid")
XKEY="/vthread/$vid/[1]/x"
yval() { "$KVC" get /viewtest/y 2>/dev/null | sed 's/.*int64://'; }
ival() { "$KVC" get /viewtest/i 2>/dev/null | sed 's/.*int64://'; }
set_status() { "$KVC" set "$ST" "string:$1" >/dev/null; }
wait_val() {
	for _ in $(seq 1 250); do
		v=$($1)
		if [ -n "$v" ] && [ "$v" -ge "$2" ] 2>/dev/null; then return 0; fi
		sleep 0.02
	done
	fail "$3 未达到 $2（当前 $($1)）"
}
wait_stop() {
	for _ in $(seq 1 100); do
		kill -0 "$RPID" 2>/dev/null || return 0
		sleep 0.02
	done
	kill -9 "$RPID" 2>/dev/null
	fail "$1：进程未停止"
}
"$KVLANG" run "$vid" >/dev/null 2>&1 & RPID=$!
wait_val ival 20 i
set_status paused
wait_stop "pause"
[ "$(ival)" -ge 20 ] || fail "暂停前进度异常"
"$KVC" set "$XKEY" int:50000 >/dev/null || fail "set x"
set_status running
"$KVLANG" run "$vid" >/dev/null 2>&1 & RPID=$!
wait_val yval 50001 y
set_status paused
wait_stop "pause2"
echo "PASS: read view ownwrite/invalidate/external"
