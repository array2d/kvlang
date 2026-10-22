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

# typed 槽赋值转换（[[赋值转换]]）
check int8_lit_overflow 'rwfunc f() -> () { x:int8 = 200 }' 'not representable as int8'
check uint8_negative 'rwfunc f() -> () { x:uint8 = -1 }' 'not representable as uint8'
check int64_fraction 'rwfunc f() -> () { x:int64 = 1.5 }' 'is not an integer'
check float32_overflow 'rwfunc f() -> () { x:float32 = 1e40 }' 'out of float32 range'
check string_to_int 'rwfunc f() -> () { x:int64 = "abc" }' 'cannot assign string to int64'
check none_to_int 'rwfunc f() -> () { x:int64 = None }' 'None cannot be assigned'
check array_elem_overflow 'rwfunc f() -> () { a:[]int8 = [200, 2] }' 'not representable as int8'
check call_lit_overflow 'rwfunc g(n:int8) -> () { } rwfunc f() -> () { g(200) }' 'not representable as int8'
check struct_field_overflow 'struct S { v:int8=0 }
rwfunc f() -> () { s:S = {v=200} }' 'not representable as int8'
check element_write_overflow 'rwfunc f() -> () { a:[]int8 = [1, 2]
a[0] = 200 }' 'not representable as int8'
check container_mismatch 'rwfunc f() -> () { b:[]int64 = [1, 2]
a:[]int8 = b }' 'cannot assign []int64 to []int8'

accept int8_lit_ok 'rwfunc f() -> () { x:int8 = 100 }'
accept float32_narrow 'rwfunc f() -> () { x:float32 = 0.1 }'
accept explicit_cast_wrap 'rwfunc f() -> () { x:int8 = int8(300) }'
accept dyn_value 'rwfunc f(v:any) -> () { x:int8 = v }'
accept call_dyn_value 'rwfunc g(n:int8) -> () { } rwfunc f(v:any) -> () { g(v) }'
accept array_int8_ok 'rwfunc f() -> () { a:[]int8 = [100, 2] }'

echo "PASS: layout 拒绝类型/arity/赋值转换错误；动态 any/混合宽度/字符串拼接放行"
