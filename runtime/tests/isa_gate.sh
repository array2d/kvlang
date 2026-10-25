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
FIXTURE="$ROOT/layout/tests/fixtures/isa_core.txt"
[ -f "$FIXTURE" ] || fail "missing manifest fixture"
fh=$(sed -n 's/^hash	\([0-9a-f]\{32\}\)$/\1/p' "$FIXTURE" | head -1)
[ "$fh" = "$hash" ] || fail "manifest fixture drifted: runtime $hash vs fixture $fh"
for op in println print input http·call networld/fs·read; do
    kvspace get "/lib/$op" 2>/dev/null | grep -q "def rwir" \
        || fail "bridge fixture drift: /lib/$op not def rwir"
done
printf '%s' "$core" | grep -q "^-	1	1	1	any\.\.\.	any	-1	-1	-1	pure	-1	core	runtime-c$" \
    || fail "- manifest record (dynamic min-arity 1)"
printf '%s' "$core" | grep -q "^int64·add	2	1	0	int64	int64	int64	0	32	8	pure	-1	core	runtime-c$" \
    || fail "int64·add manifest record"
printf '%s' "$core" | grep -q "^int64·eq	2	1	0	int64	int64	bool	0	32	1	pure	-1	core	runtime-c$" \
    || fail "int64·eq manifest record"
printf '%s' "$core" | grep -q "^int64	1	1	0	any	int64	0	32	8	pure	-1	core	runtime-c$" \
    || fail "cast int64 manifest record"
printf '%s' "$core" | grep -q "^char/utf8	1	1	0	any	\[\]char/utf8	1	64	-1	pure	-1	core	runtime-c$" \
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

# 7) 完整 manifest 的三类变更（重算 hash 后写回控制 store）
export KVSPACE="$DSN.3"
"$TERM" isa publish >/dev/null 2>&1 || fail "isa publish dsn3"
CTL3="$DSN.3.isa"
core300=$(KVSPACE="$CTL3" kvspace get '/‥isa/‥core' | tr -d '\0')
printf '%s' "$core300" | grep -q "^count	300$" || fail "isa publish manifest count"
MODCORE=/tmp/isa-core-$$.txt
MODMETA=/tmp/isa-meta-$$.txt
make_mod() { # $1 = drop|arity|benign；输出新 hash
    python3 - "$core300" "$1" "$MODCORE" "$MODMETA" <<'PYEOF'
import sys, hashlib
core, mode, outc, outm = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
lines = core.rstrip('\n').split('\n')
recs = lines[3:]
if mode == 'drop':
    recs = [r for r in recs if not r.startswith('int64·add\t')]
elif mode == 'arity':
    out = []
    for r in recs:
        c = r.split('\t')
        if c[0] == 'int64·add':
            c[1] = '3'
            c.insert(4, 'int64')
            r = '\t'.join(c)
        out.append(r)
    recs = out
elif mode == 'benign':
    out = []
    for r in recs:
        c = r.split('\t')
        if c[0] == 'debugger':
            c[-3] = '0'
            r = '\t'.join(c)
        out.append(r)
    recs = out
else:
    raise SystemExit('bad mode')
region = ''.join(x + '\n' for x in recs)
h = hashlib.sha256(region.encode()).hexdigest()[:32]
open(outc, 'w').write('ver\t1\ncount\t%d\nhash\t%s\n%s' % (len(recs), h, region))
open(outm, 'w').write('ver\t1\ncount\t%d\nhash\t%s\nowners\truntime-c\n' % (len(recs), h))
print(h)
PYEOF
}
set_mod() {
    KVSPACE="$CTL3" kvspace set '/‥isa/‥core' "string:$(cat "$MODCORE")" >/dev/null \
        || fail "write modified core"
    KVSPACE="$CTL3" kvspace set '/‥isa/‥meta' "string:$(cat "$MODMETA")" >/dev/null \
        || fail "write modified meta"
}

# 7a 未知 opcode：从 manifest 删掉 int64·add → layout 硬失败
make_mod drop >/dev/null
set_mod
if "$LAYOUT" "$PROG" "$DSN.3" >/tmp/isa-gate-$$.log 2>&1; then
    fail "manifest without int64·add must fail layout"
fi
grep -q "ISA: unknown opcode int64·add" /tmp/isa-gate-$$.log \
    || fail "unknown opcode message: $(cat /tmp/isa-gate-$$.log)"

# 7b arity 不符：int64·add 改成 3r/1w → layout 硬失败
make_mod arity >/dev/null
set_mod
if "$LAYOUT" "$PROG" "$DSN.3" >/tmp/isa-gate-$$.log 2>&1; then
    fail "arity mismatch must fail layout"
fi
grep -q "ISA: arity mismatch for int64·add" /tmp/isa-gate-$$.log \
    || fail "arity message: $(cat /tmp/isa-gate-$$.log)"

# 7c 合法变更：改 debugger 的 cost 并重算 hash → layout 成功且 ref 采用新 hash
h2=$(make_mod benign)
set_mod
"$LAYOUT" "$PROG" "$DSN.3" >/dev/null 2>&1 || fail "layout with benign manifest B"
ref3=$(KVSPACE="$CTL3" kvspace get '/‥isa/func/lib/test' | tr -d '\0')
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

# 9) 控制 store 由 KVSPACE 派生：fs → <dsn>.isa；数据 store 不含控制键
export KVSPACE="fs://$BASE.4"
"$TERM" -c 'print("boot")' >/dev/null 2>&1 || fail "boot (derived isa)"
[ -e "$BASE.4.isa" ] || fail "derived .isa not created"
KVSPACE="fs://$BASE.4.isa" kvspace get '/‥isa/‥core' 2>/dev/null | grep -q "count" \
    || fail "derived control store missing manifest"
kvspace get '/‥isa/‥core' 2>/dev/null | grep -q "(nil)" \
    || fail "data store must not hold manifest"

# 10) scheme 未知：硬失败（没有环境变量兜底）
if KVSPACE=bogus://x "$TERM" -c 'print("x")' >/dev/null 2>&1; then
    fail "unknown scheme must fail"
fi

# 11) kvlang isa publish：不跑完整 boot 也能让 layout CLI 独立工作
export KVSPACE="fs://$BASE.5"
"$TERM" isa publish >/dev/null 2>&1 || fail "isa publish"
[ -e "$BASE.5.isa" ] || fail "isa publish did not create control store"
KVSPACE="fs://$BASE.5.isa" kvspace get '/‥isa/‥core' 2>/dev/null | grep -q "^count	300$" \
    || fail "isa publish manifest count"
"$LAYOUT" "$PROG" "fs://$BASE.5" >/dev/null 2>&1 || fail "layout after isa publish"
KVSPACE="fs://$BASE.5.isa" kvspace get '/‥isa/func/lib/test' 2>/dev/null | grep -q "hash" \
    || fail "ref missing after isa publish"

rm -f "$PROG" "$PKGPROG" "/tmp/isa-gate-$$.log" "$MODCORE" "$MODMETA"
rm -rf "$BASE" "$BASE.isa" "$BASE.2" "$BASE.2.isa" "$BASE.3" "$BASE.3.isa" \
    "$BASE.4" "$BASE.4.isa" "$BASE.4ctl" "$BASE.pkg" "$BASE.pkg.isa" \
    "$BASE.5" "$BASE.5.isa"
echo "ok: ISA control store / per-func ref / hash gate / conformance"
