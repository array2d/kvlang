#!/usr/bin/env bash
# P1b4-b1a：status 槽被外部删除/重建时，runtime 必须重绑 view 或安全回退，
# 不得停在陈旧 view 上、不得挂起。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
KVC="${KVSPACE_CLI:-kvspace}"
command -v "$KVC" >/dev/null 2>&1 || { echo "skip: no kvspace CLI"; exit 77; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export KVSPACE="shm://$tmp/db"
fail() { echo "FAIL: $*"; exit 1; }
progress() { "$KVC" get /pausetest/i 2>/dev/null | cut -f2- | sed 's/^[^:]*://'; }
wait_progress() {
	for _ in $(seq 1 200); do
		v=$(progress)
		if [ -n "$v" ] && [ "$v" != "(nil)" ] && [ "$v" -ge "$1" ] 2>/dev/null; then return 0; fi
		sleep 0.02
	done
	fail "进度未达到 $1（当前 $(progress)）"
}
"$KVLANG" -c 'print("boot")' >/dev/null 2>&1
"$LAYOUT" "$ROOT/runtime/tests/pause_resume.kv" "$KVSPACE" >/dev/null 2>&1 || fail "layout"
vid=$("$KVLANG" create worker) || fail "create"
ST=$(printf '/vthread/%s/\xe2\x80\xa5status' "$vid")
"$KVLANG" run "$vid" >/dev/null 2>&1 & RPID=$!
wait_progress 5

# A) 删除后立即重建：继续运行或安全退出，二者皆可；不得挂起。
"$KVC" del "$ST" >/dev/null 2>&1
"$KVC" set "$ST" "string:running" >/dev/null 2>&1
sleep 0.2
if kill -0 "$RPID" 2>/dev/null; then
	p1=$(progress)
	"$KVC" set "$ST" "string:paused" >/dev/null 2>&1
	for _ in $(seq 1 100); do kill -0 "$RPID" 2>/dev/null || break; sleep 0.05; done
	if kill -0 "$RPID" 2>/dev/null; then kill -9 "$RPID"; fail "删除/重建后无法暂停"; fi
	echo "rebind: 继续运行并可暂停（进度 $p1）"
else
	wait "$RPID" 2>/dev/null
	echo "rebind: 删除窗口内安全回退退出"
fi

# B) 只删除不重建：必须安全退出，不得挂起。
"$KVC" set "$ST" "string:running" >/dev/null 2>&1
"$KVLANG" run "$vid" >/dev/null 2>&1 & RPID=$!
wait_progress $(( $(progress) + 5 ))
"$KVC" del "$ST" >/dev/null 2>&1
for _ in $(seq 1 100); do kill -0 "$RPID" 2>/dev/null || break; sleep 0.05; done
if kill -0 "$RPID" 2>/dev/null; then kill -9 "$RPID"; fail "status 被删后仍在运行"; fi
echo "PASS: status 删除/重建后重绑或安全回退"
