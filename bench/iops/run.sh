#!/usr/bin/env bash
# IOPS floor (#204): Rust / Python / kvspace Get+Set, same a=a+1 loop.
#   IOPS_N=1000000 ./bench/iops/run.sh          # default (CI-sized)
#   IOPS_N=100000000 ./bench/iops/run.sh        # issue-sized (slow)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# 依赖（前端/后端）在 repo 内 lib/ 与 lib/kvspace/（不装 /usr）。
# 需要别的前缀时显式 DEPS=…（该前缀下必须有 lib/ 与 lib/kvspace/）。
DEPS="${DEPS:-$ROOT}"
[[ -e "$DEPS/lib/libkvspace.so" ]] || {
  echo "bench/iops: $DEPS/lib/libkvspace.so not found (set DEPS to a prefix with lib/)" >&2
  exit 1
}
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/.run"
mkdir -p "$OUT"

export IOPS_N="${IOPS_N:-1000000}"
# 后端目录固定（/usr/lib/kvspace 或 deps 前缀下的 lib/kvspace），无运行期 env；
# LD_LIBRARY_PATH 只用于让循环程序找到 deps 前缀里的前端/后端。
LIBS="$DEPS/lib:$DEPS/lib/kvspace"
export LD_LIBRARY_PATH="$LIBS:$ROOT/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [[ -z "${KVSPACE:-}" ]]; then
  export KVSPACE="shm://${OUT}/kv.shm"
fi
rm -f "${OUT}/kv.shm" "${OUT}/kv.shm.sbo.head" "${OUT}/kv.shm.sbo.data"
echo "IOPS_N=$IOPS_N KVSPACE=$KVSPACE"

rustc -O -o "$OUT/loop-rust" "$HERE/loop.rs"
cc -O2 -o "$OUT/loop-kv" "$HERE/kv.c" \
  -I "$DEPS/include" -L "$DEPS/lib" -lkvspace -ldl

"$OUT/loop-rust"
python3 "$HERE/loop.py"
"$OUT/loop-kv"
