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
for repo in $(jq -r 'keys[]' "$ROOT/deps.json"); do
  ver=$(jq -r ".\"$repo\"" "$ROOT/deps.json")
  tmp="$(mktemp -d)"
  gh release download "$ver" -R "array2d/$repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ "$repo" = "kvspace-durable" ]; then
    # Backend tarballs may bundle build-time kvspace headers; use headers from kvspace only.
    cp -P "$tmp/lib/"*.so* "$ROOT/lib/kvspace/"
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
