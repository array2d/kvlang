#!/usr/bin/env bash
# B1 端到端：语义开关下 JIT2 必须接受矩阵覆盖的呼叫自由段，且结果等于解释器。
# 三档：语义+JIT（默认）、语义+RC2_NOJIT2、typed+RC2_NOJIT2；另跑纯解释器作真值。
# usage: jit_semantic_ops.sh <byteseek> <repo-root>
set -uo pipefail
BIN=${1:?usage: jit_semantic_ops.sh <byteseek> <repo-root>}
ROOT=${2:?usage: jit_semantic_ops.sh <byteseek> <repo-root>}
FIX="$ROOT/runtimec2/tests/fixtures/jit2"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fail=0
run() {  # run <tag> <kv> <env...>
    local tag=$1 kv=$2
    shift 2
    local dsn="shm://$TMP/$tag.shm"
    rm -f "$TMP/$tag.shm"*
    env KVSPACE="$dsn" "$@" "$BIN" "$kv" \
        > "$TMP/$tag.out" 2> "$TMP/$tag.err"
    echo $? > "$TMP/$tag.rc"
}

same() {  # same <tag-a> <tag-b> <what>
    if ! diff -q "$TMP/$1.out" "$TMP/$2.out" > /dev/null ||
       [ "$(cat "$TMP/$1.rc")" != "$(cat "$TMP/$2.rc")" ]; then
        echo "FAIL $3: $1 vs $2 differ"
        diff "$TMP/$1.out" "$TMP/$2.out" | head -6
        fail=1
        return
    fi
}

check_case() {  # check_case <fixture> <func>
    local name=$1 fn=$2
    run "$name-sem" "$FIX/$name.kv" \
        KVLANG_LAYOUT_SEMANTIC_OP=1 RC2_JITDBG=1
    run "$name-semno" "$FIX/$name.kv" \
        KVLANG_LAYOUT_SEMANTIC_OP=1 RC2_NOJIT2=1
    run "$name-typedno" "$FIX/$name.kv" RC2_NOJIT2=1
    run "$name-interp" "$FIX/$name.kv" RC2_NOJIT=1 RC2_NOJIT2=1

    if ! grep -q "jit2dbg accept /lib/$fn" "$TMP/$name-sem.err"; then
        echo "FAIL $name: JIT2 did not accept /lib/$fn"
        grep "lib/$fn" "$TMP/$name-sem.err" | head -4
        fail=1
    fi
    for tag in "$name-sem" "$name-semno" "$name-typedno"; do
        same "$tag" "$name-interp" "$name ($tag vs 解释器)"
    done
    echo "OK $name"
}

check_case sem_int hot
exit $fail
