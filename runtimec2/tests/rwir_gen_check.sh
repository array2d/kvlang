#!/usr/bin/env bash
# 生成物一致：src/rwir_gen.cpp 必须等于从 native 清单重新生成的结果。
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
python3 "$HERE/tools/gen_ops.py" >"$tmp"
if ! diff -u "$HERE/src/rwir_gen.cpp" "$tmp"; then
    echo "FAIL: src/rwir_gen.cpp is stale (regenerate with tools/gen_ops.py)"
    exit 1
fi
echo "OK ops_gen matches native list"
