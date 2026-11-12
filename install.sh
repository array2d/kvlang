#!/bin/sh
set -eu

REPO=array2d/kvlang
KVSPACE_BACKEND_DIR=kvspace   # 后端与 dispatch 前端同放 <prefix>/lib/kvspace

os=$(uname -s)
arch=$(uname -m)
case "$os-$arch" in
  Linux-x86_64) plat=linux-x86_64 ;;
  Darwin-arm64) plat=darwin-arm64 ;;
  *)
    echo "kvlang: 不支持的平台 $os-$arch（已发布 linux-x86_64 / darwin-arm64）" >&2
    exit 1
    ;;
esac

if [ "$os" = Darwin ]; then
  PREFIX="${PREFIX:-/usr/local}"
  SUDO="${SUDO-sudo}"
else
  PREFIX="${PREFIX:-/usr}"
  SUDO="${SUDO-sudo}"
fi

tag="${VERSION:-}"
if [ -z "$tag" ]; then
  tag=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
        | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -1)
  [ -n "$tag" ] || { echo "kvlang: 无法获取最新版本号" >&2; exit 1; }
fi

base="${BASE_URL:-https://github.com/$REPO/releases/download/$tag}"
tarball="kvlang-abi-$tag-$plat.tar.gz"
case "$plat" in
  linux-*)  sums=SHA256SUMS-linux ;;
  darwin-*) sums=SHA256SUMS-macos ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "kvlang $tag ($plat) → $PREFIX"
curl -fsSL "$base/$tarball" -o "$tmp/$tarball"

if curl -fsSL "$base/$sums" -o "$tmp/$sums" 2>/dev/null; then
  want=$(sed -n "s/^\\([0-9a-f]\\{64\\}\\)[[:space:]]*\\**$tarball$/\\1/p" "$tmp/$sums" | head -1)
  if [ -n "$want" ]; then
    if command -v sha256sum >/dev/null 2>&1; then
      got=$(sha256sum "$tmp/$tarball" | cut -d' ' -f1)
    else
      got=$(shasum -a 256 "$tmp/$tarball" | cut -d' ' -f1)
    fi
    [ "$want" = "$got" ] || { echo "kvlang: 校验失败（sha256 不匹配）" >&2; exit 1; }
    echo "  ✓ sha256 校验通过"
  fi
fi

tar xzf "$tmp/$tarball" -C "$tmp"
root="$tmp/kvlang-abi-$tag-$plat"
[ -d "$root" ] || { echo "kvlang: 包结构异常" >&2; exit 1; }

$SUDO mkdir -p "$PREFIX/lib" "$PREFIX/lib/$KVSPACE_BACKEND_DIR" "$PREFIX/bin" "$PREFIX/include"

if [ -d "$root/lib" ]; then
  for f in "$root/lib"/*.so* "$root/lib"/*.dylib; do
    [ -e "$f" ] && $SUDO cp -P "$f" "$PREFIX/lib/"     # -P 保符号链接原样
  done
  if [ -d "$root/lib/$KVSPACE_BACKEND_DIR" ]; then
    for f in "$root/lib/$KVSPACE_BACKEND_DIR"/*.so* "$root/lib/$KVSPACE_BACKEND_DIR"/*.dylib; do
      [ -e "$f" ] && $SUDO cp -P "$f" "$PREFIX/lib/$KVSPACE_BACKEND_DIR/"
    done
  fi
fi

if [ -d "$root/bin" ]; then
  for f in "$root/bin"/*; do
    [ -e "$f" ] && { $SUDO cp "$f" "$PREFIX/bin/"; $SUDO chmod 755 "$PREFIX/bin/$(basename "$f")"; }
  done
fi

if [ -d "$root/include" ]; then
  for d in "$root/include"/*; do
    [ -e "$d" ] && $SUDO cp -R "$d" "$PREFIX/include/"
  done
fi

echo
echo "✅ kvlang $tag 已安装到 $PREFIX"
echo "   CLI      : $PREFIX/bin/{byteseek,kvlanglayout,kvspace}"
echo "   库       : $PREFIX/lib/  +  $PREFIX/lib/$KVSPACE_BACKEND_DIR/"
echo "   头       : $PREFIX/include/{kvlang,kvspace,...}"
echo
echo "试用（后端由 KVSPACE DSN 选择）："
echo "  KVSPACE=shm:///tmp/kvlang.shm $PREFIX/bin/byteseek -c 'println(\"hello\")'"
case "$plat" in
  darwin-*)
    echo
    echo "注意：macOS 无 ldconfig，若 CLI 或扩展找不到库，请设："
    echo "  DYLD_FALLBACK_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib/$KVSPACE_BACKEND_DIR"
    ;;
esac
