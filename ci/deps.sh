#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="${1:-}"
if [ -z "$PREFIX" ]; then
  echo "用法: $0 <prefix>（Linux: /usr；macOS: /usr/local）" >&2
  exit 2
fi
mkdir -p "$PREFIX/include" "$PREFIX/lib" "$PREFIX/bin"

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
  gh release download "$ver" -R "$repo" -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ -d "$tmp/include" ]; then cp -r "$tmp/include/"* "$PREFIX/include/"; fi
  if [ -d "$tmp/lib" ]; then
    cp -P "$tmp/lib/"*.so* "$PREFIX/lib/"
  fi
  if [ -d "$tmp/bin" ]; then
    install -m 755 "$tmp/bin/"* "$PREFIX/bin/"
  fi
  rm -rf "$tmp"
done
echo "✅ ABI deps → $PREFIX/{include,lib,bin}"
