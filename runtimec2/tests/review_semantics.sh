#!/usr/bin/env bash
# runtimec2 semantics and bounds regression suite (aligned with C runtime and spec).
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${1:-$ROOT/bin/kvlanglayout}"
BYTESEEK="${2:-$ROOT/bin/byteseek}"

for f in "$LAYOUT" "$BYTESEEK" "$ROOT/lib/libkvlang_runtimec2.so"; do
    if [[ ! -e "$f" ]]; then
        echo "SKIP: missing $f"
        exit 77
    fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
seq=0

expect_err() {
    local label="$1" want="$2" src="$3"
    seq=$((seq + 1))
    local kv="$tmp/case_${seq}.kv" dsn="shm://$tmp/s_${seq}.shm"
    printf '%s\n' "$src" >"$kv"
    KVSPACE="$dsn" "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1 || {
        echo "FAIL [$label]: boot"
        exit 1
    }
    "$LAYOUT" "$kv" "$dsn" >"$tmp/lay_${seq}.log" 2>&1 || {
        echo "FAIL [$label]: layout"
        cat "$tmp/lay_${seq}.log"
        exit 1
    }
    KVSPACE="$dsn" timeout 30 "$BYTESEEK" test >"$tmp/out_${seq}" 2>"$tmp/err_${seq}"
    local rc=$?
    if [[ $rc -eq 0 ]]; then
        echo "FAIL [$label]: expected non-zero exit, got 0 (stdout=$(cat "$tmp/out_${seq}"))"
        exit 1
    fi
    if ! grep -qF -- "$want" "$tmp/err_${seq}"; then
        echo "FAIL [$label]: stderr missing '$want'"
        cat "$tmp/err_${seq}"
        exit 1
    fi
    echo "OK   [$label]"
}

expect_out() {
    local label="$1" want="$2" src="$3"
    seq=$((seq + 1))
    local kv="$tmp/case_${seq}.kv" dsn="shm://$tmp/s_${seq}.shm"
    printf '%s\n' "$src" >"$kv"
    KVSPACE="$dsn" "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1 || {
        echo "FAIL [$label]: boot"
        exit 1
    }
    "$LAYOUT" "$kv" "$dsn" >"$tmp/lay_${seq}.log" 2>&1 || {
        echo "FAIL [$label]: layout"
        cat "$tmp/lay_${seq}.log"
        exit 1
    }
    KVSPACE="$dsn" timeout 30 "$BYTESEEK" test >"$tmp/out_${seq}" 2>"$tmp/err_${seq}"
    local rc=$?
    if [[ $rc -ne 0 ]]; then
        echo "FAIL [$label]: exit $rc"
        cat "$tmp/err_${seq}"
        exit 1
    fi
    if [[ "$(cat "$tmp/out_${seq}")" != "$want" ]]; then
        echo "FAIL [$label]: stdout mismatch: got '$(cat "$tmp/out_${seq}")', want '$want'"
        exit 1
    fi
    echo "OK   [$label]"
}

# 1. `compactarray·at` / `set` negative and per-dimension bounds checks
expect_err "compact_at_neg" "IndexError" '
rwfunc test() -> () {
    a:[]int64 = [10, 20, 30]
    i = -1
    println(a[i])
}'

expect_err "compact_set_neg" "IndexError" '
rwfunc test() -> () {
    a:[]int64 = [10, 20, 30]
    i = -1
    a[i] = 99
    println(a[0])
}'

expect_err "compact_2d_col_oob_at" "IndexError" '
rwfunc test() -> () {
    raw:[]int64 = [1, 2, 3, 4, 5, 6]
    m = compactarray·newreshape(raw, 2, 3)
    c = 5
    println(m[0, c])
}'

expect_err "compact_2d_col_neg_at" "IndexError" '
rwfunc test() -> () {
    raw:[]int64 = [1, 2, 3, 4, 5, 6]
    m = compactarray·newreshape(raw, 2, 3)
    c = -1
    println(m[1, c])
}'

expect_err "compact_2d_col_oob_set" "IndexError" '
rwfunc test() -> () {
    raw:[]int64 = [1, 2, 3, 4, 5, 6]
    m = compactarray·newreshape(raw, 2, 3)
    c = 3
    m[0, c] = 99
    println(m[1, 0])
}'

expect_out "compact_2d_valid" "6" '
rwfunc test() -> () {
    raw:[]int64 = [1, 2, 3, 4, 5, 6]
    m = compactarray·newreshape(raw, 2, 3)
    println(m[1, 2])
}'

# 2. `vthread·setstatus("paused")` halts immediately and resumes cleanly
expect_out "debugger_halts_exact" "before" '
rwfunc test() -> () {
    println("before")
    vthread·setstatus("paused")
    println("after")
}'

{
    seq=$((seq + 1))
    kv="$tmp/case_${seq}.kv"
    dsn="shm://$tmp/s_${seq}.shm"
    cat >"$kv" <<'KV'
rwfunc f2(a:int64) -> (s:int64) {
    x = a + 10
    println("step1", x)
    vthread·setstatus("paused")
    y = x + 40
    println("step2", y)
    s = y
}

rwfunc f1(a:int64) -> (s:int64) {
    t = a × 2
    f2(t) -> r
    s = r
}

rwfunc worker() -> () {
    f1(10) -> ans
    println("done", ans)
}

rwfunc check_status(vid:[]char/utf8) -> () {
    st_key = string·concat(string·concat("/vthread/", vid), "/‥status")
    println(kvspace·get(st_key))
}
KV
    KVSPACE="$dsn" "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1
    "$LAYOUT" "$kv" "$dsn" >/dev/null 2>&1 || { echo "FAIL [debugger_nested_resume]: layout"; exit 1; }
    vid="$(KVSPACE="$dsn" "$BYTESEEK" create worker)"
    out1="$(KVSPACE="$dsn" "$BYTESEEK" run "$vid")"
    if [[ "$out1" != "step1 30" ]]; then
        echo "FAIL [debugger_nested_resume]: run1 got '$out1', want 'step1 30'"
        exit 1
    fi
    st1="$(python3 - "$ROOT/lib/libkvspace.so.1" "$dsn" "$vid" <<'PY'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
lib.kvspaceConnect.argtypes = [ctypes.c_char_p]
lib.kvspaceConnect.restype = ctypes.c_void_p
lib.kvspaceGet.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint32)]
lib.kvspaceXvalueHeadNewSlack.argtypes = [ctypes.c_char_p, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_char_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint64)]
lib.kvspaceSetValue.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
lib.kvspaceClose.argtypes = [ctypes.c_void_p]
h = lib.kvspaceConnect(sys.argv[2].encode())
assert h
err = ctypes.create_string_buffer(256)
st_key = f"/vthread/{sys.argv[3]}/\u2025status".encode()
d = ctypes.POINTER(ctypes.c_uint8)()
n = ctypes.c_uint32()
assert lib.kvspaceGet(h, st_key, 0, ctypes.byref(d), ctypes.byref(n)) == 0
blen = int.from_bytes(bytes(d[:6]), "little")
print(bytes(d[8:8+blen]).decode())
out = ctypes.POINTER(ctypes.c_uint8)()
olen = ctypes.c_uint64()
assert lib.kvspaceXvalueHeadNewSlack(b"running", 7, 16, b"[7]char/utf8", ctypes.byref(out), ctypes.byref(olen)) == 0
assert lib.kvspaceSetValue(h, st_key, out, olen.value, err, 256) == 0
lib.kvspaceClose(h)
PY
)"
    if [[ "$st1" != "paused" ]]; then
        echo "FAIL [debugger_nested_resume]: status got '$st1', want 'paused'"
        exit 1
    fi
    out2="$(KVSPACE="$dsn" "$BYTESEEK" run "$vid")"
    want2="$(printf 'step2 70\ndone 70')"
    if [[ "$out2" != "$want2" ]]; then
        echo "FAIL [debugger_nested_resume]: run2 got '$out2', want '$want2'"
        exit 1
    fi
    echo "OK   [debugger_nested_resume]"
}

# 3. `rwirext` dynamic member write slots (`json·from("...") -> m·*k`) and param keys
expect_out "rwirext_dyn_member_write" "42 99" '
rwfunc load_into(k:[]char/utf8, raw:[]char/utf8) -> (m:*[]char/utf8·int64) {
    json·from(raw) -> m·*k
}
rwfunc test() -> () {
    m:[]char/utf8·int64 = {}
    k = "alpha"
    json·from("42") -> m·*k
    load_into("beta", "99") -> m
    println(m·alpha, m·beta)
}'

expect_err "member_key_inf_read" "TypeError: floating member key must be finite" \
    "$(cat "$ROOT/runtime/tests/member_key_inf_read.kv")"
expect_err "member_key_nan_write" "TypeError: floating member key must be finite" \
    "$(cat "$ROOT/runtime/tests/member_key_nan_write.kv")"
expect_out "member_key_text_parity" "$(cat "$ROOT/runtime/tests/member_key_text.want")" \
    "$(cat "$ROOT/runtime/tests/member_key_text.kv")"

# 4. `None` predicate, scalar coercion/None rejection, mixed int promotion, unsigned `abs`, `compactarray·copy`
expect_out "none_predicate" "$(cat "$ROOT/runtime/tests/none_predicate.want")" \
    "$(cat "$ROOT/runtime/tests/none_predicate.kv")"
expect_err "none_param_int64" "TypeError: cannot write None to int64 slot" \
    "$(cat "$ROOT/runtime/tests/none_param_int64.kv")"
expect_err "none_local_float32" "TypeError: cannot write None to float32 slot" \
    "$(cat "$ROOT/runtime/tests/none_local_float32.kv")"
expect_err "none_struct_field" "TypeError: cannot write None to uint8 slot" \
    "$(cat "$ROOT/runtime/tests/none_struct_field.kv")"
expect_err "none_write_param" "TypeError: cannot write None to int64 slot" \
    "$(cat "$ROOT/runtime/tests/none_write_param.kv")"
expect_out "unsigned_abs" "$(cat "$ROOT/runtime/tests/unsigned_abs.want")" \
    "$(cat "$ROOT/runtime/tests/unsigned_abs.kv")"
expect_out "mixed_int_promotion" "$(cat "$ROOT/runtime/tests/mixed_int_promotion.want")" \
    "$(cat "$ROOT/runtime/tests/mixed_int_promotion.kv")"
expect_err "coercion_err" "TypeError: cannot assign char/" \
    "$(cat "$ROOT/runtime/tests/coercion_err.kv")"
expect_out "typed_scalar_coercion" "$(printf 'i8 -56\nu8 200\nf32 0.10000000149011612\nf64 0.1\nmap -56\nfield -56\nptr -56\nptr_i64 99\ndynkv 300')" '
struct S { v:int8 = 0 }
rwfunc test() -> () {
    v = 200
    x:int8 = v
    println("i8", x)
    y:uint8 = v
    println("u8", y)
    f:float32 = 0.1
    println("f32", f)
    g:float64 = 0.1
    println("f64", g)
    m:[]char/utf8·int8 = {x=v}
    println("map", m·x)
    s:S = S{v=v}
    println("field", s·v)
    p:*int8 = &x
    *p = v
    println("ptr", x)
    q_val:int64 = 10
    qp:*int64 = &q_val
    *qp = 99
    println("ptr_i64", q_val)
    w = 300
    kvspace·set("/tmp/coerce_c2/k", w)
    z = kvspace·get("/tmp/coerce_c2/k")
    println("dynkv", z)
}'
expect_err "compactarray_copy_scalar" "TypeError: compactarray·copy expects a compact array" '
rwfunc test() -> () {
    x = 42
    b = compactarray·copy(x)
}'
expect_err "compactarray_copy_scatter" "TypeError: compactarray·copy expects a compact array" '
rwfunc test() -> () {
    s = scatterarray·new("int64", 1, 2)
    b = compactarray·copy(s)
}'

# 5. Float abs(-0.0) -> +0.0 parity, sqrt(float32), container clear_members, *T None arg, and fs:// coherency
expect_out "float_abs_neg_zero_and_sqrt_f32" "$(printf '0.0 0.0\n2.0')" '
rwfunc test() -> () {
    z:float64 = -0.0
    zf:float32 = -0.0
    println(abs(z), abs(zf))
    s:float32 = 4.0
    println(sqrt(s))
}'

expect_out_fs() {
    local label="$1" want="$2" src="$3"
    seq=$((seq + 1))
    local kv="$tmp/case_${seq}.kv" fsdir="$tmp/fs_${seq}"
    mkdir -p "$fsdir"
    local dsn="fs://$fsdir"
    printf '%s\n' "$src" >"$kv"
    KVSPACE="$dsn" "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1 || {
        echo "FAIL [$label]: boot (fs)"
        exit 1
    }
    "$LAYOUT" "$kv" "$dsn" >"$tmp/lay_${seq}.log" 2>&1 || {
        echo "FAIL [$label]: layout (fs)"
        cat "$tmp/lay_${seq}.log"
        exit 1
    }
    KVSPACE="$dsn" timeout 30 "$BYTESEEK" test >"$tmp/out_${seq}" 2>"$tmp/err_${seq}"
    local rc=$?
    if [[ $rc -ne 0 ]]; then
        echo "FAIL [$label]: exit $rc (fs)"
        cat "$tmp/err_${seq}"
        exit 1
    fi
    if [[ "$(cat "$tmp/out_${seq}")" != "$want" ]]; then
        echo "FAIL [$label]: stdout mismatch (fs): got '$(cat "$tmp/out_${seq}")', want '$want'"
        exit 1
    fi
    echo "OK   [$label]"
}

expect_out "container_reassign_and_ptr_none" "$(printf '1 3\n0\nprobe\nprobe\ntrue false')" '
rwfunc is_null(p:*int64) -> (r:bool) {
    println("probe")
    r = p == None
}
rwfunc test() -> () {
    m:[]char/utf8·int64 = {a=1, b=2}
    m:[]char/utf8·int64 = {a=3}
    println(kvspace·listlen(string·concat(&m, "·")), m·a)
    m:[]char/utf8·int64 = {}
    println(kvspace·listlen(string·concat(&m, "·")))
    x = 7
    b1 = is_null(None)
    b2 = is_null(&x)
    println(b1, b2)
}'

expect_out_fs "durable_fs_full_ops" "$(printf 'ptr 11 22 33\narr 10 99 30 2\nmeta [3]int64 24 int64 1 [1,3]int64 [3]uint64\nstr B\nmap 1 7')" '
rwfunc bump(p:*int64) -> () {
    lp:*int64 = p
    *lp = *lp + 11
}
rwfunc test() -> () {
    x = 11
    p:*int64 = &x
    a = *p
    x = 22
    b = *p
    bump(p)
    c = x
    println("ptr", a, b, c)

    arr:[]int64 = [10, 20, 30]
    arr[1] = 99
    cp = compactarray·copy(arr)
    sl = compactarray·newslice(cp, 1, 3)
    sc = scatterarray·newscatter(sl)
    println("arr", cp[0], cp[1], cp[2], scatterarray·len(sc))

    lt = compactarray·langtype(cp)
    bl = compactarray·bodylen(cp)
    compactarray·parselangtype(cp) -> (bk, nd)
    rs = compactarray·newreshape(cp, 1, 3)
    ri = compactarray·newreinterpret(cp, "[]uint64")
    println("meta", lt, bl, bk, nd, compactarray·langtype(rs), compactarray·langtype(ri))

    ch = string·char("ABC", 1)
    println("str", char/utf8(ch))

    m:[]char/utf8·int64 = {x=1, y=2}
    m:[]char/utf8·int64 = {x=7}
    println("map", kvspace·listlen(string·concat(&m, "·")), m·x)
}'
