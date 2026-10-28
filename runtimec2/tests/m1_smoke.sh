#!/usr/bin/env bash
# M1 端到端：layout 一份 print("hello") 的程序，再用 runtimec2 的驱动跑一遍。
set -euo pipefail
BIN=${1:?usage: m1_smoke.sh <m1_smoke> [kvlang]}
KVLANG=${2:-${KVLANG_BIN:-/usr/bin/kvlang}}
STORE=$(mktemp -d)/m1.shm
trap 'rm -rf "$(dirname "$STORE")"' EXIT
KVSPACE="shm://$STORE" "$KVLANG" -c 'print("hello")' >/dev/null 2>&1 || true
out=$("$BIN" "shm://$STORE" init)
if [ "$out" != "hello" ]; then
    echo "FAIL: expected hello, got '$out'"
    exit 1
fi
echo "OK m1 print(hello)"
