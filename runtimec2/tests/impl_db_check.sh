#!/usr/bin/env bash
# 可选：本地 DB 导出存在时校验 impl 一致；不存在则跳过。
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$HERE/tools/check_impl_consistency.py" "${KVLANG_ISA_CSV:-$HERE/../../workspace/py-lua-bytecode-ref/opcodes-499.csv}"
