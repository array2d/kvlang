#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

PATHLANG_BIN="${PATHLANG_BIN:-$ROOT/bin/pathseek}"
CASE_TIMEOUT="${KV_CASE_TIMEOUT:-180}"

FILE="benchmark/cases/iops/iops.kv"
SCALE="200"
REQUIRE=0
KEEP=0

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'; NC=$'\033[0m'

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

usage() {
	cat <<'EOF'
usage: dump-binir-check.sh [--file FILE.kv] [--scale N] [--require] [--keep]

  --file FILE   .kv file to dump (default: benchmark/cases/iops/iops.kv)
  --scale N     Replacement value for __SCALE__ in source (default: 200)
  --require     Fail when RC2_DUMP_BINIR produces no output (default: SKIP with exit 0)
  --keep        Keep temporary directory and print its path
  -h, --help    Show this help

Exit codes: 0 passed/skipped; 1 structural mismatch; 2 environment/usage error.
EOF
}

while (($#)); do
	case "$1" in
	--file) FILE="${2:-}"; shift ;;
	--scale) SCALE="${2:-}"; shift ;;
	--require) REQUIRE=1 ;;
	--keep) KEEP=1 ;;
	-h | --help) usage; exit 0 ;;
	*) die "unknown argument: $1" ;;
	esac
	shift
done

[ -x "$PATHLANG_BIN" ] || die "PATHLANG_BIN is not executable: $PATHLANG_BIN"
[ -f "$FILE" ] || die "file does not exist: $FILE"
[[ "$SCALE" =~ ^[0-9]+$ ]] || die "--scale must be a non-negative integer: $SCALE"
command -v timeout >/dev/null 2>&1 || die "coreutils timeout is required"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/pathlang-binir.XXXXXX")" || die "mktemp failed"
cleanup() { [ "$KEEP" -eq 1 ] || rm -rf "$TMP"; }
trap cleanup EXIT

SRC="$TMP/case.kv"
sed "s/__SCALE__/$SCALE/g" "$FILE" >"$SRC"
DSN="shm://$TMP/store.shm"
STDOUT="$TMP/stdout.txt"
STDERR="$TMP/stderr.txt"

env PATHSPACE="$DSN" RC2_DUMP_BINIR=1 timeout -k 5 "$CASE_TIMEOUT" \
	"$PATHLANG_BIN" "$SRC" >"$STDOUT" 2>"$STDERR"
RC=$?

if [ "$KEEP" -eq 1 ]; then
	printf 'tmp: %s\n' "$TMP"
fi
printf 'file=%s scale=%s exit=%s\n' "$FILE" "$SCALE" "$RC"
[ "$RC" -eq 0 ] || printf '%s[warn]%s non-zero program exit code; continuing structural check\n' "$YELLOW" "$NC"

DUMP="$TMP/dump.txt"
if grep -q '^\[binir\]' "$STDERR"; then
	cp "$STDERR" "$DUMP"
elif grep -q '^\[binir\]' "$STDOUT"; then
	printf '%s[warn]%s [binir] written to stdout (expected stderr)\n' "$YELLOW" "$NC"
	cp "$STDOUT" "$DUMP"
else
	if [ "$REQUIRE" -eq 1 ]; then
		printf '%s[FAIL]%s RC2_DUMP_BINIR=1 produced no [binir] output (--require)\n' "$RED" "$NC"
		exit 1
	fi
	printf '%s[SKIP]%s current build does not emit binir dump (no RC2_DUMP_BINIR output)\n' \
		"$YELLOW" "$NC"
	exit 0
fi

printf '\n-- sample --\n'
grep -m1 '^\[binir\]' "$DUMP"
grep -E '^[[:space:]]+[0-9]+[[:space:]]+' "$DUMP" | head -n 6
printf -- '-- end sample --\n\n'

awk '
function fail(msg) { printf "  FAIL: %s\n", msg; seg_fail = 1; global_fail = 1 }
function finish() {
	if (seg == 0) return
	if (insn == 0) fail("segment " seg " has no instruction lines")
	if (!saw_exit) fail("segment " seg " missing exit: JIT x / INTERP y")
	if (saw_exit && jit + interp != ninsn)
		fail("segment " seg " JIT(" jit ") + INTERP(" interp ") != ninsn(" ninsn ")")
	if (nconst > 0 && constn == 0) fail("segment " seg " nconst=" nconst " but no const[] lines")
	if (nreg > 0 && regn == 0) fail("segment " seg " nreg=" nreg " but no reg[] lines")
	if (!seg_fail)
		printf "  OK: seg %d func=%s insn=%d const=%d reg=%d jit=%d interp=%d\n", \
			seg, func, insn, constn, regn, jit, interp
}
/^\[binir\] / {
	finish()
	seg++; seg_fail = 0
	insn = constn = regn = jit = interp = 0
	saw_exit = 0
	func = "-"; ninsn = -1; nreg = -1; nconst = -1
	for (i = 1; i <= NF; i++) {
		if ($i ~ /^func=/) { func = $i; sub(/^func=/, "", func) }
		else if ($i ~ /^ninsn=/) { ninsn = $i; sub(/^ninsn=/, "", ninsn) }
		else if ($i ~ /^nreg=/) { nreg = $i; sub(/^nreg=/, "", nreg) }
		else if ($i ~ /^nconst=/) { nconst = $i; sub(/^nconst=/, "", nconst) }
	}
	if (ninsn < 0 || nreg < 0 || nconst < 0) fail("segment " seg " header missing ninsn/nreg/nconst fields")
	have_header = 1
	next
}
/^[[:space:]]*[0-9]+[[:space:]]+/ {
	if ($0 ~ /\[JIT\]/ || $0 ~ /\[INTERP\]/) { insn++; insn_total++; have_insn = 1 }
	next
}
/^[[:space:]]*const\[[0-9]+\]/ { constn++; have_const = 1; next }
/^[[:space:]]*reg\[[0-9]+\]/ { regn++; have_reg = 1; next }
/^[[:space:]]*exit: JIT [0-9]+ \/ INTERP [0-9]+/ {
	t = $0
	sub(/^[[:space:]]*exit:[[:space:]]*JIT[[:space:]]*/, "", t); sub(/[^0-9].*/, "", t); jit = t + 0
	t = $0
	sub(/.*INTERP[[:space:]]*/, "", t); sub(/[^0-9].*/, "", t); interp = t + 0
	saw_exit = 1
	have_exit = 1
	next
}
END {
	finish()
	if (seg == 0) { printf "  FAIL: no [binir] segment header\n"; global_fail = 1 }
	if (!have_insn) { printf "  FAIL: no instruction lines with [JIT]/[INTERP]\n"; global_fail = 1 }
	if (!have_const) { printf "  FAIL: no const[] lines\n"; global_fail = 1 }
	if (!have_reg) { printf "  FAIL: no reg[] lines\n"; global_fail = 1 }
	if (!have_exit) { printf "  FAIL: no exit: JIT x / INTERP y lines\n"; global_fail = 1 }
	printf "segments=%d instructions=%d\n", seg, insn_total
	exit (global_fail ? 1 : 0)
}
' "$DUMP"
VERDICT=$?

if [ "$VERDICT" -eq 0 ]; then
	printf '%s[PASS]%s binir dump structure valid\n' "$GREEN" "$NC"
	exit 0
fi
printf '%s[FAIL]%s binir dump structure incomplete/inconsistent\n' "$RED" "$NC"
exit 1
