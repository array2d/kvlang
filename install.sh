#!/bin/sh
set -eu

REPO=pathlang/pathlang
PATHSPACE_BACKEND_DIR=pathspace   # Backends live alongside dispatch frontend in <prefix>/lib/pathspace

os=$(uname -s)
arch=$(uname -m)
case "$os-$arch" in
  Linux-x86_64) plat=linux-x86_64 ;;
  Darwin-arm64) plat=darwin-arm64 ;;
  *)
    echo "pathlang: unsupported platform $os-$arch (published: linux-x86_64, darwin-arm64)" >&2
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
  [ -n "$tag" ] || { echo "pathlang: failed to fetch latest release version" >&2; exit 1; }
fi

base="${BASE_URL:-https://github.com/$REPO/releases/download/$tag}"
tarball="pathlang-abi-$tag-$plat.tar.gz"
case "$plat" in
  linux-*)  sums=SHA256SUMS-linux ;;
  darwin-*) sums=SHA256SUMS-macos ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

echo "pathlang $tag ($plat) -> $PREFIX"
curl -fsSL "$base/$tarball" -o "$tmp/$tarball"

if curl -fsSL "$base/$sums" -o "$tmp/$sums" 2>/dev/null; then
  want=$(sed -n "s/^\\([0-9a-f]\\{64\\}\\)[[:space:]]*\\**$tarball$/\\1/p" "$tmp/$sums" | head -1)
  if [ -n "$want" ]; then
    if command -v sha256sum >/dev/null 2>&1; then
      got=$(sha256sum "$tmp/$tarball" | cut -d' ' -f1)
    else
      got=$(shasum -a 256 "$tmp/$tarball" | cut -d' ' -f1)
    fi
    [ "$want" = "$got" ] || { echo "pathlang: checksum verification failed (sha256 mismatch)" >&2; exit 1; }
    echo "  [ok] sha256 verified"
  fi
fi

tar xzf "$tmp/$tarball" -C "$tmp"
root="$tmp/pathlang-abi-$tag-$plat"
[ -d "$root" ] || { echo "pathlang: unexpected archive layout" >&2; exit 1; }

$SUDO mkdir -p "$PREFIX/lib" "$PREFIX/lib/$PATHSPACE_BACKEND_DIR" "$PREFIX/bin" "$PREFIX/include"

if [ -d "$root/lib" ]; then
  for f in "$root/lib"/*.so* "$root/lib"/*.dylib; do
    [ -e "$f" ] && $SUDO cp -P "$f" "$PREFIX/lib/"     # -P preserves symlinks
  done
  if [ -d "$root/lib/$PATHSPACE_BACKEND_DIR" ]; then
    for f in "$root/lib/$PATHSPACE_BACKEND_DIR"/*.so* "$root/lib/$PATHSPACE_BACKEND_DIR"/*.dylib; do
      [ -e "$f" ] && $SUDO cp -P "$f" "$PREFIX/lib/$PATHSPACE_BACKEND_DIR/"
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
echo "Installed pathlang $tag to $PREFIX"
echo "   CLI      : $PREFIX/bin/{pathseek,pathlanglayout,pathspace}"
echo "   Libs     : $PREFIX/lib/  +  $PREFIX/lib/$PATHSPACE_BACKEND_DIR/"
echo "   Headers  : $PREFIX/include/{pathlang,pathspace,...}"
echo
echo "Quick start (backend selected via PATHSPACE DSN):"
echo "  PATHSPACE=shm:///tmp/pathlang.shm $PREFIX/bin/pathseek -c 'println(\"hello\")'"
case "$plat" in
  darwin-*)
    echo
    echo "Note: macOS has no ldconfig; if libraries are not found, set:"
    echo "  DYLD_FALLBACK_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib/$PATHSPACE_BACKEND_DIR"
    ;;
esac
