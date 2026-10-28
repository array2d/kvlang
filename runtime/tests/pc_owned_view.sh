#!/usr/bin/env bash
# b1b：PC owned view 固定档与跨档。
# 1) 首次写后 body_cap >= 1024、headlen=64、pow=6/flags=1、
#    a==len(body)、langtype == [a]char/utf8；
# 2) 深递归多帧运行后仍走 owned（固定档保持），可暂停/恢复。
# 说明：PC 编码按帧深/irseq 压缩，合法 PC 远小于 256，跨档路径不可达；
# 这里验证固定档与深帧场景的一致性。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
KVLANG="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}"
PROBE="${KVLANG_PC_PROBE:-$ROOT/bin/test_pc_view_probe}"
KVC="${KVSPACE_CLI:-kvspace}"
command -v "$KVC" >/dev/null 2>&1 || { echo "skip: no kvspace CLI"; exit 77; }
[ -x "$PROBE" ] || { echo "skip: no pc probe"; exit 77; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
export KVSPACE="shm://$tmp/db"
fail() { echo "FAIL: $*"; exit 1; }
"$KVLANG" -c 'print("boot")' >/dev/null 2>&1
"$LAYOUT" "$ROOT/runtime/tests/pc_long.kv" "$KVSPACE" >/dev/null 2>&1 || fail "layout"
vid=$("$KVLANG" create worker) || fail "create"
"$KVLANG" run "$vid" >/dev/null 2>&1 # 停在 deep() 底部的 paused
out=$("$PROBE" "$KVSPACE" "$vid") || fail "probe"
echo "$out"
pow=$(sed -n 's/.*pow=\([0-9]*\).*/\1/p' <<<"$out")
flags=$(sed -n 's/.*flags=\([0-9]*\).*/\1/p' <<<"$out")
a=$(sed -n 's/.* a=\([0-9]*\) .*/\1/p' <<<"$out")
b=$(sed -n 's/.* b=\([0-9]*\) .*/\1/p' <<<"$out")
hl=$(sed -n 's/.*headlen=\([0-9]*\).*/\1/p' <<<"$out")
bc=$(sed -n 's/.*body_cap=\([0-9]*\).*/\1/p' <<<"$out")
lt=$(sed -n 's/.*lt=\([^ ]*\).*/\1/p' <<<"$out")
[ "$pow" = 6 ] || fail "pow=$pow"
[ "$flags" = 1 ] || fail "flags=$flags"
[ "$hl" = 64 ] || fail "headlen=$hl"
[ "$bc" -ge 1024 ] || fail "body_cap=$bc < 1024"
[ "$a" -le "$b" ] || fail "a=$a > b=$b"
[ "$lt" = "[$a]char/utf8" ] || fail "langtype=$lt a=$a"
[ "$a" -gt 0 ] && [ "$a" -lt 256 ] || fail "PC 长度异常（a=$a）"
# 恢复并跑完
ST=$(printf '/vthread/%s/\xe2\x80\xa5status' "$vid")
"$KVC" set "$ST" "string:running" >/dev/null
timeout 30 "$KVLANG" run "$vid" >/dev/null 2>&1 || fail "resume/run"
# 3) owned 已接管后，外部把 PC 槽改成小 cap（模拟超档/不合规）：
#    runtime 重验后必须永久回退，且回退后 PC 仍写对（可继续暂停）。
"$KVLANG" run "$vid" >/dev/null 2>&1 & # 已 paused，重入后立即停
sleep 0.1
"$KVLANG" run "$vid" >/dev/null 2>&1 &
RPID=$!
"$KVC" set "$ST" "string:running" >/dev/null
sleep 0.3
"$PROBE" "$KVSPACE" "$vid" shrink >/dev/null || { kill -9 "$RPID" 2>/dev/null; fail "shrink"; }
sleep 0.3
"$KVC" set "$ST" "string:paused" >/dev/null
for _ in $(seq 1 100); do kill -0 "$RPID" 2>/dev/null || break; sleep 0.05; done
if kill -0 "$RPID" 2>/dev/null; then kill -9 "$RPID"; fail "shrink 后无法暂停（回退写错）"; fi
out=$("$PROBE" "$KVSPACE" "$vid") || fail "probe after shrink"
grep -q "body=/" <<<"$out" || fail "shrink 后 PC 不正确: $out"
echo "PASS: PC owned view fixed tier + long PC + fallback after shrink"
