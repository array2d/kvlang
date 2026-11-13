#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

PATHLANG_BIN="${PATHLANG_BIN:-$ROOT/bin/pathseek}"
LAYOUT_BIN="${PATHLANG_LAYOUT_BIN:-$ROOT/bin/pathlanglayout}"
CASE_TIMEOUT="${KV_CASE_TIMEOUT:-180}"

MODE="all"
FILTER=""
SCALE_MODE="largest"
KEEP_TIMING=0
VERBOSE=0
STRICT=0
MAX_DIFF=12
FILES=()

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'; NC=$'\033[0m'

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

usage() {
	cat <<'EOF'
usage: verify-jit-parity.sh [--tutorial|--bench|--all] [options]

  --tutorial        Run tutorial suite only (non-extern/wip files with Expected output)
  --bench           Run the 10 benchmark cases only
  --all             Run both suites (default)
  --filter PAT      Only run cases whose path/name contains PAT
  --file FILE.kv    Run specified file(s) only (may combine with --scale N for __SCALE__)
  --scale MODE      Benchmark scale: largest (default) | all | positive integer
  --keep-timing     Keep __bench_ns/__bench_us timing lines (stripped by default)
  --strict          Treat NONDET (non-deterministic output) as failure
  -v, --verbose     Print status line for every case
  -h, --help        Show this help

Exit codes: 0 passed; 1 parity FAIL; 2 environment/usage error.
EOF
}

while (($#)); do
	case "$1" in
	--tutorial) MODE="tutorial" ;;
	--bench) MODE="bench" ;;
	--all) MODE="all" ;;
	--filter) FILTER="${2:-}"; shift ;;
	--scale) SCALE_MODE="${2:-}"; shift ;;
	--file) FILES+=("${2:-}"); shift ;;
	--keep-timing) KEEP_TIMING=1 ;;
	--strict) STRICT=1 ;;
	-v | --verbose) VERBOSE=1 ;;
	-h | --help) usage; exit 0 ;;
	*) die "unknown argument: $1" ;;
	esac
	shift
done

[ -x "$PATHLANG_BIN" ] || die "PATHLANG_BIN is not executable: $PATHLANG_BIN"
[ -x "$LAYOUT_BIN" ] || die "PATHLANG_LAYOUT_BIN is not executable: $LAYOUT_BIN"
command -v timeout >/dev/null 2>&1 || die "coreutils timeout is required"
command -v python3 >/dev/null 2>&1 || die "python3 is required (reads SWEEP from benchmark/run.py)"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/pathlang-parity.XXXXXX")" || die "mktemp failed"
trap 'rm -rf "$TMP"' EXIT

normalize() {
	if [ "$KEEP_TIMING" -eq 1 ]; then
		cat
	else
		sed -e '/^__bench_ns:/d' -e '/^__bench_us:/d'
	fi
}

needs_skip() {
	awk '
		/^\/\/ extern/ || /^\/\/ wip/ { print "1"; exit }
		$0 != "" && $0 !~ /^\/\// { exit }
	' "$1"
}

has_expects() {
	grep -Eq '^// [Ee]xpected output' "$1"
}

run_tutorial() {
	local rel="$1" out="$2" nojit="${3:-0}"
	local d="$TMP/tut"
	rm -rf "$d"; mkdir -p "$d"
	local dsn="shm://$d/store.shm"
	local -a e=()
	[ "$nojit" -eq 1 ] && e=(RC2_NOJIT=1)
	local rc

	env PATHSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$PATHLANG_BIN" -c 'print("boot")' >/dev/null 2>"$d/boot.err"
	rc=$?
	[ "$rc" -ne 0 ] && {
		printf '<boot failed rc=%s> %s\n' "$rc" "$(tail -n 1 "$d/boot.err")" >"$out"
		return 90
	}

	env PATHSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$LAYOUT_BIN" "$rel" "$dsn" >/dev/null 2>"$d/layout.err"
	rc=$?
	[ "$rc" -ne 0 ] && {
		printf '<layout failed rc=%s> %s\n' "$rc" "$(tail -n 1 "$d/layout.err")" >"$out"
		return 91
	}

	env PATHSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$PATHLANG_BIN" test >"$out" 2>"$d/run.err"
	return $?
}

run_bench() {
	local case="$1" scale="$2" out="$3" nojit="${4:-0}"
	local d="$TMP/bench"
	rm -rf "$d"; mkdir -p "$d"
	local src="$d/$case.kv"
	sed "s/__SCALE__/$scale/g" "benchmark/cases/$case/$case.kv" >"$src"
	local dsn="shm://$d/store.shm"
	local -a e=()
	[ "$nojit" -eq 1 ] && e=(RC2_NOJIT=1)
	env PATHSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$PATHLANG_BIN" "$src" >"$out" 2>"$d/run.err"
	return $?
}

run_file() {
	local path="$1" out="$2" nojit="${3:-0}"
	local d="$TMP/file"
	rm -rf "$d"; mkdir -p "$d"
	local src="$d/case.kv" sc="$SCALE_MODE"
	[[ "$sc" =~ ^[0-9]+$ ]] || sc=1
	sed "s/__SCALE__/$sc/g" "$path" >"$src"
	local dsn="shm://$d/store.shm"
	local -a e=()
	[ "$nojit" -eq 1 ] && e=(RC2_NOJIT=1)
	env PATHSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$PATHLANG_BIN" "$src" >"$out" 2>"$d/run.err"
	return $?
}

sweep_scales() {
	python3 - "$ROOT/benchmark/run.py" "$1" <<'PY'
import importlib.util
import sys

path, case = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location("_bench_run", path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
print(" ".join(str(x) for x in mod.SWEEP.get(case, [1])))
PY
}

scales_for() {
	local case="$1"
	case "$SCALE_MODE" in
	largest)
		local s
		s="$(sweep_scales "$case")"
		printf '%s\n' "${s##* }"
		;;
	all) sweep_scales "$case" ;;
	*)
		[[ "$SCALE_MODE" =~ ^[0-9]+$ ]] || die "--scale accepts largest|all|<positive integer>: $SCALE_MODE"
		printf '%s\n' "$SCALE_MODE"
		;;
	esac
}

PASS=0 FAIL=0 NONDET=0 SKIP=0 TOTAL=0
FAILED=()

report_fail() { # $1=name $2=rcA $3=rcB $4=normA $5=normB
	printf '%s[FAIL]%s %s\n' "$RED" "$NC" "$1"
	[ "$2" -ne "$3" ] && printf '  exit: A=%s B=%s\n' "$2" "$3"
	if ! cmp -s "$4" "$5"; then
		diff -u "$5" "$4" | sed -n "1,${MAX_DIFF}p" | sed 's/^/  /'
	fi
}

check_case() { # $1=name $2=kind(tutorial|bench) $3=rel $4=scale
	local name="$1" kind="$2" rel="$3" scale="${4:-}"
	local outA="$TMP/outA" outB="$TMP/outB" outA2="$TMP/outA2"
	local nA="$TMP/nA" nB="$TMP/nB" nA2="$TMP/nA2"
	local rcA rcB rcA2

	if [ "$kind" = "tutorial" ]; then
		run_tutorial "$rel" "$outA" 0; rcA=$?
		run_tutorial "$rel" "$outB" 1; rcB=$?
	elif [ "$kind" = "file" ]; then
		run_file "$rel" "$outA" 0; rcA=$?
		run_file "$rel" "$outB" 1; rcB=$?
	else
		run_bench "$name" "$scale" "$outA" 0; rcA=$?
		run_bench "$name" "$scale" "$outB" 1; rcB=$?
	fi
	normalize <"$outA" >"$nA"
	normalize <"$outB" >"$nB"

	if [ "$rcA" -ge 90 ] || [ "$rcB" -ge 90 ]; then
		FAIL=$((FAIL + 1))
		FAILED+=("$name")
		printf '%s[FAIL]%s %s (boot/layout failed: A=%s B=%s)\n' \
			"$RED" "$NC" "$name" "$rcA" "$rcB"
		[ "$rcA" -ge 90 ] && sed -n '1,3p' "$outA" | sed 's/^/  A: /'
		[ "$rcB" -ge 90 ] && sed -n '1,3p' "$outB" | sed 's/^/  B: /'
		return
	fi

	if [ "$rcA" -eq "$rcB" ] && cmp -s "$nA" "$nB"; then
		PASS=$((PASS + 1))
		[ "$VERBOSE" -eq 1 ] && printf '%s[PASS]%s %s\n' "$GREEN" "$NC" "$name"
		return
	fi

	if [ "$kind" = "tutorial" ]; then
		run_tutorial "$rel" "$outA2" 0; rcA2=$?
	elif [ "$kind" = "file" ]; then
		run_file "$rel" "$outA2" 0; rcA2=$?
	else
		run_bench "$name" "$scale" "$outA2" 0; rcA2=$?
	fi
	normalize <"$outA2" >"$nA2"
	if [ "$rcA" -ne "$rcA2" ] || ! cmp -s "$nA" "$nA2"; then
		NONDET=$((NONDET + 1))
		printf '%s[NONDET]%s %s (two JIT runs differed; non-deterministic)\n' "$YELLOW" "$NC" "$name"
		return
	fi

	FAIL=$((FAIL + 1))
	FAILED+=("$name")
	report_fail "$name" "$rcA" "$rcB" "$nA" "$nB"
}

run_tutorial_suite() {
	local f rel
	while IFS= read -r f; do
		rel="${f#./}"
		[ -n "$FILTER" ] && [[ "$rel" != *"$FILTER"* ]] && continue
		if [ -n "$(needs_skip "$f")" ]; then
			SKIP=$((SKIP + 1))
			continue
		fi
		has_expects "$f" || continue
		TOTAL=$((TOTAL + 1))
		check_case "$rel" tutorial "$rel" ""
	done < <(find ./tutorial -type f -name '*.kv' | sort)
}

run_bench_suite() {
	local cdir name scale
	for cdir in ./benchmark/cases/*/; do
		name="$(basename "$cdir")"
		[ -f "$cdir/$name.kv" ] || continue
		[ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue
		for scale in $(scales_for "$name"); do
			TOTAL=$((TOTAL + 1))
			check_case "$name" bench "$cdir/$name.kv" "$scale"
		done
	done
}

printf 'mode=%s filter=%s scale=%s backend=shm\n' "$MODE" "${FILTER:--}" "$SCALE_MODE"
printf 'A: JIT on           %s\n' "$PATHLANG_BIN"
printf 'B: RC2_NOJIT=1      same binary\n'

if [ "${#FILES[@]}" -gt 0 ]; then
	for f in "${FILES[@]}"; do
		TOTAL=$((TOTAL + 1))
		check_case "$f" file "$f" ""
	done
else
	[ "$MODE" = "tutorial" ] || [ "$MODE" = "all" ] && run_tutorial_suite
	[ "$MODE" = "bench" ] || [ "$MODE" = "all" ] && run_bench_suite
fi

printf '\nsummary: total=%d pass=%d fail=%d nondet=%d skip=%d\n' \
	"$TOTAL" "$PASS" "$FAIL" "$NONDET" "$SKIP"
if [ "$FAIL" -gt 0 ]; then
	printf 'failed cases:\n'
	printf '  %s\n' "${FAILED[@]}"
fi

if [ "$FAIL" -gt 0 ]; then exit 1; fi
if [ "$STRICT" -eq 1 ] && [ "$NONDET" -gt 0 ]; then exit 1; fi
exit 0
