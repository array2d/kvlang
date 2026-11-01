#!/usr/bin/env bash
# Run `runtime/tests/<name>.kv` on the C runtime:
#   `<name>.want`: expected stdout (exact match);
#   `<name>.wanterr` (optional): require non-zero exit with each line matched in stderr.
set -uo pipefail
name="${1:?usage: c_kv_case.sh <case-name>}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
T="$ROOT/runtime/tests"
if [ -z "${KVLANG_TERM_BIN:-}" ] || [ -z "${KVLANG_LAYOUT_BIN:-}" ]; then
	exec "$T/with_c_runtime.sh" bash "$0" "$@"
fi
KVLANG="$KVLANG_TERM_BIN"
LAYOUT="$KVLANG_LAYOUT_BIN"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
dsn="shm://$tmp/s.shm"
if ! KVSPACE="$dsn" "$KVLANG" -c 'print("boot")' >/dev/null 2>&1; then
	echo "FAIL: boot"
	exit 1
fi
if ! "$LAYOUT" "$T/$name.kv" "$dsn" >"$tmp/layout.log" 2>&1; then
	echo "FAIL: layout $name"
	cat "$tmp/layout.log"
	exit 1
fi
KVSPACE="$dsn" timeout 60 "$KVLANG" test >"$tmp/out" 2>"$tmp/err"
rc=$?
fail=0
if ! diff -u "$T/$name.want" "$tmp/out"; then
	echo "FAIL: $name stdout"
	fail=1
fi
if [ -f "$T/$name.wanterr" ]; then
	if [ "$rc" -eq 0 ]; then
		echo "FAIL: $name expected non-zero exit"
		fail=1
	fi
	while IFS= read -r want; do
		[ -z "$want" ] && continue
		if ! grep -qF -- "$want" "$tmp/err"; then
			echo "FAIL: $name stderr lacks: $want"
			fail=1
		fi
	done <"$T/$name.wanterr"
elif [ "$rc" -ne 0 ]; then
	echo "FAIL: $name exit $rc"
	fail=1
fi
if [ "$fail" -ne 0 ]; then
	echo "--- stderr"
	cat "$tmp/err"
	exit 1
fi
echo "PASS: $name (C runtime)"
