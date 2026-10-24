#!/usr/bin/env bash
# P1b4-b1a：status 只读 slot view 路径（shm）必须观测外部 paused/running/done/error。
# 复用 pause_resume 的全流程，只把后端固定为 shm（view 生效路径）。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
PAUSE_RESUME_DSN="shm://$tmp/sv" \
KVLANG_LAYOUT_BIN="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}" \
KVLANG_TERM_BIN="${KVLANG_TERM_BIN:-$ROOT/bin/kvlang}" \
KVSPACE_CLI="${KVSPACE_CLI:-kvspace}" \
	bash "$ROOT/runtime/tests/pause_resume.sh"
