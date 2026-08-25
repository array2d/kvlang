#!/usr/bin/env bash
# 下载 ABI 依赖（deps.json: repo → tag 或 owner/repo@tag），安装到 PREFIX。
# 本地与 CI 共用。PREFIX 默认 /usr；非 /usr 不使用 sudo。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${KVSPACE_ABI_PREFIX:-/usr}"
if [ "$PREFIX" = /usr ]; then SUDO=sudo; else SUDO=; fi
$SUDO mkdir -p "$PREFIX/lib" "$PREFIX/include" "$PREFIX/lib/kvspace"
for repo in $(jq -r 'keys[]' "$ROOT/deps.json"); do
  spec=$(jq -r --arg r "$repo" '.[$r]' "$ROOT/deps.json")
  if [[ "$spec" == *@* ]]; then
    owner_repo="${spec%@*}"
    ver="${spec##*@}"
  else
    owner_repo="array2d/$repo"
    ver="$spec"
  fi
  tmp="$(mktemp -d)"
  gh release download "$ver" -R "$owner_repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ -d "$tmp/include" ]; then $SUDO cp -r "$tmp/include/"* "$PREFIX/include/"; fi
  $SUDO cp "$tmp/lib/"*.so* "$PREFIX/lib/"
  # dispatch 前端默认 dlopen $PREFIX/lib/kvspace/<soname>
  $SUDO cp "$tmp/lib/"*.so* "$PREFIX/lib/kvspace/"
  # durable v0.2.1 ships libkvspace_durable.so without SONAME .1
  if [ -e "$PREFIX/lib/kvspace/libkvspace_durable.so" ] && [ ! -e "$PREFIX/lib/kvspace/libkvspace_durable.so.1" ]; then
    $SUDO ln -s libkvspace_durable.so "$PREFIX/lib/kvspace/libkvspace_durable.so.1"
  fi
  rm -rf "$tmp"
done
echo "✅ ABI deps → $PREFIX/lib + $PREFIX/include"
