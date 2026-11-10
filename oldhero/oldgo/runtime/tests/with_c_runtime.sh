#!/usr/bin/env bash
# Stage a temporary prefix swapping libkvlang_runtime.so into libkvlang_runtimec2.so
# (byteseek links libkvlang_runtimec2.so with DT_RPATH=$ORIGIN/../lib), export test
# environment variables, execute "$@", and clean up the temporary prefix on exit.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RT_SO="${KVLANG_C_RUNTIME_SO:-$ROOT/lib/libkvlang_runtime.so}"
for f in "$ROOT/bin/byteseek" "$ROOT/bin/kvlanglayout" \
	"$RT_SO" "$ROOT/lib/libkvlanglayout.so"; do
	if [ ! -e "$f" ]; then
		echo "SKIP: missing $f" >&2
		exit 77
	fi
done
tmp=$(mktemp -d)
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT
P="$tmp/pfx"
mkdir -p "$P/bin" "$P/lib"
cp "$ROOT/bin/byteseek" "$ROOT/bin/kvlanglayout" "$P/bin/"
cp -a "$ROOT"/lib/libkvspace.so* "$ROOT/lib/libkvlanglayout.so" "$P/lib/"
[ -d "$ROOT/lib/kvspace" ] && cp -a "$ROOT/lib/kvspace" "$P/lib/"
ln -sf "$RT_SO" "$P/lib/libkvlang_runtimec2.so"

export KVLANG_TERM_BIN="$P/bin/byteseek"
export KVLANG_LAYOUT_BIN="$P/bin/kvlanglayout"
if [ -x "$ROOT/bin/kvspace" ]; then
	export KVSPACE_CLI="$ROOT/bin/kvspace"
fi
export KVLANG_PC_PROBE="$ROOT/bin/test_pc_view_probe"
export LD_LIBRARY_PATH="$P/lib:$P/lib/kvspace:$ROOT/lib:$ROOT/lib/kvspace${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$@"
rc=$?
exit "$rc"
