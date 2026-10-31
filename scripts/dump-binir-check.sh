#!/usr/bin/env bash
# #583 验收：binir 序列转储结构检查（RC2_DUMP_BINIR=1）。
#
# 运行指定 case，抓 stderr 上的 `[binir]` 转储，逐段校验：
#   段头 `[binir] func=... ninsn=... nreg=... nconst=...`
#   指令行 `  0000  add r2, r0, r1 ; int64·add [JIT]`（含 [JIT]/[INTERP] 覆盖标记）
#   `const[i] ...`、`reg[i] ...`、`exit: JIT x / INTERP y`
# 并校验每段 JIT + INTERP == ninsn（#583 §2·7.1 全覆盖契约）。
#
# dump 由 coding4 实现；当前构建尚无输出时打印 SKIP 并退出 0（--require 则退出 1）。
#
# 用法：
#   scripts/dump-binir-check.sh                      # 默认 benchmark/cases/iops
#   scripts/dump-binir-check.sh --file some.kv
#   scripts/dump-binir-check.sh --file x.kv --scale 100
#   scripts/dump-binir-check.sh --require            # dump 未实现也判失败
#   scripts/dump-binir-check.sh --keep               # 保留临时文件并打印路径
#
# 环境变量：KVLANG_BIN（默认 <repo>/bin/byteseek）、KV_CASE_TIMEOUT（默认 180）
#
# 退出码：0 结构通过 / dump 未实现而跳过；1 结构缺项或不一致；2 环境/用法错误。
set -uo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT" || exit 2

KVLANG_BIN="${KVLANG_BIN:-$ROOT/bin/byteseek}"
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

  --file FILE   要转储的 .kv（默认 benchmark/cases/iops/iops.kv）
  --scale N     源码内 __SCALE__ 的替换值（默认 200）
  --require     RC2_DUMP_BINIR 无输出时判失败（默认 SKIP 退出 0）
  --keep        保留临时目录并打印路径
  -h, --help    显示本帮助

退出码：0 通过/跳过；1 结构不符；2 环境/用法错误。
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

[ -x "$KVLANG_BIN" ] || die "KVLANG_BIN 不可执行：$KVLANG_BIN"
[ -f "$FILE" ] || die "文件不存在：$FILE"
[[ "$SCALE" =~ ^[0-9]+$ ]] || die "--scale 需为正整数：$SCALE"
command -v timeout >/dev/null 2>&1 || die "需要 coreutils timeout"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/kvlang-binir.XXXXXX")" || die "mktemp 失败"
cleanup() { [ "$KEEP" -eq 1 ] || rm -rf "$TMP"; }
trap cleanup EXIT

SRC="$TMP/case.kv"
sed "s/__SCALE__/$SCALE/g" "$FILE" >"$SRC"
DSN="shm://$TMP/store.shm"
STDOUT="$TMP/stdout.txt"
STDERR="$TMP/stderr.txt"

env KVSPACE="$DSN" RC2_DUMP_BINIR=1 timeout -k 5 "$CASE_TIMEOUT" \
	"$KVLANG_BIN" "$SRC" >"$STDOUT" 2>"$STDERR"
RC=$?

if [ "$KEEP" -eq 1 ]; then
	printf 'tmp: %s\n' "$TMP"
fi
printf 'file=%s scale=%s exit=%s\n' "$FILE" "$SCALE" "$RC"
[ "$RC" -eq 0 ] || printf '%s[warn]%s 程序退出码非 0，结构检查继续进行\n' "$YELLOW" "$NC"

# 转储按设计走 stderr；兜底也接受 stdout（实现方若写错流仍可检查）。
DUMP="$TMP/dump.txt"
if grep -q '^\[binir\]' "$STDERR"; then
	cp "$STDERR" "$DUMP"
elif grep -q '^\[binir\]' "$STDOUT"; then
	printf '%s[warn]%s [binir] 出现在 stdout（规范要求 stderr）\n' "$YELLOW" "$NC"
	cp "$STDOUT" "$DUMP"
else
	if [ "$REQUIRE" -eq 1 ]; then
		printf '%s[FAIL]%s RC2_DUMP_BINIR=1 无 [binir] 输出（--require）\n' "$RED" "$NC"
		exit 1
	fi
	printf '%s[SKIP]%s 当前构建未实现 binir dump（RC2_DUMP_BINIR 无输出，coding4 合入后重跑）\n' \
		"$YELLOW" "$NC"
	exit 0
fi

# 先给报告一段样例（段头 + 前几条指令）。
printf '\n-- sample --\n'
grep -m1 '^\[binir\]' "$DUMP"
grep -E '^[[:space:]]+[0-9]+[[:space:]]+' "$DUMP" | head -n 6
printf -- '-- end sample --\n\n'

awk '
function fail(msg) { printf "  FAIL: %s\n", msg; seg_fail = 1; global_fail = 1 }
function finish() {
	if (seg == 0) return
	if (insn == 0) fail("段 " seg " 无指令行")
	if (!saw_exit) fail("段 " seg " 缺 exit: JIT x / INTERP y")
	if (saw_exit && jit + interp != ninsn)
		fail("段 " seg " JIT(" jit ") + INTERP(" interp ") != ninsn(" ninsn ")")
	if (nconst > 0 && constn == 0) fail("段 " seg " nconst=" nconst " 但无 const[] 行")
	if (nreg > 0 && regn == 0) fail("段 " seg " nreg=" nreg " 但无 reg[] 行")
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
	if (ninsn < 0 || nreg < 0 || nconst < 0) fail("段 " seg " 段头缺 ninsn/nreg/nconst 字段")
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
	if (seg == 0) { printf "  FAIL: 无 [binir] 段头\n"; global_fail = 1 }
	if (!have_insn) { printf "  FAIL: 无带 [JIT]/[INTERP] 的指令行\n"; global_fail = 1 }
	if (!have_const) { printf "  FAIL: 无 const[] 行\n"; global_fail = 1 }
	if (!have_reg) { printf "  FAIL: 无 reg[] 行\n"; global_fail = 1 }
	if (!have_exit) { printf "  FAIL: 无 exit: JIT x / INTERP y 行\n"; global_fail = 1 }
	printf "segments=%d instructions=%d\n", seg, insn_total
	exit (global_fail ? 1 : 0)
}
' "$DUMP"
VERDICT=$?

if [ "$VERDICT" -eq 0 ]; then
	printf '%s[PASS]%s binir 转储结构完整\n' "$GREEN" "$NC"
	exit 0
fi
printf '%s[FAIL]%s binir 转储结构缺项/不一致\n' "$RED" "$NC"
exit 1
