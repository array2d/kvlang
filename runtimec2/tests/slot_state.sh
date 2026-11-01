#!/usr/bin/env bash
# 槽状态（Register::SlotState）正确性：Absent 只允许跳过「确定无槽」的类型查询。
# 用例：
#   1. float64 槽后写 int64 不得被静默改写/跳过；
#   2. 帧局部 int64 纯内存重复读写正确；成员读结果写 int64 走快路径
#      （absent_write_hits > 0）；
#   3. 帧池复用不得泄漏上一层的槽状态；
# 用例 4（handler 直写 kvspace 后同槽写须重新查类型）走纯状态机单测：
#   tests/slot_state_test.cpp（c2_slot_state_unit）。
set -euo pipefail

M1=${1:?usage: slot_state.sh <m1_smoke> [byteseek]}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
KVLANG=${2:-${KVLANG_BIN:-"$ROOT/bin/byteseek"}}

for path in "$M1" "$KVLANG"; do
    if [[ ! -e "$path" ]]; then
        echo "FAIL: missing test dependency: $path" >&2
        exit 1
    fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
dsn="shm://$tmp/slot.shm"
source_file="$tmp/slot.kv"

cat >"$source_file" <<'KV'
rwfunc slot_float_then_int() -> () {
    x = 1.5
    print(x)
    x = 2
    print(x)
}

rwfunc slot_local_int_loop() -> () {
    total = 0
    i = 0
    while (i < 5) {
        total = total + i
        i = i + 1
    }
    print(total)
}

rwfunc slot_member_read_loop() -> () {
    h:[int64]·int64 = {}
    0 -> i
    while (i < 5) {
        i -> h·*i
        i + 1 -> i
    }
    0 -> i
    0 -> total
    while (i < 5) {
        h·*i -> v
        total + v -> total
        i + 1 -> i
    }
    print(total)
}

rwfunc slot_child_int() -> (r:int64) {
    t = 7
    t -> r
}

rwfunc slot_reuse_float_then_int() -> () {
    t = 1.5
    t = 2
    print(t)
}

rwfunc slot_frame_reuse() -> () {
    slot_child_int() -> a
    slot_reuse_float_then_int()
    slot_child_int() -> b
    print(b)
}
KV

set +e
env KVSPACE="$dsn" "$KVLANG" "$source_file" >"$tmp/layout.out" 2>"$tmp/layout.err"
layout_rc=$?
set -e
if [[ $layout_rc -ne 0 ]] && ! grep -q 'bootstrap test' "$tmp/layout.err"; then
    cat "$tmp/layout.err" >&2
    echo "FAIL: fixture layout failed unexpectedly" >&2
    exit 1
fi

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

run() {
    local function=$1
    shift
    local want=("$@")
    local out
    out=$(RC2_SLOTSTATE_STATS=1 "$M1" "$dsn" "$function" 2>"$tmp/$function.err") ||
        fail "$function: rc=$? ($(tail -n 3 "$tmp/$function.err" | tr '\n' ' '))"
    local got=()
    mapfile -t got <<<"$out"
    if [[ "${got[*]}" != "${want[*]}" ]]; then
        fail "$function: got [${got[*]}] want [${want[*]}]"
    fi
}

run slot_float_then_int "1.5" "2"
run slot_local_int_loop "10"
run slot_member_read_loop "10"
hits=$(sed -n 's/.*absent_write_hits=\([0-9]*\).*/\1/p' "$tmp/slot_member_read_loop.err")
[[ -n $hits && $hits -gt 0 ]] ||
    fail "slot_member_read_loop: absent_write_hits=$hits (纯内存快路径未命中)"
run slot_frame_reuse "2" "7"

echo "OK slot state: float64→int64, pure-memory int64, frame reuse"
