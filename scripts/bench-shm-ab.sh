#!/usr/bin/env bash
# #583 验收：shm A/B 基准（benchmark 10 case × repeat），输出 kv/C 比值表（C = 1）。
#
# A/B 两臂各自有一套隔离的 bin/ + lib/（复制后 $ORIGIN rpath 只指向本臂），
# 避免 DT_RPATH=$ORIGIN 压过 LD_LIBRARY_PATH 导致两臂实际跑同一个库。
# 默认 A = 当前 <repo>/bin/byteseek（JIT 开），B 不启用；给 --b-bin 或 --b-env 才启用 B。
#
# 用法：
#   scripts/bench-shm-ab.sh                       # 单臂：A vs C 比值
#   scripts/bench-shm-ab.sh --b-env 'RC2_NOJIT=1' # JIT vs 解释 A/B
#   scripts/bench-shm-ab.sh --b-bin /path/byteseek --b-lib /path/lib
#   scripts/bench-shm-ab.sh --scale all --repeat 3
#   scripts/bench-shm-ab.sh -k iops --verbose
#   scripts/bench-shm-ab.sh --csv out.csv
#
# 环境变量：KVLANG_BIN（A 臂默认 <repo>/bin/byteseek）、KV_CASE_TIMEOUT（默认 300）
#
# 退出码：0 全部成功且功能输出一致；1 有 case 失败或功能输出不一致；2 环境/用法错误。
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

A_BIN="${KVLANG_BIN:-$ROOT/bin/byteseek}"
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

RED=$'\033[0;31m'; GREEN=$'\033[0;32m'; YELLOW=$'\033[1;33m'; NC=$'\033[0m'

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

usage() {
	cat <<'EOF'
usage: bench-shm-ab.sh [options]

  --a-bin PATH     A 臂可执行（默认 $KVLANG_BIN 或 <repo>/bin/byteseek）
  --a-lib DIR      A 臂库目录（默认 <repo>/lib）
  --a-env 'K=V ..' A 臂额外环境变量
  --b-bin PATH     B 臂可执行（给此项或 --b-env 才启用 B）
  --b-lib DIR      B 臂库目录（默认 <repo>/lib）
  --b-env 'K=V ..' B 臂额外环境变量（如 RC2_NOJIT=1）
  --repeat N       每点采样次数，取 min（默认 3）
  --scale MODE     largest（默认）| all | 具体数字
  -k, --filter P   只跑名字含 P 的 case
  --csv FILE       另存原始数据 CSV
  -v, --verbose    打印每臂每次运行结果
  -h, --help       显示本帮助

比值列均以 C 为 1：A kv/C、B kv/C；B/A 为两臂耗时之比（<1 表示 B 更快）。
退出码：0 成功；1 有失败/功能不一致；2 环境或用法错误。
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

[[ "$REPEAT" =~ ^[1-9][0-9]*$ ]] || die "--repeat 需为正整数：$REPEAT"
[ -x "$A_BIN" ] || die "A 臂不可执行：$A_BIN"
[ -d "$A_LIB" ] || die "A 臂库目录不存在：$A_LIB"
command -v gcc >/dev/null 2>&1 || die "需要 gcc 编译 C 基线"
command -v timeout >/dev/null 2>&1 || die "需要 coreutils timeout"
command -v python3 >/dev/null 2>&1 || die "需要 python3（读取 benchmark/run.py 的 SWEEP）"

B_ON=0
if [ -n "$B_BIN" ] || [ -n "$B_ENV" ]; then
	B_ON=1
	[ -n "$B_BIN" ] || B_BIN="$A_BIN"
	[ -x "$B_BIN" ] || die "B 臂不可执行：$B_BIN"
	[ -d "$B_LIB" ] || die "B 臂库目录不存在：$B_LIB"
fi

TMP="$(mktemp -d "${TMPDIR:-/tmp}/kvlang-bench-ab.XXXXXX")" || die "mktemp 失败"
trap 'rm -rf "$TMP"' EXIT

# 每臂一套隔离 bin/ + lib/：复制后 $ORIGIN/../lib 只解析到本臂副本。
stage_arm() { # $1=dir $2=bin $3=lib
	mkdir -p "$1/bin" "$1/lib"
	cp "$2" "$1/bin/byteseek"
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
		[[ "$SCALE_MODE" =~ ^[0-9]+$ ]] || die "--scale 只接受 largest|all|正整数：$SCALE_MODE"
		printf '%s\n' "$SCALE_MODE"
		;;
	esac
}

# 一次运行：$1=arm 根 $2=env 串 $3=stdout 文件 $4=源文件
run_once() {
	local arm="$1" envs="$2" out="$3" src="$4"
	local d="$TMP/run"
	rm -rf "$d"; mkdir -p "$d"
	local dsn="shm://$d/store.shm"
	local -a e=()
	[ -n "$envs" ] && read -r -a e <<<"$envs"
	env KVSPACE="$dsn" ${e[@]+"${e[@]}"} timeout -k 5 "$CASE_TIMEOUT" \
		"$arm/bin/byteseek" "$src" >"$out" 2>"$d/err"
	return $?
}

# 解析 __bench_ns 行。
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

# 跑一个臂 N 次取 min（写入全局 ARM_BEST），同时留下最后一次 stdout 供功能比对。
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
printf '# version=%s commit=%s cpu=[%s]\n' "$version" "$commit" "${cpu:-unknown}"
printf '# A: bin=%s lib=%s env=%s\n' "$A_BIN" "$A_LIB" "${A_ENV:-(none)}"
if [ "$B_ON" -eq 1 ]; then
	printf '# B: bin=%s lib=%s env=%s\n' "$B_BIN" "$B_LIB" "${B_ENV:-(none)}"
else
	printf '# B: (未启用，仅 A vs C)\n'
fi
printf '# 比值以 C=1：A kv/C、B kv/C、B/A（<1 = B 更快）\n'

if [ -n "$CSV" ]; then
	printf 'case,input,A_ns,B_ns,C_ns,A_over_C,B_over_C,B_over_A,valid\n' >"$CSV"
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
		printf '%s[FAIL]%s %s: C 基线编译失败\n' "$RED" "$NC" "$name"
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

		# C 基线：同一源码规模，环境变量 BENCH_SCALE。
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
			printf '%s[FAIL]%s %s %s: C 基线运行失败 rc=%s\n' "$RED" "$NC" "$name" "$inp" "$c_rc"
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
			printf '%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
				"$name" "$inp" "${a_ns:-}" "${b_ns:-}" "${c_best:-}" \
				"${a_c%-x}" "${b_c%-x}" "${b_a%-x}" "$valid" >>"$CSV"
		fi
	done
done

printf '\nsummary: cases=%d bad=%d\n' "$TOTAL" "$BAD"
[ -n "$CSV" ] && printf 'csv: %s\n' "$CSV"

[ "$BAD" -gt 0 ] && exit 1
exit 0
