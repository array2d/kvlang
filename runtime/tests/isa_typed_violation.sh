#!/bin/bash
# P1b3：KV_DEBUG_TYPES 构建的负例——把已定型槽改成错误 kind，
# 继续执行时 typed 断言必须触发（不静默）。
# 需要 KVLANG_DEBUG_RUNTIME_LIB=/path/libkvlang_runtime.so（KV_DEBUG_TYPES=ON 构建）。
set -u
LIB=${KVLANG_DEBUG_RUNTIME_LIB:-}
[ -n "$LIB" ] && [ -f "$LIB" ] || exit 77 # ctest SKIP_RETURN_CODE
LAYOUT=${KVLANG_LAYOUT_BIN:?}
TERM=${KVLANG_TERM_BIN:?}
BASE=/tmp/isa-typed-violation-$$
rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
mkdir -p "$BASE" "$BASE.bin"
cp "$TERM" "$BASE.bin/kvlang"
cp "$LAYOUT" "$BASE.bin/kvlanglayout"
cp "$(dirname "$LAYOUT")/libkvlanglayout.so" "$BASE.bin/" 2>/dev/null || true
cp "$LIB" "$BASE.bin/libkvlang_runtime.so"
TERM="$BASE.bin/kvlang"

PROG="$BASE/p.kv"
cat > "$PROG" <<'EOF'
rwfunc test() -> () {
	a = 2
	vthread·setstatus("paused")
	int64·add(a, 1) -> r
	println(r)
}
EOF
export KVSPACE="shm://$BASE/s"
"$TERM" isa publish >/dev/null 2>&1 || {
    rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
    echo "FAIL: publish" >&2
    exit 1
}
"$LAYOUT" "$PROG" "shm://$BASE/s" >/dev/null 2>&1 || {
    rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
    echo "FAIL: layout" >&2
    exit 1
}
vid=$("$TERM" create test 2>/dev/null | head -1)
[ -n "$vid" ] || {
    rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
    echo "FAIL: create" >&2
    exit 1
}
"$TERM" run "$vid" >/dev/null 2>&1 # 暂停在第一行
kvspace set "/vthread/$vid/[1]/a" "string:zz" >/dev/null || {
    rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
    echo "FAIL: tamper slot" >&2
    exit 1
}
kvspace set "/vthread/$vid/‥status" "string:running" >/dev/null || {
    rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
    echo "FAIL: resume status" >&2
    exit 1
}
out=$("$TERM" run "$vid" 2>&1)
rc=$?
rm -rf "$BASE" "$BASE.isa" "$BASE.bin"
if [ "$rc" = 0 ]; then
    echo "FAIL: typed violation not detected" >&2
    exit 1
fi
printf '%s' "$out" | grep -q "typed int64·add" || {
    echo "FAIL: wrong diagnostic: $out" >&2
    exit 1
}
echo "ok: typed violation detected"
