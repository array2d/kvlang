#!/usr/bin/env bash
# 下载 ABI 依赖（deps.json: repo → tag），安装到 /usr/lib + /usr/lib/kvspace + /usr/include + /usr/bin。
# 本地与 CI 共用。tarball 可选带 bin/（如 kvspace 的 CLI）。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
for repo in $(jq -r 'keys[]' "$ROOT/deps.json"); do
  ver=$(jq -r ".\"$repo\"" "$ROOT/deps.json")
  tmp="$(mktemp -d)"
  gh release download "$ver" -R "array2d/$repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ -d "$tmp/include" ]; then sudo cp -r "$tmp/include/"* /usr/include/; fi
  if [ "$repo" = "kvspace-c" ] || [ "$repo" = "kvspace-durable" ]; then
    sudo mkdir -p /usr/lib/kvspace
    sudo cp "$tmp/lib/"*.so* /usr/lib/kvspace/
  elif [ -d "$tmp/lib" ]; then
    # blockmalloc / slotsboxmalloc 是 header-only：只有头、无 lib
    sudo cp "$tmp/lib/"*.so* /usr/lib/
  fi
  # 可选可执行（kvspace 的 CLI）→ /usr/bin
  if [ -d "$tmp/bin" ]; then
    sudo cp "$tmp/bin/"* /usr/bin/
    sudo chmod 755 /usr/bin/"$(ls "$tmp/bin/" | head -1)"
  fi
  rm -rf "$tmp"
done
echo "✅ ABI deps → /usr/lib + /usr/lib/kvspace + /usr/include + /usr/bin"
