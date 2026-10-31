#!/usr/bin/env bash
# Repository ISA guard always runs; a workspace CSV is an optional extra view.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
if [[ -n "${KVLANG_ISA_CSV:-}" ]]; then
    exec python3 "$HERE/tools/check_impl_consistency.py" "$KVLANG_ISA_CSV"
fi
exec python3 "$HERE/tools/check_impl_consistency.py"
