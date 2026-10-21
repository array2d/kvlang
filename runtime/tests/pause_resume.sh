#!/usr/bin/env bash
# pause/resume/done：另一个 handle 改 /vthread/<vid>‥status，运行中的 kvcpu 必须观测到。
# durable（fs）后端：控制键是 volatile（每次 Get 重读），普通 key 才是快照。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
KVC="${KVSPACE_CLI:-kvspace}"
command -v "$KVC" >/dev/null 2>&1 || { echo "FAIL: 需要 kvspace CLI（设置 KVSPACE_CLI）"; exit 1; }

tmp=$(mktemp -d)
if [ -n "${PAUSE_RESUME_DSN:-}" ]; then
	export KVSPACE="$PAUSE_RESUME_DSN"
else
	export KVSPACE="fs://$tmp"
fi
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT

fail() { echo "FAIL: $*"; exit 1; }
progress() { "$KVC" get /pausetest/i 2>/dev/null | cut -f2- | sed 's/^[^:]*://'; }
set_status() { "$KVC" set "$STKEY" "string:$1" >/dev/null; }

"$LAYOUT" "$ROOT/runtime/tests/pause_resume.kv" "$KVSPACE" >/dev/null 2>&1 || fail "layout"
vid=$("$KVLANG" create worker) || fail "create"
STKEY=$(printf '/vthread/%s/\xe2\x80\xa5status' "$vid")
echo "vid=$vid"

run_bg() { "$KVLANG" run "$vid" >/dev/null 2>&1 & RPID=$!; }
wait_exit() { # $1=描述；最多等 5s
	for _ in $(seq 1 100); do
		kill -0 "$RPID" 2>/dev/null || return 0
		sleep 0.05
	done
	kill -9 "$RPID" 2>/dev/null
	fail "$1：运行进程未按外部 status 停止"
}
wait_progress() { # $1=下限
	for _ in $(seq 1 200); do
		v=$(progress)
		if [ -n "$v" ] && [ "$v" != "(nil)" ] && [ "$v" -ge "$1" ] 2>/dev/null; then return 0; fi
		sleep 0.02
	done
	fail "进度未达到 $1（当前 $(progress)）"
}

run_bg
wait_progress 5
# 外部暂停：进程必须停，且进度不再增长
set_status paused
wait_exit "pause"
p1=$(progress); sleep 0.3; p2=$(progress)
[ "$p1" = "$p2" ] || fail "暂停后进度仍在增长（$p1 -> $p2）"
echo "paused at $p1"

# 外部恢复：继续推进
set_status running
run_bg
wait_progress $((p1 + 5))
echo "resumed past $((p1 + 5))"

# done 也必须被观测
set_status done
wait_exit "done"
echo "done observed"

# error 也必须被观测：重跑并置 error
set_status running
run_bg
wait_progress $((p1 + 10))
set_status error
wait_exit "error"
echo "error observed"

echo "PASS: paused / running / done / error 均由外部 handle 观测"
