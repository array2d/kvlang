#!/usr/bin/env bash
set -u
if [ -z "${PATHSPACE:-}" ]; then
  echo "缺少 PATHSPACE：DSN 必填、无默认值。" >&2
  echo "用法：PATHSPACE=shm:///tmp/crashrecover.shm $0" >&2
  exit 2
fi
export PATHSPACE
KV=/usr/bin/pathspace
PATHLANG=/usr/bin/pathlang
DIR="$(cd "$(dirname "$0")" && pwd)"
PROG="$DIR/02-crashrecover.kv"
PC_KEY=$(printf '/vthread/%%s/\xe2\x80\xa5pc')       # ‥pc, U+2025
ST_KEY=$(printf '/vthread/%%s/\xe2\x80\xa5status')

val() { "$KV" get "$1" 2>/dev/null | cut -f2- | sed 's/^[^:]*://'; }  # 输出 "key<TAB>kind:value" → 剥 kind:
fail() { echo "FAIL: $*"; exit 1; }

echo "== 清空 pathspace + /crashtest =="
"$KV" clear >/dev/null 2>&1
"$KV" deltree /crashtest >/dev/null 2>&1

echo "== Phase1：启动 worker，跑到 step3 时外部 SIGKILL =="
ERR=$(mktemp); OUT1=$(mktemp)
LOG_LEVEL=info "$PATHLANG" "$PROG" >"$OUT1" 2>"$ERR" &  # info 级才吐 "vthread <vid>"，供 resume 捕获
PID=$!
vid=""
for _ in $(seq 1 50); do
	vid=$(grep -m1 -oE 'vthread [0-9]+' "$ERR" | awk '{print $2}')
	[ -n "$vid" ] && break
	sleep 0.05
done
[ -n "$vid" ] || fail "未捕获 vthread id（stderr: $(cat "$ERR")）"
echo "  vthread=$vid  pid=$PID"
got=0
for _ in $(seq 1 100); do
	s=$(val /crashtest/step)
	if [ "$s" = "3" ]; then got=1; sleep 0.08; break; fi
	sleep 0.03
done
[ "$got" = "1" ] || fail "worker 未推进到 step3（step=$(val /crashtest/step)）"
kill -9 "$PID" 2>/dev/null
wait "$PID" 2>/dev/null; rc=$?
echo "  已 SIGKILL，退出码=$rc（137=被 SIGKILL）"

pc1=$(val "$(printf "$PC_KEY" "$vid")")
st1=$(val "$(printf "$ST_KEY" "$vid")")
step1=$(val /crashtest/step)
done1=$(val /crashtest/done)
echo "  崩溃时: step=$step1 done=[$done1] status=$st1 pc=$pc1"
[ "$step1" = "3" ]        || fail "崩溃时 step 应为 3，实为 $step1"
[ "$done1" = "(nil)" ] || [ -z "$done1" ] || fail "崩溃时不应已 done（done=$done1）"
[ -n "$pc1" ] && [ "$pc1" != "(nil)" ] || fail "崩溃时 pc 应非空（半途）"
grep -q '^step 3$' "$OUT1" || fail "Phase1 stdout 应含到 step 3"
grep -q '^step 4$' "$OUT1" && fail "Phase1 不应跑到 step 4"

echo "== Phase2：另起进程 pathlang run $vid，从持久化 pc 续跑 =="
OUT2=$(mktemp)
"$PATHLANG" run "$vid" >"$OUT2" 2>/dev/null
rc2=$?
[ "$rc2" = "0" ] || fail "run 退出码 $rc2"
step2=$(val /crashtest/step)
done2=$(val /crashtest/done)
st2=$(val "$(printf "$ST_KEY" "$vid")")
echo "  续跑后: step=$step2 done=$done2 status=$st2"
echo "  --- Phase2 stdout ---"; sed 's/^/    /' "$OUT2"
[ "$step2" = "5" ] || fail "续跑后 step 应为 5，实为 $step2"
[ "$done2" = "1" ] || fail "续跑后应 done=1，实为 $done2"
[ "$st2" = "ok" ] || fail "续跑后 status 应为 ok（done 态），实为 $st2"
grep -q '^step 4$' "$OUT2" || fail "resume 未补跑 step 4"
grep -q '^step 5$' "$OUT2" || fail "resume 未补跑 step 5"
grep -qE '^step [123]$' "$OUT2" && fail "resume 重演了已完成步骤（应续跑而非重启）"
grep -q '^complete final= 5$' "$OUT2" || fail "resume 未正常收尾"

rm -f "$ERR" "$OUT1" "$OUT2"
echo
echo "PASS: 进程被 SIGKILL 后，另一进程从 pathspace 持久化 pc 无缝续跑；step 1..3 未重演，4..5 补齐至完成。"
