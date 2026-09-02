#!/usr/bin/env bash
# kvlang「runtime 崩溃可随时恢复」端到端验证。
# 核心承诺：vthread 全部执行态（pc/帧/栈）活在 kvspace，runtime 进程无内存态；
# 进程随时被 SIGKILL，另起一个进程即可从持久化 pc 续跑，不重启、不重演已完成步骤。
# 需 redis 后端（状态须活过被杀进程）。
set -u
export KVSPACE="${KVSPACE:-redis://127.0.0.1:6379}"
KV=/usr/bin/kvspace
KVLANG=/usr/bin/kvlang
DIR="$(cd "$(dirname "$0")" && pwd)"
PROG="$DIR/02-crashrecover.kv"
PC_KEY=$(printf '/vthread/%%s/\xe2\x80\xa5pc')       # ‥pc, U+2025
ST_KEY=$(printf '/vthread/%%s/\xe2\x80\xa5status')

val() { "$KV" get "$1" 2>/dev/null | cut -f2- | sed 's/^[^:]*://'; }  # 输出 "key<TAB>kind:value" → 剥 kind:
fail() { echo "FAIL: $*"; exit 1; }

echo "== 清空 kvspace + /crashtest =="
"$KV" clear >/dev/null 2>&1
"$KV" deltree /crashtest >/dev/null 2>&1

echo "== Phase1：启动 worker，跑到 step3 时外部 SIGKILL =="
ERR=$(mktemp); OUT1=$(mktemp)
LOG_LEVEL=info "$KVLANG" "$PROG" >"$OUT1" 2>"$ERR" &  # info 级才吐 "vthread <vid>"，供 resume 捕获
PID=$!
# 等 vthread id 出现在 stderr
vid=""
for _ in $(seq 1 50); do
	vid=$(grep -m1 -oE 'vthread [0-9]+' "$ERR" | awk '{print $2}')
	[ -n "$vid" ] && break
	sleep 0.05
done
[ -n "$vid" ] || fail "未捕获 vthread id（stderr: $(cat "$ERR")）"
echo "  vthread=$vid  pid=$PID"
# 轮询到 step==3，再稍等落进 sleep 窗口，然后杀
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

echo "== Phase2：另起进程 kvlang resume $vid，从持久化 pc 续跑 =="
OUT2=$(mktemp)
"$KVLANG" resume "$vid" >"$OUT2" 2>/dev/null
rc2=$?
[ "$rc2" = "0" ] || fail "resume 退出码 $rc2"
step2=$(val /crashtest/step)
done2=$(val /crashtest/done)
st2=$(val "$(printf "$ST_KEY" "$vid")")
echo "  续跑后: step=$step2 done=$done2 status=$st2"
echo "  --- Phase2 stdout ---"; sed 's/^/    /' "$OUT2"
[ "$step2" = "5" ] || fail "续跑后 step 应为 5，实为 $step2"
[ "$done2" = "1" ] || fail "续跑后应 done=1，实为 $done2"
[ "$st2" = "ok" ] || fail "续跑后 status 应为 ok（done 态），实为 $st2"
# 铁证：Phase2 只补打 step 4/5，绝不重演已完成的 1..3
grep -q '^step 4$' "$OUT2" || fail "resume 未补跑 step 4"
grep -q '^step 5$' "$OUT2" || fail "resume 未补跑 step 5"
grep -qE '^step [123]$' "$OUT2" && fail "resume 重演了已完成步骤（应续跑而非重启）"
grep -q '^complete final= 5$' "$OUT2" || fail "resume 未正常收尾"

rm -f "$ERR" "$OUT1" "$OUT2"
echo
echo "PASS: 进程被 SIGKILL 后，另一进程从 kvspace 持久化 pc 无缝续跑；step 1..3 未重演，4..5 补齐至完成。"
