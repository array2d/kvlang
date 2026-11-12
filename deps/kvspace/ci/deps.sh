#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

BLOCKMALLOC_VERSION=v0.1.5
SLOTSBOXMALLOC_VERSION=v0.1.7

fetch_headers() {
  local repo=$1 ver=$2
  local tmp; tmp="$(mktemp -d)"
  gh release download "$ver" -R "array2d/$repo" \
     -p "${repo}-abi-*-linux-x86_64.tar.gz" -D "$tmp"
  tar xzf "$tmp"/*.tar.gz -C "$tmp" --strip-components=1
  if [ -d "$tmp/include" ]; then sudo cp -r "$tmp/include/"* /usr/include/; fi
  rm -rf "$tmp"
}

fetch_headers blockmalloc "$BLOCKMALLOC_VERSION"
fetch_headers slotsboxmalloc "$SLOTSBOXMALLOC_VERSION"
echo "✅ ABI deps → /usr/include（blockmalloc, slotsboxmalloc）"
