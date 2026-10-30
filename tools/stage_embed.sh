#!/usr/bin/env bash
# stage_embed.sh — 为 #493 单一二进制准备 embed payload 目录。
#
# 布局（build.rs 固定从 build/embed 查找，不读构建环境变量）：
#   <out>/libkvspace.so.1              dispatch 前端（必须含 RTLD_NOLOAD 能力标记：
#                                      用本分支/最新 kvspace 构建，不要用 v0.3.18 旧前端）
#   <out>/libkvlang_runtimec2.so       本仓本次构建（lib/，默认 c2 runtime）
#   <out>/libkvlang_runtime.so         （可选 A/B：存在则一并带，build.rs 优先取 c2）
#   <out>/libkvlanglayout.so           本仓本次构建（lib/）
#   <out>/kvspace/libkvspace-c.so.1    shm 后端（deps.json pin）
#   <out>/kvspace/libkvspace_durable.so.1  durable 后端（deps.json pin）
#
# 用法：tools/stage_embed.sh <libkvspace.so.1> <outdir> <deps前缀>
#   deps前缀 = repo 内 {lib,lib/kvspace} 所在目录（如 kvlang/）。
set -euo pipefail
FRONTEND=${1:?用法: tools/stage_embed.sh <libkvspace.so.1> <outdir> <deps前缀>}
OUT=${2:?用法: tools/stage_embed.sh <libkvspace.so.1> <outdir> <deps前缀>}
DEPS=${3:?用法: tools/stage_embed.sh <libkvspace.so.1> <outdir> <deps前缀>}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# p0：前端必须带 RTLD_NOLOAD 能力标记（旧前端会静默回退到系统目录）。宁可构建失败。
if ! grep -aq "kvspace:embed-noload" "$FRONTEND"; then
    echo "stage_embed: $FRONTEND 不含 RTLD_NOLOAD 能力标记（旧前端）" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/kvspace"
cp "$ROOT/lib/libkvlang_runtimec2.so" "$ROOT/lib/libkvlanglayout.so" "$OUT/"
# A/B 可选臂：C 版存在则一并带（embed 默认 c2，见 build.rs）。
[ -f "$ROOT/lib/libkvlang_runtime.so" ] && cp "$ROOT/lib/libkvlang_runtime.so" "$OUT/" || true
cp "$FRONTEND" "$OUT/libkvspace.so.1"
cp -P "$DEPS"/lib/kvspace/libkvspace-c.so* "$DEPS"/lib/kvspace/libkvspace_durable.so* "$OUT/kvspace/"
echo "embed payload: $OUT"
ls -l "$OUT" "$OUT/kvspace"
