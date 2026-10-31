#!/usr/bin/env bash
# myrwircap 表即代码：校验表项形状与实现载体（内联 / rwir handler id）。
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
exec python3 "$HERE/tools/check_impl_consistency.py"
