#!/usr/bin/env bash
# layout 阶段拒绝数值算子的类型/arity 错误（A1 原型）。
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${KVLANG_LAYOUT_BIN:-$ROOT/bin/kvlanglayout}"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
DSN="fs://$tmp/db"; mkdir -p "$tmp/db"

check() { # name, source, expected-substring
	local name=$1 src=$2 want=$3
	printf '%s\n' "$src" > "$tmp/$name.kv"
	out=$("$LAYOUT" "$tmp/$name.kv" "$DSN" 2>&1)
	rc=$?
	if [ "$rc" = 0 ]; then
		echo "FAIL: $name 应被 layout 拒绝，实际通过"
		exit 1
	fi
	case "$out" in
	*"$want"*) ;;
	*) echo "FAIL: $name 缺少诊断 '$want'：$out"; exit 1 ;;
	esac
}

check mix_type 'rwfunc test() -> () { x = "abc" - 1 }' 'cannot mix string and numeric'
check non_numeric 'rwfunc test() -> () { x = true + 1 }' 'expects numeric operand'
check arity 'rwfunc test() -> () { x = sqrt(4, 2) }' 'sqrt expects 1 operand'

accept() { # name, source：动态 any / 混合宽度 / 字符串拼接必须放行（走通用实现）
	local name=$1 src=$2
	printf '%s\n' "$src" > "$tmp/$name.kv"
	if ! "$LAYOUT" "$tmp/$name.kv" "$DSN" >/dev/null 2>&1; then
		echo "FAIL: $name 不应被 layout 拒绝"
		exit 1
	fi
}

accept dyn_any 'rwfunc f(x:any) -> () { y = x + 1 }'
accept char_concat 'rwfunc f() -> () { y = "a" + "b" }'
accept mixed_width 'rwfunc f() -> () { a:int32 = 1 b:int64 = 2 c = a + b }'

echo "PASS: layout 拒绝类型/arity 错误；动态 any/混合宽度/字符串拼接放行"
