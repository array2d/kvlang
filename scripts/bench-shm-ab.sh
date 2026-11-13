#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

A_BIN="${PATHLANG_BIN:-$ROOT/bin/pathseek}"
A_LIB="$ROOT/lib"
A_ENV=""
B_BIN=""
B_LIB="$ROOT/lib"
B_ENV=""
REPEAT=3
SCALE_MODE="largest"
FILTER=""
CSV=""
VERBOSE=0
CASE_TIMEOUT="${KV_CASE_TIMEOUT:-300}"
HARNESS_REV=2

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'; NC=$'\033[0m'

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

usage() {
	cat <<'EOF'
usage: bench-shm-ab.sh [options]

  --a-bin PATH     Arm A executable (default: $PATHLANG_BIN or <repo>/bin/pathseek)
  --a-lib DIR      Arm A library directory (default: <repo>/lib)
  --a-env 'K=V ..' Extra env vars for arm A
  --b-bin PATH     Arm B executable (enables arm B when set or with --b-env)
  --b-lib DIR      Arm B library directory (default: <repo>/lib)
  --b-env 'K=V ..' Extra env vars for arm B (e.g. RC2_NOJIT=1)
  --repeat N       Samples per point, taking min (default: 3)
  --scale MODE     largest (default) | all | positive integer
  -k, --filter P   Only run cases matching substring P
  --csv FILE       Also write raw results to CSV
  -v, --verbose    Print per-run timing for each arm
  -h, --help       Show this help

Ratios use C=1 as baseline: A kv/C, B kv/C; B/A is arm B over arm A (<1 means B is faster).
Exit codes: 0 success; 1 failure or output mismatch; 2 environment or usage error.
EOF
}

while (($#)); do
	case "$1" in
	--a-bin) A_BIN="${2:-}"; shift ;;
	--a-lib) A_LIB="${2:-}"; shift ;;
	--a-env) A_ENV="${2:-}"; shift ;;
	--b-bin) B_BIN="${2:-}"; shift ;;
	--b-lib) B_LIB="${2:-}"; shift ;;
	--b-env) B_ENV="${2:-}"; shift ;;
	--repeat) REPEAT="${2:-}"; shift ;;
	--scale) SCALE_MODE="${2:-}"; shift ;;
	-k | --filter) FILTER="${2:-}"; shift ;;
	--csv) CSV="${2:-}"; shift ;;
	-v | --verbose) VERBOSE=1 ;;
	-h | --help) usage; exit 0 ;;
	*) die "unknown argument: $1" ;;
	esac
	shift
done

[[ "$REPEAT" =~ ^[1-9][0-9]*$ ]] || die "--repeat must be a positive integer: $REPEAT"
[ -x "$A_BIN" ] || die "arm A binary is not executable: $A_BIN"
[ -d "$A_LIB" ] || die "arm A library directory does not exist: $A_LIB"
command -v gcc >/dev/null 2>&1 || die "gcc is required to build C baselines"
command -v timeout >/dev/null 2>&1 || die "coreutils timeout is required"
command -v python3 >/dev/null 2>&1 || die "python3 is required (reads SWEEP from benchmark/run.py)"

B_ON=0
if [ -n "$B_BIN" ] || [ -n "$B_ENV" ]; then
	B_ON=1
	[ -n "$B_BIN" ] || B_BIN="$A_BIN"
	[ -x "$B_BIN" ] || die "arm B binary is not executable: $B_BIN"
	[ -d "$B_LIB" ] || die "arm B library directory does not exist: $B_LIB"
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/pathlang-bench-ab.XXXXXX")" || die "mktemp failed"
trap 'rm -rf "$TMP"' EXIT

stage_arm() { # $1=dir $2=bin $3=lib
	mkdir -p "$1/bin" "$1/lib"
	cp "$2" "$1/bin/pathseek"
	cp -a "$3/." "$1/lib/"
}

A_ROOT="$TMP/armA"
stage_arm "$A_ROOT" "$A_BIN" "$A_LIB"

B_ROOT=""
if [ "$B_ON" -eq 1 ]; then
	B_ROOT="$TMP/armB"
	stage_arm "$B_ROOT" "$B_BIN" "$B_LIB"
fi

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

run_once() {
	local arm="$1" envs="$2" out="$3" src="$4"
	local d="$TMP/run"
	rm -rf "$d"; mkdir -p "$d"
	local dsn="shm://$d/store.shm"
	local -a e=()
	[ -n "$envs" ] && read -r -a e <<<"$envs"
	env PATHSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$arm/bin/pathseek" "$src" >"$out" 2>"$d/err"
	return $?
}

bench_ns() {
	awk '/^__bench_ns:/ { print $2; exit }' "$1"
}

normalize() {
	sed -e '/^__bench_ns:/d' -e '/^__bench_us:/d' -e '/^__bench_input:/d'
}

fmt_ns() {
	local ns="$1"
	[ -z "$ns" ] && { printf '%s' "-"; return; }
	if [ "$ns" -ge 1000000000 ]; then printf '%.3fs' "$(awk "BEGIN{print $ns/1e9}")"
	elif [ "$ns" -ge 1000000 ]; then printf '%.3fms' "$(awk "BEGIN{print $ns/1e6}")"
	elif [ "$ns" -ge 1000 ]; then printf '%.3fus' "$(awk "BEGIN{print $ns/1e3}")"
	else printf '%sns' "$ns"; fi
}

ratio() { # $1=ns $2=base ns → "x.xx×"
	local a="$1" b="$2"
	if [ -z "$a" ] || [ -z "$b" ] || [ "$b" -eq 0 ]; then printf '%s' "-"; return; fi
	awk -v a="$a" -v b="$b" 'BEGIN{ printf "%.2fx", a / b }'
}

ARM_BEST=""
run_arm_best() { # $1=arm root $2=env $3=src $4=outfile
	local arm="$1" envs="$2" src="$3" out="$4"
	local best="" i ns rc=0
	for ((i = 1; i <= REPEAT; i++)); do
		run_once "$arm" "$envs" "$out" "$src" || rc=$?
		ns="$(bench_ns "$out")"
		[ "$VERBOSE" -eq 1 ] && printf '    %-28s run%d rc=%s ns=%s\n' "$(basename "$arm")" "$i" "$rc" "${ns:--}"
		[ "$rc" -ne 0 ] && break
		if [ -n "$ns" ] && { [ -z "$best" ] || [ "$ns" -lt "$best" ]; }; then best="$ns"; fi
	done
	ARM_BEST="$best"
	return "$rc"
}

version="$(git -C "$ROOT" describe --tags --always --dirty 2>/dev/null || echo unknown)"
commit="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
cpu="$(awk -F: '/model name/ { gsub(/^ +/, "", $2); print $2; exit }' /proc/cpuinfo 2>/dev/null || echo unknown)"

printf '# bench-shm-ab  backend=shm  repeat=%s  scale=%s\n' "$REPEAT" "$SCALE_MODE"
printf '# version=%s commit=%s cpu=[%s] harness_rev=%s\n' \
	"$version" "$commit" "${cpu:-unknown}" "$HARNESS_REV"
printf '# A: bin=%s lib=%s env=%s\n' "$A_BIN" "$A_LIB" "${A_ENV:-(none)}"
if [ "$B_ON" -eq 1 ]; then
	printf '# B: bin=%s lib=%s env=%s\n' "$B_BIN" "$B_LIB" "${B_ENV:-(none)}"
else
	printf '# B: (disabled, A vs C only)\n'
fi
printf '# Ratios relative to C=1: A kv/C, B kv/C, B/A (<1 = B faster)\n'

if [ -n "$CSV" ]; then
	printf 'case,input,A_ns,B_ns,C_ns,A_over_C,B_over_C,B_over_A,valid,harness_rev\n' >"$CSV"
fi

printf '%-14s %-10s %11s %11s %11s %9s %9s %8s %5s\n' \
	case input A B C 'A kv/C' 'B kv/C' 'B/A' valid

TOTAL=0 BAD=0
for cdir in ./benchmark/cases/*/; do
	name="$(basename "$cdir")"
	src_case="$cdir/$name.kv"
	[ -f "$src_case" ] || continue
	[ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]] && continue

	native="$TMP/native-$name"
	if ! gcc -O1 -o "$native" "$cdir/$name.c" 2>"$TMP/gcc-$name.err"; then
		printf '%s[FAIL]%s %s: C baseline build failed\n' "$RED" "$NC" "$name"
		BAD=$((BAD + 1))
		continue
	fi

	for scale in $(scales_for "$name"); do
		TOTAL=$((TOTAL + 1))
		src="$TMP/src-$name-$scale.kv"
		sed "s/__SCALE__/$scale/g" "$src_case" >"$src"
		inp="N=$scale"

		outA="$TMP/out-A-$name-$scale"
		outB="$TMP/out-B-$name-$scale"
		outC="$TMP/out-C-$name-$scale"

		a_rc=0
		run_arm_best "$A_ROOT" "$A_ENV" "$src" "$outA" || a_rc=$?
		a_ns="$ARM_BEST"
		b_rc=0 b_ns=""
		[ "$B_ON" -eq 1 ] && { run_arm_best "$B_ROOT" "$B_ENV" "$src" "$outB" || b_rc=$?; b_ns="$ARM_BEST"; }

		c_best=""
		c_rc=0
		for ((i = 1; i <= REPEAT; i++)); do
			env BENCH_SCALE="$scale" timeout -k 5 "$CASE_TIMEOUT" "$native" >"$outC" 2>"$TMP/c.err"
			c_rc=$?
			[ "$c_rc" -ne 0 ] && break
			ns="$(bench_ns "$outC")"
			if [ -n "$ns" ] && { [ -z "$c_best" ] || [ "$ns" -lt "$c_best" ]; }; then c_best="$ns"; fi
		done
		if [ "$c_rc" -ne 0 ]; then
			printf '%s[FAIL]%s %s %s: C baseline run failed rc=%s\n' "$RED" "$NC" "$name" "$inp" "$c_rc"
			BAD=$((BAD + 1))
			continue
		fi

		valid="yes"
		nA="$TMP/nA"; nB="$TMP/nB"; nC="$TMP/nC"
		normalize <"$outA" >"$nA"
		normalize <"$outC" >"$nC"
		! cmp -s "$nA" "$nC" && valid="no"
		if [ "$B_ON" -eq 1 ]; then
			normalize <"$outB" >"$nB"
			! cmp -s "$nB" "$nC" && valid="no"
		fi

		if [ "$a_rc" -ne 0 ] || [ -z "$a_ns" ] || { [ "$B_ON" -eq 1 ] && { [ "$b_rc" -ne 0 ] || [ -z "$b_ns" ]; }; }; then
			valid="no"
			BAD=$((BAD + 1))
		fi

		a_c="$(ratio "$a_ns" "$c_best")"
		b_c="-"; b_a="-"
		if [ "$B_ON" -eq 1 ]; then
			b_c="$(ratio "$b_ns" "$c_best")"
			b_a="$(ratio "$b_ns" "$a_ns")"
		fi

		printf '%-14s %-10s %11s %11s %11s %9s %9s %8s %5s\n' \
			"$name" "$inp" "$(fmt_ns "$a_ns")" "$(fmt_ns "$b_ns")" \
			"$(fmt_ns "$c_best")" "$a_c" "$b_c" "$b_a" "$valid"

		if [ "$VERBOSE" -eq 1 ] && [ "$valid" = "no" ]; then
			diff -u "$nC" "$nA" | sed -n '1,10p' | sed 's/^/    /'
		fi

		if [ -n "$CSV" ]; then
			printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
				"$name" "$inp" "${a_ns:-}" "${b_ns:-}" "${c_best:-}" \
				"${a_c%-x}" "${b_c%-x}" "${b_a%-x}" "$valid" \
				"$HARNESS_REV" >>"$CSV"
		fi
	done
done

printf '\nsummary: cases=%d bad=%d\n' "$TOTAL" "$BAD"
[ -n "$CSV" ] && printf 'csv: %s\n' "$CSV"

[ "$BAD" -gt 0 ] && exit 1
exit 0
