#!/usr/bin/env bash
set -u
if [ -z "${PATHSPACE:-}" ]; then
  echo "Missing PATHSPACE: DSN is required with no default." >&2
  echo "Usage: PATHSPACE=shm:///tmp/crashrecover.shm $0" >&2
  exit 2
fi
export PATHSPACE
KV=/usr/bin/pathspace
PATHLANG=/usr/bin/pathlang
DIR="$(cd "$(dirname "$0")" && pwd)"
PROG="$DIR/02-crashrecover.kv"
PC_KEY=$(printf '/vthread/%%s/\xe2\x80\xa5pc')       # ‥pc, U+2025
ST_KEY=$(printf '/vthread/%%s/\xe2\x80\xa5status')

val() { "$KV" get "$1" 2>/dev/null | cut -f2- | sed 's/^[^:]*://'; }  # output "key<TAB>kind:value" -> strip kind:
fail() { echo "FAIL: $*"; exit 1; }

echo "== Clear pathspace + /crashtest =="
"$KV" clear >/dev/null 2>&1
"$KV" deltree /crashtest >/dev/null 2>&1

echo "== Phase 1: launch worker, send external SIGKILL at step 3 =="
ERR=$(mktemp); OUT1=$(mktemp)
LOG_LEVEL=info "$PATHLANG" "$PROG" >"$OUT1" 2>"$ERR" &  # info level emits "vthread <vid>" for resume capture
PID=$!
vid=""
for _ in $(seq 1 50); do
	vid=$(grep -m1 -oE 'vthread [0-9]+' "$ERR" | awk '{print $2}')
	[ -n "$vid" ] && break
	sleep 0.05
done
[ -n "$vid" ] || fail "failed to capture vthread id (stderr: $(cat "$ERR"))"
echo "  vthread=$vid  pid=$PID"
got=0
for _ in $(seq 1 100); do
	s=$(val /crashtest/step)
	if [ "$s" = "3" ]; then got=1; sleep 0.08; break; fi
	sleep 0.03
done
[ "$got" = "1" ] || fail "worker did not reach step 3 (step=$(val /crashtest/step))"
kill -9 "$PID" 2>/dev/null
wait "$PID" 2>/dev/null; rc=$?
echo "  sent SIGKILL, exit code=$rc (137 = SIGKILL)"

pc1=$(val "$(printf "$PC_KEY" "$vid")")
st1=$(val "$(printf "$ST_KEY" "$vid")")
step1=$(val /crashtest/step)
done1=$(val /crashtest/done)
echo "  at crash: step=$step1 done=[$done1] status=$st1 pc=$pc1"
[ "$step1" = "3" ]        || fail "step at crash should be 3, got $step1"
[ "$done1" = "(nil)" ] || [ -z "$done1" ] || fail "done should not be set at crash (done=$done1)"
[ -n "$pc1" ] && [ "$pc1" != "(nil)" ] || fail "pc at crash should be non-empty (mid-flight)"
grep -q '^step 3$' "$OUT1" || fail "Phase 1 stdout should contain step 3"
grep -q '^step 4$' "$OUT1" && fail "Phase 1 should not reach step 4"

echo "== Phase 2: spawn new process 'pathlang run $vid' to resume from persisted pc =="
OUT2=$(mktemp)
"$PATHLANG" run "$vid" >"$OUT2" 2>/dev/null
rc2=$?
[ "$rc2" = "0" ] || fail "run exited with status $rc2"
step2=$(val /crashtest/step)
done2=$(val /crashtest/done)
st2=$(val "$(printf "$ST_KEY" "$vid")")
echo "  after resume: step=$step2 done=$done2 status=$st2"
echo "  --- Phase 2 stdout ---"; sed 's/^/    /' "$OUT2"
[ "$step2" = "5" ] || fail "step after resume should be 5, got $step2"
[ "$done2" = "1" ] || fail "done after resume should be 1, got $done2"
[ "$st2" = "ok" ] || fail "status after resume should be ok (done state), got $st2"
grep -q '^step 4$' "$OUT2" || fail "resume did not run step 4"
grep -q '^step 5$' "$OUT2" || fail "resume did not run step 5"
grep -qE '^step [123]$' "$OUT2" && fail "resume replayed completed steps (should resume, not restart)"
grep -q '^complete final= 5$' "$OUT2" || fail "resume did not finish cleanly"

rm -f "$ERR" "$OUT1" "$OUT2"
echo
echo "PASS: after SIGKILL, a new process resumed seamlessly from persisted pathspace pc; steps 1..3 were not replayed and 4..5 completed."
