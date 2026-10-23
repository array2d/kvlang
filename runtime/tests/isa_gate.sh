#!/bin/bash
# P1a ISA registry（控制面独立 store）：发布、per-func ref、双 store 隔离、
# layout hash 闸门、runtime stale 闸门、conformance 1 条。
set -u
LAYOUT=${KVLANG_LAYOUT_BIN:?}
TERM=${KVLANG_TERM_BIN:?}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BASE=/tmp/isa-gate-$$
DSN="fs://$BASE"
CTL="$DSN.isa"
rm -rf "$BASE" "$BASE.isa" "$BASE.2" "$BASE.2.isa" "$BASE.3" "$BASE.3.isa"
export KVSPACE="$DSN"
fail() { echo "FAIL: $*" >&2; exit 1; }

PROG=/tmp/isa-gate-$$.kv
cat > "$PROG" <<'EOF'
rwfunc test() -> () {
	a = 2 + 3
	println(a)
}
EOF

# 1) boot runtime-c：发布到控制 store 的 /‥isa/‥core + ‥meta
"$TERM" -c 'print("boot")' >/dev/null 2>&1 || fail "boot runtime"
[ -e "$BASE.isa" ] || fail "control store not created"
core=$(KVSPACE="$CTL" kvspace get '/‥isa/‥core' | tr -d '\0') \
    || fail "no core manifest"
meta=$(KVSPACE="$CTL" kvspace get '/‥isa/‥meta' | tr -d '\0') || fail "no meta"
hash=$(printf '%s' "$meta" | sed -n 's/.*hash\t\([0-9a-f]\{32\}\).*/\1/p')
[ -n "$hash" ] || fail "meta hash"
printf '%s' "$core" | grep -q "^count	300$" || fail "core count != 300"
printf '%s' "$core" | grep -q "^int64·add	2	1	0	int64	int64	int64	0	32	8	pure	-1	core	runtime-c$" \
    || fail "int64·add manifest record"
printf '%s' "$core" | grep -q "^int64·eq	2	1	0	int64	int64	bool	0	32	1	pure	-1	core	runtime-c$" \
    || fail "int64·eq manifest record"
printf '%s' "$core" | grep -q "^int64	1	1	0	any	int64	0	32	8	pure	-1	core	runtime-c$" \
    || fail "cast int64 manifest record"
printf '%s' "$core" | grep -q "^char/utf8	1	1	0	any	char/utf8	1	64	-1	pure	-1	core	runtime-c$" \
    || fail "cast char/utf8 manifest record"
printf '%s' "$core" | grep -qE "^array	1	1	1	any\.\.\.	any	-1	-1	-1	pure	-1	ext	runtime-c$" \
    || fail "array manifest record (dynamic ext)"
printf '%s' "$core" | grep -q "^string·len	1	1	0	any	any	-1	-1	-1	pure	-1	ext	runtime-c$" \
    || fail "string·len manifest record (ext)"
printf '%s' "$core" | grep -q "^kvspace·set	3	1	0	any	any	any	any	-1	-1	-1	write	-1	core	runtime-c$" \
    || fail "kvspace·set manifest record"
printf '%s' "$core" | grep -q "^vthread·run	1	0	0	any	-1	-1	-1	control,write	-1	core	runtime-c$" \
    || fail "vthread·run manifest record"
printf '%s' "$core" | grep -q "^debugger	0	0	0	-1	-1	-1	debug	-1	core	runtime-c$" \
    || fail "debugger manifest record"

# 2) 双 store 隔离：数据 store 不写任何 /lib/isa key
[ "$(kvspace list /lib/ 2>/dev/null | grep -c '^isa')" = "0" ] \
    || fail "data store must not contain /lib/isa"
kvspace get '/lib/isa/‥core' 2>/dev/null | grep -q "(nil)" \
    || fail "data store has /lib/isa/‥core"

# 3) layout 写 per-func ref 到控制 store（hash = core hash）
"$LAYOUT" "$PROG" "$DSN" >/dev/null 2>&1 || fail "layout"
ref=$(KVSPACE="$CTL" kvspace get '/‥isa/func/lib/test' | tr -d '\0') \
    || fail "no /‥isa/func ref"
printf '%s' "$ref" | grep -q "hash	$hash" || fail "ref hash != core hash"
printf '%s' "$ref" | grep -qE "ext_bridge	[0-9a-f]{32}" \
    || fail "ext_bridge must hash the def-rwir bridge ops"
op=$(kvspace get '/lib/test/[1,0]' | tr -d '\0')
printf '%s' "$op" | grep -q "int64·add" || fail "specialized opcode missing"
# 数据 store 里旧位置不得出现 ref
kvspace get '/lib/test/‥isa' 2>/dev/null | grep -q "(nil)" \
    || fail "data store must not contain /lib/<func>/‥isa"

# 4) conformance：执行结果
out=$("$TERM" "$PROG" 2>/dev/null | head -1)
[ "$out" = "5" ] || fail "conformance: want 5, got '$out'"

# 5) hash 闸门：篡改控制 store 的 meta hash 后 layout 必须硬失败
bad=$(printf '%s' "$meta" | sed "s/hash	$hash/hash	00000000000000000000000000000000/")
KVSPACE="$CTL" kvspace set '/‥isa/‥meta' "string:$bad" >/dev/null || fail "tamper meta"
if "$LAYOUT" "$PROG" "$DSN" >/tmp/isa-gate-$$.log 2>&1; then
    fail "tampered meta must fail layout"
fi
grep -q "ISA manifest hash mismatch" /tmp/isa-gate-$$.log \
    || fail "hash gate message: $(cat /tmp/isa-gate-$$.log)"

# 6) runtime 闸门：篡改 per-func ref，再 run 必须报 stale ISA layout
export KVSPACE="$DSN.2"
"$TERM" -c 'print("boot")' >/dev/null 2>&1 || fail "boot dsn2"
"$TERM" layout "$PROG" >/dev/null 2>&1 || fail "kvlang layout"
vid=$("$TERM" create test 2>/dev/null | head -1)
[ -n "$vid" ] || fail "create vid"
KVSPACE="$DSN.2.isa" kvspace set '/‥isa/func/lib/test' "string:ver	1
hash	ffffffffffffffffffffffffffffffff
count	300
" >/dev/null || fail "tamper ref"
runout=$("$TERM" run "$vid" 2>&1)
runrc=$?
[ "$runrc" != 0 ] || fail "stale ref must fail run"
printf '%s' "$runout" | grep -q "stale ISA layout" || fail "runtime gate: $runout"

# 7) manifest 更新：换一份合法 manifest B 到控制 store，layout 必须读到新 hash
export KVSPACE="$DSN.3"
"$TERM" -c 'print("boot")' >/dev/null 2>&1 || fail "boot dsn3"
rec=$(printf 'call\t1\t0\t0\tany\t-1\t-1\t-1\tcontrol\t-1\tcore\truntime-c')
h2=$(printf '%s\n' "$rec" | sha256sum | cut -c1-32)
body=$(printf 'ver\t1\ncount\t1\nhash\t%s\n%s\n' "$h2" "$rec")
metab=$(printf 'ver\t1\ncount\t1\nhash\t%s\nowners\truntime-c\n' "$h2")
KVSPACE="$DSN.3.isa" kvspace set '/‥isa/‥core' "string:$body" >/dev/null || fail "write manifest B"
KVSPACE="$DSN.3.isa" kvspace set '/‥isa/‥meta' "string:$metab" >/dev/null || fail "write meta B"
"$LAYOUT" "$PROG" "$DSN.3" >/dev/null 2>&1 || fail "layout with manifest B"
ref3=$(KVSPACE="$DSN.3.isa" kvspace get '/‥isa/func/lib/test' | tr -d '\0')
printf '%s' "$ref3" | grep -q "hash	$h2" || fail "layout did not adopt new hash"

# 8) pkg/nested-pkg 路径：控制键把成员分隔符 '·' 折成 '/'，layout/runtime 同规则
#    （durable 里 '·' 是 memitem 分隔符，未声明 memhead 直写会失败）
PKGPROG=/tmp/isa-gate-pkg-$$.kv
cat > "$PKGPROG" <<'EOF'
lib a {
	rwfunc b() -> (r:int64) {
		1 -> r
	}
}

lib a/b {
	rwfunc c() -> (r:int64) {
		2 -> r
	}
}

rwfunc test() -> () {
	println("pkg ok")
}
EOF
export KVSPACE="fs://$BASE.pkg"
"$TERM" -c 'print("boot")' >/dev/null 2>&1 || fail "boot pkg"
"$LAYOUT" "$PKGPROG" "fs://$BASE.pkg" >/dev/null 2>&1 || fail "layout pkg"
KVSPACE="fs://$BASE.pkg.isa" kvspace get '/‥isa/func/lib/a/b' 2>/dev/null | grep -q "hash" \
    || fail "pkg ref /‥isa/func/lib/a/b missing"
KVSPACE="fs://$BASE.pkg.isa" kvspace get '/‥isa/func/lib/a/b/c' 2>/dev/null | grep -q "hash" \
    || fail "nested pkg ref /‥isa/func/lib/a/b/c missing"
KVSPACE="fs://$BASE.pkg.isa" kvspace get '/‥isa/func/lib/a·b' 2>/dev/null | grep -q "(nil)" \
    || fail "raw '·' key must not exist in control store"
pv=$("$TERM" test 2>/dev/null | head -1)
[ "$pv" = "pkg ok" ] || fail "pkg conformance: '$pv'"

# 9) KVSPACE_ISA 显式覆盖：数据 store 与派生 .isa 都不出现控制键
export KVSPACE="fs://$BASE.4"
KVSPACE_ISA="fs://$BASE.4ctl" "$TERM" -c 'print("boot")' >/dev/null 2>&1 \
    || fail "boot with KVSPACE_ISA"
KVSPACE="fs://$BASE.4ctl" kvspace get '/‥isa/‥core' 2>/dev/null | grep -q "count" \
    || fail "explicit control store missing manifest"
kvspace get '/‥isa/‥core' 2>/dev/null | grep -q "(nil)" \
    || fail "data store must not hold manifest with explicit KVSPACE_ISA"
[ ! -e "$BASE.4.isa" ] || fail "derived .isa must not be created with KVSPACE_ISA"

# 10) 未设 KVSPACE_ISA 且 scheme 未知：硬失败（不静默回落）
if KVSPACE=bogus://x "$TERM" -c 'print("x")' >/dev/null 2>&1; then
    fail "unknown scheme without KVSPACE_ISA must fail"
fi

rm -f "$PROG" "$PKGPROG" "/tmp/isa-gate-$$.log"
rm -rf "$BASE" "$BASE.isa" "$BASE.2" "$BASE.2.isa" "$BASE.3" "$BASE.3.isa" \
    "$BASE.4" "$BASE.4.isa" "$BASE.4ctl" "$BASE.pkg" "$BASE.pkg.isa"
echo "ok: ISA control store / per-func ref / hash gate / conformance"
