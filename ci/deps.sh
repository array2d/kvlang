#!/usr/bin/env bash
# 下载 ABI 依赖（deps.json: repo → tag）到 libso/。本地与 CI 共用。
# 需 gh 已登录；CI 中 GH_TOKEN 自动可用。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/libso"
for repo in $(jq -r 'keys[]' "$ROOT/deps.json"); do
  ver=$(jq -r ".\"$repo\"" "$ROOT/deps.json")
  tmp="$(mktemp -d)"
  gh release download "$ver" -R "array2d/$repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$ROOT/libso" --strip-components=1
  rm -rf "$tmp"
done
echo "✅ ABI deps → libso/: $(ls "$ROOT/libso" 2>/dev/null | tr '\n' ' ')"
