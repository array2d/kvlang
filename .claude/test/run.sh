#!/usr/bin/env bash
# kvlang 自动化测试脚本
# 用法（在项目根目录执行）:
#   .claude/test/run.sh                   全量测试
#   .claude/test/run.sh --filter serve    只跑含 "serve" 的测试
set -uo pipefail

cd "$(dirname "$0")/../.."

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
PASS=0; FAIL=0; SKIP=0

FILTER=""
if [ "${1:-}" = "--filter" ]; then FILTER="${2:-}"; fi

# ─── helpers ──────────────────────────────────────────────────────────────────
ok()   { echo -e "${GREEN}✅ $1${NC}"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}❌ $1${NC}";   FAIL=$((FAIL+1)); }
skip() { echo -e "${YELLOW}⏭  $1${NC}"; SKIP=$((SKIP+1)); }
section() { echo; echo -e "${YELLOW}── $1 ──${NC}"; }

should_run() { [ -z "$FILTER" ] || [[ "$1" == *"$FILTER"* ]]; }

# check_out DESC PATTERN CMD...   — CMD stdout 含 PATTERN
check_out() {
    local desc="$1" pat="$2"; shift 2
    should_run "$desc" || { skip "$desc"; return; }
    local out; out=$(eval "$@" 2>/dev/null) || true
    if echo "$out" | grep -qF -- "$pat"; then ok "$desc"
    else fail "$desc (want: '$pat'  got: ${out:0:120})"; fi
}

# check_err DESC PATTERN CMD...   — CMD stderr 含 PATTERN（忽略退出码）
check_err() {
    local desc="$1" pat="$2"; shift 2
    should_run "$desc" || { skip "$desc"; return; }
    local err; err=$(eval "$@" 2>&1 >/dev/null) || true
    if echo "$err" | grep -qF -- "$pat"; then ok "$desc"
    else fail "$desc (want: '$pat'  stderr: ${err:0:120})"; fi
}

# check_exit DESC WANT_EXIT CMD...
check_exit() {
    local desc="$1" want="$2"; shift 2
    should_run "$desc" || { skip "$desc"; return; }
    local rc=0; eval "$@" >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq "$want" ]; then ok "$desc"
    else fail "$desc (exit=$rc, want=$want)"; fi
}

# check_any DESC PATTERN CMD...   — stdout+stderr 合并含 PATTERN
check_any() {
    local desc="$1" pat="$2"; shift 2
    should_run "$desc" || { skip "$desc"; return; }
    local out; out=$(eval "$@" 2>&1) || true
    if echo "$out" | grep -qF -- "$pat"; then ok "$desc"
    else fail "$desc (want: '$pat'  out: ${out:0:180})"; fi
}

KV=./kvlang
PRINT_INT=example/kvlang/builtin/print/print_int.kv
ADD_KV=example/kvlang/builtin/arith/add.kv
SERVE_STDERR=/tmp/kvlang_test_serve.stderr
WATCH_OUT=/tmp/kvlang_test_watch.out
trap 'rm -f $SERVE_STDERR $WATCH_OUT' EXIT

# ── §0 前置条件 ───────────────────────────────────────────────────────────────
section "§0 前置条件"
check_out  "Redis 在线"         "PONG"  'redis-cli ping'
check_exit "kvlang 二进制存在"  0       'test -f ./kvlang'
check_exit "print_int.kv 存在"  0       "test -f $PRINT_INT"
check_exit "add.kv 存在"        0       "test -f $ADD_KV"

# ── §1 构建与静态检查 ─────────────────────────────────────────────────────────
section "§1 构建与静态检查"
check_exit "go build ./..."     0  'go build ./...'
check_exit "go vet ./..."       0  'go vet ./...'
check_exit "go test ./..."      0  'go test ./... -count=1'
check_exit "check-keytree"      0  '.claude/hooks/check-keytree.sh'
# 零 redis 直引用（internal/kvspace 除外）
REDIS_LEAK=$(grep -rn "github.com/redis" --include="*.go" . \
    | grep -v "internal/kvspace" | grep -v "go\." || true)
if [ -z "$REDIS_LEAK" ]; then ok "零 redis 包直引用"; else fail "零 redis 包直引用: $REDIS_LEAK"; fi

# ── §2 help ───────────────────────────────────────────────────────────────────
section "§2 help"
check_any "help 子命令"     "usage:"   "$KV help"
check_any "-h flag"         "usage:"   "$KV -h"
check_any "--help flag"     "usage:"   "$KV --help"
check_any "help 含 load"    "load"     "$KV help"
check_any "help 含 serve"   "serve"    "$KV help"
check_any "help 含 kvspace" "kvspace"  "$KV help"

# ── §3 load ───────────────────────────────────────────────────────────────────
section "§3 load"
$KV kvspace clear >/dev/null 2>&1

check_any  "load 文件"              "loaded 1 file"   "LOG_LEVEL=info $KV load $PRINT_INT"
check_out  "load 后 /func/main 存在" '"entry"'         "$KV kvspace get /func/main"
check_exit "load 后无 vthread"       1                 "$KV kvspace list /vthread 2>/dev/null | grep -q ."
check_any  "load --addr"             "loaded"          "LOG_LEVEL=info $KV load --addr 127.0.0.1:6379 $PRINT_INT"
check_any  "load 目录"               "loaded"          "LOG_LEVEL=info $KV load example/kvlang/builtin/print/"
check_err  "load 缺路径 → usage"     "usage:"          "$KV load"
check_err  "load 未知 flag"          "flag provided"   "$KV load --unknown"
check_any  "load --help 含 addr"     "addr"            "$KV load --help"

# ── §4 run（默认子命令）──────────────────────────────────────────────────────
section "§4a run 文件模式"
$KV kvspace clear >/dev/null 2>&1
check_out "print_int → X=42"  "X = 42"  "$KV $PRINT_INT"
$KV kvspace clear >/dev/null 2>&1
check_out "print_int → R=42"  "R = 42"  "$KV $PRINT_INT"
$KV kvspace clear >/dev/null 2>&1
check_out "add.kv → C=5"      "C = 5"   "$KV $ADD_KV"
$KV kvspace clear >/dev/null 2>&1
check_out "run --addr"        "X = 42"  "$KV --addr 127.0.0.1:6379 $PRINT_INT"

section "§4b run 内联 -c"
$KV kvspace clear >/dev/null 2>&1
INLINE=$(cat <<'KV'
def add2(A:int, B:int) -> (C:int) {
    './C' <- A + B
}
str.set("kvlangrun") -> './term'
add2(10, 32) -> './sum'
print("sum =", './sum')
KV
)
check_out "run -c inline"  "sum = 42"  "$KV -c \"\$INLINE\""

section "§4c run 管道模式"
$KV kvspace clear >/dev/null 2>&1
check_out "run pipe"  "X = 42"  "cat $PRINT_INT | $KV"

# ── §5 serve ──────────────────────────────────────────────────────────────────
section "§5 serve"
check_any "serve 启动日志"   "starting"    "LOG_LEVEL=info timeout 2 $KV serve"
check_any "serve --addr 日志" "127.0.0.1"  "LOG_LEVEL=info timeout 2 $KV serve --addr 127.0.0.1:6379"
check_any "serve --help"     "addr"        "$KV serve --help"
check_err "serve 未知 flag"  "flag provided" "$KV serve --unknown"

section "§5.1 load → serve 集成"
$KV kvspace clear >/dev/null 2>&1
LOG_LEVEL=info $KV load $PRINT_INT >/dev/null 2>&1
SERVE_STDOUT=$(LOG_LEVEL=info timeout 6 $KV serve 2>"$SERVE_STDERR") || true
# check stdout
if echo "$SERVE_STDOUT" | grep -qF "X = 42"; then ok "serve → X = 42"
else fail "serve → X = 42 (stdout: ${SERVE_STDOUT:0:80})"; fi
if echo "$SERVE_STDOUT" | grep -qF "R = 42"; then ok "serve → R = 42"
else fail "serve → R = 42 (stdout: ${SERVE_STDOUT:0:80})"; fi
# check stderr has log
if grep -qF "entry=pre_main" "$SERVE_STDERR" 2>/dev/null; then ok "serve stderr 含 entry=pre_main"
else fail "serve stderr 含 entry=pre_main ($(cat $SERVE_STDERR 2>/dev/null | grep -v worker | head -5))"; fi

# ── §6 vet ────────────────────────────────────────────────────────────────────
section "§6 vet"
check_out "vet OK"             "OK"          "$KV vet $PRINT_INT"
check_out "vet --dump 含 Func" "Func"        "$KV vet --dump $PRINT_INT"
check_out "vet --lower OK"     "OK"          "$KV vet --lower $PRINT_INT"
check_out "vet --dump --lower" "Instruction" "$KV vet --dump --lower $PRINT_INT"
check_out "vet pipe"           "stdin: OK"   "cat $PRINT_INT | $KV vet"
check_any "vet --help 含 dump" "dump"        "$KV vet --help"
check_err "vet 无参数 → usage" "usage:"      "$KV vet"

# ── §7 format ─────────────────────────────────────────────────────────────────
section "§7 format"
check_out "format 文件"    "def "   "$KV format $PRINT_INT"
check_out "format 别名 fmt" "def "  "$KV fmt $PRINT_INT"
check_out "format pipe"    "def "   "cat $PRINT_INT | $KV format"
check_any "format --help"  "-c"     "$KV format --help"

# ── §8 kvspace ────────────────────────────────────────────────────────────────
section "§8 kvspace CRUD"
$KV kvspace clear >/dev/null 2>&1
$KV kvspace set /test/x hello >/dev/null 2>&1
check_out  "get 存在的 key"      "hello"   "$KV kvspace get /test/x"
$KV kvspace set /test/y world >/dev/null 2>&1
check_out  "mget 第一个值"       "hello"   "$KV kvspace mget /test/x /test/y"
check_out  "mget 第二个值"       "world"   "$KV kvspace mget /test/x /test/y"
check_out  "list 子项"           "x"       "$KV kvspace list /test"
$KV kvspace del /test/x >/dev/null 2>&1
check_exit "get 已删除 → exit 1" 1         "$KV kvspace get /test/x"

section "§8 kvspace tree / dump"
$KV kvspace clear >/dev/null 2>&1
$KV load $PRINT_INT >/dev/null 2>&1
check_out "tree 含函数名"       "print_int"  "$KV kvspace tree /src"
check_out "dump 含签名 def"     "def "       "$KV kvspace dump /src/print/print_int"

section "§8 kvspace notify / watch"
$KV kvspace watch --timeout 3s /test/notify >"$WATCH_OUT" 2>&1 &
WPID=$!
sleep 0.3
$KV kvspace notify /test/notify "ping-msg" >/dev/null 2>&1
wait $WPID 2>/dev/null || true
if grep -qF "ping-msg" "$WATCH_OUT" 2>/dev/null; then ok "watch 收到通知消息"
else fail "watch 收到通知消息 (out: $(cat $WATCH_OUT 2>/dev/null))"; fi

check_exit "watch 超时 exit 1"      1  "$KV kvspace watch --timeout 1s /nonexistent"
check_any  "watch --help 含 timeout" "timeout"     "$KV kvspace watch --help"
check_err  "watch 非法 duration"    "invalid value" "$KV kvspace watch --timeout xyz /k"

section "§8 kvspace --addr / clear"
check_out  "kvspace --addr get"   "entry"  "$KV kvspace --addr 127.0.0.1:6379 get /func/main"
$KV kvspace clear >/dev/null 2>&1
check_exit "clear 后 list 为空"   1        "$KV kvspace list /src 2>/dev/null | grep -q ."

# ── §9 flag 错误处理 ─────────────────────────────────────────────────────────
section "§9 Flag 错误处理"
check_err "load 未知 flag"         "flag provided"  "$KV load --unknown /f"
check_err "serve 未知 flag"        "flag provided"  "$KV serve --unknown"
check_err "vet 未知 flag"          "flag provided"  "$KV vet --unknown"
check_err "kvspace watch 非法时长" "invalid value"  "$KV kvspace watch --timeout xyz /k"
check_err "kvspace get 缺 key"     "usage:"         "$KV kvspace get"
check_err "kvspace set 缺 value"   "usage:"         "$KV kvspace set /k"
check_err "kvspace 无子命令"       "usage:"         "$KV kvspace"

# ── §10 架构合规 ──────────────────────────────────────────────────────────────
section "§10 架构合规"
# redis 直引用检查
REDIS_LEAK=$(grep -rn "github.com/redis" --include="*.go" . \
    | grep -v "internal/kvspace" | grep -v "go\." || true)
if [ -z "$REDIS_LEAK" ]; then ok "零 redis 包直引用"; else fail "redis 泄漏: $REDIS_LEAK"; fi

# 硬编码路径检查
VTHREAD_LEAK=$(grep -rn '"/vthread/' --include="*.go" . | grep -v "internal/keytree" || true)
if [ -z "$VTHREAD_LEAK" ]; then ok "零硬编码 /vthread/ 路径"; else fail "/vthread/ 泄漏: $VTHREAD_LEAK"; fi

SRCFUNC_LEAK=$(grep -rn '"\/src\/' --include="*.go" . | grep -v "internal/keytree" || true)
if [ -z "$SRCFUNC_LEAK" ]; then ok "零硬编码 /src/ 路径"; else fail "/src/ 泄漏: $SRCFUNC_LEAK"; fi

check_exit "check-keytree hook"  0  '.claude/hooks/check-keytree.sh'

# ── §11 vet 全量（所有非 tensor/index .kv 文件）────────────────────────────
section "§11 vet 全量"
VET_FAIL=0
VET_PASS=0
while IFS= read -r f; do
  should_run "§11 vet 全量" || break
  result=$(./kvlang vet "$f" 2>&1)
  if [ $? -eq 0 ]; then
    VET_PASS=$((VET_PASS+1))
  else
    fail "vet $f: $result"
    VET_FAIL=$((VET_FAIL+1))
  fi
done < <(find example/kvlang/builtin example/kvlang/controlflow \
           -name "*.kv" ! -name "index.kv" | sort)
if should_run "§11 vet 全量"; then
  if [ $VET_FAIL -eq 0 ]; then
    ok "vet 全量通过 ($VET_PASS 个文件)"
    PASS=$((PASS+1))
  fi
  [ $VET_FAIL -gt 0 ] && FAIL=$((FAIL+VET_FAIL))
fi

# ── §12 run builtin（有预期输出的文件）────────────────────────────────────
section "§12 run builtin arith"
run_kv() { ./kvlang kvspace clear >/dev/null 2>&1; timeout 10 $KV "$1" 2>/dev/null; }
BA=example/kvlang/builtin/arith

check_out "arith/add   C=5"    "C = 5"    'run_kv '"$BA/add.kv"
check_out "arith/sub   C=7"    "C = 7"    'run_kv '"$BA/sub.kv"
check_out "arith/mul   C=42"   "C = 42"   'run_kv '"$BA/mul.kv"
check_out "arith/div   C=7.5"  "C = 7.5"  'run_kv '"$BA/div.kv"
check_out "arith/abs   C=5"    "C = 5"    'run_kv '"$BA/abs.kv"
check_out "arith/neg   C=-5"   "C = -5"   'run_kv '"$BA/neg.kv"
check_out "arith/sign  C=-1"   "C = -1"   'run_kv '"$BA/sign.kv"
check_out "arith/pow   C=8"    "C = 8"    'run_kv '"$BA/pow.kv"
check_out "arith/sqrt  C=4"    "C = 4"    'run_kv '"$BA/sqrt.kv"
check_out "arith/max   C=7"    "C = 7"    'run_kv '"$BA/max.kv"
check_out "arith/min   C=-2"   "C = -2"   'run_kv '"$BA/min.kv"
check_out "arith/exp   C=7"    "C = 7"    'run_kv '"$BA/exp.kv"
check_out "arith/log   C=2"    "C = 2"    'run_kv '"$BA/log.kv"
check_out "arith/double_op  S=13"   "S = 13"  'run_kv '"$BA/double_op.kv"
check_out "arith/double_op_cstyle S=13" "S = 13" 'run_kv '"$BA/double_op_cstyle.kv"
check_out "arith/three_add R=9"     "R = 9"   'run_kv '"$BA/three_add.kv"
check_out "arith/poly3 Y=35"        "Y = 35"  'run_kv '"$BA/poly3.kv"

section "§12 run builtin print/cast/chain/compare/logic"
BP=example/kvlang/builtin/print
check_out "print/print_int   X=42"    "X = 42"   'run_kv '"$BP/print_int.kv"
check_out "print/print_bool  C=true"  "C = true" 'run_kv '"$BP/print_bool.kv"
check_out "print/print_multi R=6"     "R = 6"    'run_kv '"$BP/print_multi.kv"
check_out "print/print_chain D=20"    "D = 20"   'run_kv '"$BP/print_chain.kv"

BC=example/kvlang/builtin/cast
check_out "cast/float  C=42.0"  "C = 42.0" 'run_kv '"$BC/float.kv"
check_out "cast/int    C=3"     "C = 3"    'run_kv '"$BC/int.kv"

check_out "chain/chain D=20"    "D = 20"   'run_kv example/kvlang/builtin/chain/chain.kv'
check_out "compare/eq  C=true"  "C = true" 'run_kv example/kvlang/builtin/compare/eq.kv'
check_out "compare/lt  C=true"  "C = true" 'run_kv example/kvlang/builtin/compare/lt.kv'
check_out "logic/and   C=false" "C = false" 'run_kv example/kvlang/builtin/logic/and.kv'
check_out "logic/bool  C=true"  "C = true"  'run_kv example/kvlang/builtin/logic/bool.kv'
check_out "logic/not   C=false" "C = false" 'run_kv example/kvlang/builtin/logic/not.kv'

section "§12 run builtin native（无 print，验 exit 0）"
NAT_FAIL=0; NAT_PASS=0
while IFS= read -r f; do
  should_run "§12 run builtin native" || break
  ./kvlang kvspace clear >/dev/null 2>&1
  timeout 10 $KV "$f" >/dev/null 2>/dev/null
  rc=$?
  if [ $rc -eq 0 ]; then
    NAT_PASS=$((NAT_PASS+1))
  else
    fail "native exit=$rc: $f"
    NAT_FAIL=$((NAT_FAIL+1))
  fi
done < <(find example/kvlang/builtin/native -name "*.kv" ! -name "index.kv" | sort)
if should_run "§12 run builtin native"; then
  if [ $NAT_FAIL -eq 0 ]; then
    ok "native 全量 exit 0 ($NAT_PASS 个文件)"
    PASS=$((PASS+1))
  else
    FAIL=$((FAIL+NAT_FAIL))
  fi
fi

section "§13 run controlflow/test_runner（32 断言全 PASS）"
run_kv() { ./kvlang kvspace clear >/dev/null 2>&1 && timeout 30 $KV "$1"; }
check_out "controlflow/test_runner ALL DONE" "ALL TESTS DONE" 'run_kv example/kvlang/controlflow/test_runner.kv'

# ── 汇总 ─────────────────────────────────────────────────────────────────────
echo
echo "════════════════════════════════════"
echo -e "  ${GREEN}PASS: $PASS${NC}   ${RED}FAIL: $FAIL${NC}   ${YELLOW}SKIP: $SKIP${NC}"
echo "════════════════════════════════════"

[ "$FAIL" -eq 0 ]
