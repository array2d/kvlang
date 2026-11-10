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
PFX="$BASE.pfx"
cleanup() { rm -rf "$BASE" "$BASE.isa" "$PFX"; }
cleanup
mkdir -p "$BASE" "$PFX/bin" "$PFX/lib"
# `byteseek` uses `DT_RPATH=$ORIGIN/../lib` and links `libkvlang_runtimec2.so`:
# stage a private `bin/` + `lib/` prefix with the `KV_DEBUG_TYPES` C runtime under that soname.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REPO_LIB="$ROOT/lib"
cp "$TERM" "$PFX/bin/byteseek"
cp -P "$REPO_LIB"/libkvspace.so* "$REPO_LIB/libkvlanglayout.so" "$PFX/lib/"
cp -rP "$REPO_LIB/kvspace" "$PFX/lib/"
cp "$LIB" "$PFX/lib/libkvlang_runtimec2.so"
TERM="$PFX/bin/byteseek"
# Prefer `KVSPACE_CLI` or repo `bin/kvspace` before falling back to `PATH`.
KVCLI="${KVSPACE_CLI:-$ROOT/bin/kvspace}"
[ -x "$KVCLI" ] || KVCLI=$(command -v kvspace)
kvcli() { LD_LIBRARY_PATH="$REPO_LIB:$REPO_LIB/kvspace${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$KVCLI" "$@"; }

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
"$TERM" -c 'print("boot")' >/dev/null 2>&1 || {
    cleanup
    echo "FAIL: boot" >&2
    exit 1
}
"$LAYOUT" "$PROG" "shm://$BASE/s" >/dev/null 2>&1 || {
    cleanup
    echo "FAIL: layout" >&2
    exit 1
}
vid=$("$TERM" create test 2>/dev/null | head -1)
[ -n "$vid" ] || {
    cleanup
    echo "FAIL: create" >&2
    exit 1
}
"$TERM" run "$vid" >/dev/null 2>&1 # 暂停在第一行
kvcli set "/vthread/$vid/[1]/a" "string:zz" >/dev/null || {
    cleanup
    echo "FAIL: tamper slot" >&2
    exit 1
}
kvcli set "/vthread/$vid/‥status" "string:running" >/dev/null || {
    cleanup
    echo "FAIL: resume status" >&2
    exit 1
}
out=$("$TERM" run "$vid" 2>&1)
rc=$?
cleanup
if [ "$rc" = 0 ]; then
    echo "FAIL: typed violation not detected" >&2
    exit 1
fi
printf '%s' "$out" | grep -q "typed int64·add" || {
    echo "FAIL: wrong diagnostic: $out" >&2
    exit 1
}
echo "ok: typed violation detected"
