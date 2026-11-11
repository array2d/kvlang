#!/usr/bin/env bash
# 下载 ABI 依赖（deps.json: repo → tag），安装到 repo 内（不装 /usr）：
#   头   → <root>/include/
#   前端 → <root>/lib/
#   后端 → <root>/lib/kvspace/（kvspace-durable）
#   可执行 → <root>/bin/
# 本地与 CI 共用。
#
# 没有 kvspace-c：该仓已冻结，shm 引擎并入 kvspace 仓，随 libkvspace 一起发布。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/include" "$ROOT/lib/kvspace" "$ROOT/bin"

# 依赖清单用 python3 读，不用 jq：jq 不是必装项，缺了会让整段静默空转（见下）。
#
# **为什么把结果先取到变量再检查**：`set -e` 不捕获**命令替换**的失败——它只看
# `for` 语句本身的退出码。曾经写成 `for repo in $(jq -r 'keys[]' ...)`，缺 jq 时
# 命令替换失败、循环体一次都没跑，脚本却照样打印 ✅ 并 exit 0。那是 p1 级假成功：
# 什么都没装，报成功。所以每一次取值都 `|| { ...; exit 1; }`。
repos=$(python3 - "$ROOT/deps.json" <<'PY'
import json, sys
print("\n".join(json.load(open(sys.argv[1])).keys()))
PY
) || { echo "读取 deps.json 失败：$ROOT/deps.json" >&2; exit 1; }

for repo in $repos; do
  ver=$(python3 - "$ROOT/deps.json" "$repo" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))[sys.argv[2]])
PY
  ) || { echo "deps.json 里缺少 $repo 的版本" >&2; exit 1; }
  tmp="$(mktemp -d)"
  gh release download "$ver" -R "array2d/$repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ "$repo" = "kvspace-durable" ]; then
    # Backend tarballs may bundle build-time kvspace headers; use headers from kvspace only.
    cp -P "$tmp/lib/"*.so* "$ROOT/lib/kvspace/"
  elif [ "$repo" = "kvspace" ]; then
    # **不拷 include/**：kvspace 的头只有一个定义点，即 `KVSPACE_SRC/include`
    # （Makefile 的 `KVSPACE_INCLUDE_DIRECTORY` 由 `KVSPACE_SRC` 派生）。
    # tarball 里的 `include/kvspace/` 一旦落进 repo，`runtimec/CMakeLists.txt` 的
    # stale-copy 闸（`:42-47`）会让 `make runtimec-lib` 必红——它专门拦这一份副本。
    cp -P "$tmp/lib/"*.so* "$ROOT/lib/"
  else
    if [ -d "$tmp/include" ]; then cp -r "$tmp/include/"* "$ROOT/include/"; fi
    if [ -d "$tmp/lib" ]; then
      # blockmalloc / slotsboxmalloc are header-only (no shared libs).
      cp -P "$tmp/lib/"*.so* "$ROOT/lib/"
    fi
  fi
  # 可选可执行（kvspace 的 CLI）→ bin/
  if [ -d "$tmp/bin" ]; then
    cp "$tmp/bin/"* "$ROOT/bin/"
    chmod 755 "$ROOT/bin/"*
  fi
  rm -rf "$tmp"
done
echo "✅ ABI deps → $ROOT/{include,lib,lib/kvspace,bin}"
