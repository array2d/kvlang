#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
M1=${1:-"$ROOT/build/runtimec2/m1_smoke"}
KVLANG=${2:-${KVLANG_BIN:-"$ROOT/bin/byteseek"}}
LIBKVSPACE="$ROOT/lib/libkvspace.so.1"
LIBRUNTIMEC2="$ROOT/lib/libkvlang_runtimec2.so"

for path in "$M1" "$KVLANG" "$LIBKVSPACE" "$LIBRUNTIMEC2"; do
    if [[ ! -e "$path" ]]; then
        echo "FAIL: missing test dependency: $path" >&2
        exit 1
    fi
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
store="$tmp/ops.shm"
dsn="shm://$store"
source_file="$tmp/ops.kv"

cat >"$source_file" <<'KV'
struct Box { x:int64 = 0 }

rwfunc direct_ops() -> () {
    string·concat("/tmp/kv583/direct", 42) -> _
    string·len("/tmp/kv583/direct") -> v
    print(v)
}

rwfunc member_ops() -> () {
    sm:[]char/utf32·int64 = {}
    sk = "a"
    string·slice(sm, sk, 40) -> _
    string·slice(sm, sk, 41) -> _
    string·concat(sm, sk) -> sv
    print(sv)
    im:int64·int64 = {}
    ik = 7
    string·slice(im, ik, 77) -> _
    string·concat(im, ik) -> iv
    print(iv)
    bm:bool·int64 = {}
    bk = true
    string·slice(bm, bk, 88) -> _
    string·concat(bm, bk) -> bv
    print(bv)
    fm:float64·int64 = {}
    fk = 3.5
    string·slice(fm, fk, 99) -> _
    string·concat(fm, fk) -> fv
    print(fv)
}

rwfunc ptr_ops() -> () {
    box:Box = {x=1}
    p = &box
    k = "x"
    string·slice(p, k, 9) -> _
    string·concat(p, k) -> v
    print(v)
}

rwfunc missing_ops() -> () {
    m:[]char/utf32·int64 = {}
    k = "absent"
    string·concat(m, k) -> v
    v == None -> missing
    print(missing)
}

rwfunc delete_ops() -> () {
    string·concat("/tmp/kv583/deleted", 5) -> _
    string·concat("/tmp/kv583/deleted", None) -> _
    string·len("/tmp/kv583/deleted") -> v
    v == None -> deleted
    print(deleted)
}

rwfunc direct_variable_path() -> () {
    string·slice("x/tmp/kv583/direct-variable", 1, 27) -> p
    string·concat(p, 42) -> _
    string·len(p) -> v
    print(v)
}

rwfunc char_base_ops() -> () {
    string·slice("x/tmp/kv583/char-base", 1, 21) -> p
    k = "x"
    string·slice(p, k, 73) -> _
    string·concat(p, k) -> v
    print(v)
}

rwfunc cache_int() -> () {
    /tmp/kv583/cache_int = 7
    /tmp/kv583/cache_int + 0 -> before
    string·concat("/tmp/kv583/cache_int", 9) -> _
    /tmp/kv583/cache_int + 0 -> after
    print(before)
    print(after)
}

rwfunc cache_bool() -> () {
    bool(/tmp/kv583/cache_bool) -> before
    string·concat("/tmp/kv583/cache_bool", 9) -> _
    /tmp/kv583/cache_bool + 0 -> after
    print(before)
    print(after)
}

rwfunc cache_scalar_view() -> () {
    /tmp/kv583/cache_float = 1.25
    /tmp/kv583/cache_float + 0.0 -> before
    string·concat("/tmp/kv583/cache_float", 2.5) -> _
    /tmp/kv583/cache_float + 0.0 -> after
    print(before)
    print(after)
}

rwfunc cache_delete() -> () {
    /tmp/kv583/cache_delete = 5
    /tmp/kv583/cache_delete + 0 -> before
    string·concat("/tmp/kv583/cache_delete", None) -> _
    /tmp/kv583/cache_delete == None -> deleted
    print(before)
    print(deleted)
}

rwfunc cache_noslot() -> () {
    /tmp/kv583/cache_new == None -> missing
    string·concat("/tmp/kv583/cache_new", 17) -> _
    /tmp/kv583/cache_new + 0 -> after
    print(missing)
    print(after)
}

rwfunc cache_ptr_alias() -> () {
    box:Box = {x=1}
    p = &box
    p·x -> before
    k = "x"
    string·slice(p, k, 9) -> _
    box·x -> after
    print(before)
    print(after)
}

rwfunc cache_array_layout() -> () {
    /tmp/kv583/cache_array:[2]int64 = [1, 2]
    compactarray·at(/tmp/kv583/cache_array, 0) -> before
    replacement:[2]int64 = [9, 8]
    string·concat("/tmp/kv583/cache_array", replacement) -> _
    compactarray·at(/tmp/kv583/cache_array, 0) -> after
    print(before)
    print(after)
}

rwfunc child_set_cross_frame() -> () {
    string·concat("/tmp/kv583/cross_frame_set", 9) -> _
}

rwfunc caller_cache_child_set() -> () {
    /tmp/kv583/cross_frame_set = 7
    /tmp/kv583/cross_frame_set + 0 -> before
    child_set_cross_frame()
    /tmp/kv583/cross_frame_set + 0 -> after
    print(before)
    print(after)
}

rwfunc child_delete_cross_frame() -> () {
    string·concat("/tmp/kv583/cross_frame_delete", None) -> _
}

rwfunc caller_cache_child_delete() -> () {
    /tmp/kv583/cross_frame_delete = 5
    /tmp/kv583/cross_frame_delete + 0 -> before
    child_delete_cross_frame()
    /tmp/kv583/cross_frame_delete == None -> deleted
    print(before)
    print(deleted)
}

rwfunc float_key_runtime_canonical() -> () {
    /tmp/kv583/float32:float32·int64 = {}
    k32 = float32(0.1)
    string·slice(/tmp/kv583/float32, k32, 32) -> _
    /tmp/kv583/float32·0.1 -> v32
    print(v32)
    e32_text = "1e-20"
    string·slice(/tmp/kv583/float32, e32_text, 33) -> _
    e32 = float32(1e-20)
    string·concat(/tmp/kv583/float32, e32) -> ve32
    print(ve32)
    z32 = float32(-0.0)
    string·slice(/tmp/kv583/float32, z32, 34) -> _
    string·concat(/tmp/kv583/float32, z32) -> vz32
    print(vz32)

    /tmp/kv583/float64:float64·int64 = {}
    k64_text = "0.1"
    string·slice(/tmp/kv583/float64, k64_text, 64) -> _
    k64 = 0.1
    string·concat(/tmp/kv583/float64, k64) -> v64
    print(v64)
    e64_text = "1e-20"
    string·slice(/tmp/kv583/float64, e64_text, 65) -> _
    e64 = 1e-20
    string·concat(/tmp/kv583/float64, e64) -> ve64
    print(ve64)
    z64 = -0.0
    string·slice(/tmp/kv583/float64, z64, 66) -> _
    string·concat(/tmp/kv583/float64, z64) -> vz64
    print(vz64)
}

rwfunc invalid_nan_key() -> () {
    m:float64·int64 = {}
    zero = 0.0
    k = zero ÷ zero
    string·concat(m, k) -> v
}

rwfunc invalid_inf_key() -> () {
    m:float64·int64 = {}
    one = 1.0
    zero = 0.0
    k = one ÷ zero
    string·concat(m, k) -> v
}

rwfunc invalid_direct_scalar_get() -> () {
    p = 7
    string·len(p) -> v
}

rwfunc invalid_direct_scalar_set() -> () {
    p = 7
    string·concat(p, 9) -> _
}

rwfunc invalid_direct_relative() -> () {
    p = "kv583-relative"
    string·len(p) -> v
}

rwfunc invalid_direct_bool() -> () {
    p = true
    bool(p) -> v
}

rwfunc invalid_direct_container() -> () {
    p:[]char/utf32·int64 = {}
    string·len(p) -> v
}

rwfunc invalid_member_relative() -> () {
    p = "kv583-relative"
    k = "x"
    string·concat(p, k) -> v
}

rwfunc invalid_scalar() -> () {
    b = 7
    k = "x"
    string·concat(b, k) -> v
}

rwfunc invalid_compact() -> () {
    b:[2]int64 = [1, 2]
    k = 0
    string·concat(b, k) -> v
}

rwfunc invalid_key() -> () {
    b:[]char/utf32·int64 = {}
    k:[2]int64 = [1, 2]
    string·concat(b, k) -> v
}

rwfunc missing_memhead() -> () {
    None -> b
    k = "x"
    string·slice(b, k, 1) -> _
}

rwfunc watch_stop() -> () {
    x = 123
    p = "/tmp/watch"
    k = "event"
    string·find(p, k) -> _
}
KV

set +e
env KVSPACE="$dsn" "$KVLANG" "$source_file" >"$tmp/layout.out" 2>"$tmp/layout.err"
layout_rc=$?
set -e
if [[ $layout_rc -ne 0 ]] && ! grep -q 'bootstrap test' "$tmp/layout.err"; then
    cat "$tmp/layout.err" >&2
    echo "FAIL: fixture layout failed unexpectedly" >&2
    exit 1
fi

LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
python3 - "$ROOT" "$dsn" "$M1" <<'PY'
import ctypes
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
dsn = sys.argv[2]
m1 = sys.argv[3]
lib = ctypes.CDLL(str(root / "lib/libkvspace.so.1"))
libc = ctypes.CDLL(None)
libc.free.argtypes = [ctypes.c_void_p]

lib.kvspaceConnect.argtypes = [ctypes.c_char_p]
lib.kvspaceConnect.restype = ctypes.c_void_p
lib.kvspaceClose.argtypes = [ctypes.c_void_p]
lib.kvspaceGet.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_uint32),
]
lib.kvspaceGet.restype = ctypes.c_int
lib.kvspaceSetValue.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_uint32,
    ctypes.c_char_p,
    ctypes.c_uint32,
]
lib.kvspaceSetValue.restype = ctypes.c_int
lib.kvspaceListLen.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_int32),
]
lib.kvspaceListLen.restype = ctypes.c_int
lib.kvspaceListAt.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int32,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32),
]
lib.kvspaceListAt.restype = ctypes.c_int
lib.kvspaceXvalueHeadNewCode.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_uint64,
    ctypes.c_uint64,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_uint64),
]
lib.kvspaceXvalueHeadNewCode.restype = ctypes.c_int
lib.kvspaceXvalueHeadNewScalar.argtypes = [
    ctypes.c_uint8,
    ctypes.POINTER(ctypes.c_uint8),
    ctypes.c_uint32,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_uint64),
]
lib.kvspaceXvalueHeadNewScalar.restype = ctypes.c_int


def get_raw(handle, key):
    data = ctypes.POINTER(ctypes.c_uint8)()
    length = ctypes.c_uint32()
    rc = lib.kvspaceGet(
        handle, key.encode(), 0, ctypes.byref(data), ctypes.byref(length)
    )
    if rc != 0 or not data:
        return None
    return ctypes.string_at(data, length.value)


def body(raw):
    length = int.from_bytes(raw[:6], "little")
    return raw[8 : 8 + length]


def langtype(raw):
    body_len = int.from_bytes(raw[:6], "little")
    offset = 8 + body_len
    attr_len = int.from_bytes(raw[offset : offset + 2], "little")
    pos = offset + 2
    end = offset + attr_len
    while pos + 3 <= end:
        tag = raw[pos]
        size = int.from_bytes(raw[pos + 1 : pos + 3], "little")
        pos += 3
        if pos + size > end:
            break
        if tag == 1:
            return raw[pos : pos + size].decode()
        pos += size
    return ""


def set_bool(handle, key, value):
    raw = (ctypes.c_uint8 * 1)(1 if value else 0)
    wire = ctypes.POINTER(ctypes.c_uint8)()
    wire_len = ctypes.c_uint64()
    rc = lib.kvspaceXvalueHeadNewScalar(
        0, raw, 1, b"bool", ctypes.byref(wire), ctypes.byref(wire_len)
    )
    if rc != 0 or not wire:
        raise AssertionError(f"cannot encode bool for {key}")
    try:
        error = ctypes.create_string_buffer(256)
        rc = lib.kvspaceSetValue(
            handle, key.encode(), wire, wire_len.value, error, len(error)
        )
        if rc != 0:
            raise AssertionError(f"cannot write {key}: {error.value.decode()}")
    finally:
        libc.free(wire)


def set_rwir(handle, key, old_body, opcode):
    encoded = old_body[:5] + opcode.encode()
    source = (ctypes.c_uint8 * len(encoded)).from_buffer_copy(encoded)
    wire = ctypes.POINTER(ctypes.c_uint8)()
    wire_len = ctypes.c_uint64()
    rc = lib.kvspaceXvalueHeadNewCode(
        source,
        len(encoded),
        len(encoded),
        b"rwir",
        ctypes.byref(wire),
        ctypes.byref(wire_len),
    )
    if rc != 0 or not wire:
        raise AssertionError(f"cannot encode {opcode}")
    try:
        error = ctypes.create_string_buffer(256)
        rc = lib.kvspaceSetValue(
            handle, key.encode(), wire, wire_len.value, error, len(error)
        )
        if rc != 0:
            raise AssertionError(f"cannot write {key}: {error.value.decode()}")
    finally:
        libc.free(wire)


patch_plan = {
    "direct_ops": {"string·concat": ["kvspace·set"], "string·len": ["kvspace·get"]},
    "direct_variable_path": {
        "string·concat": ["kvspace·set"],
        "string·len": ["kvspace·get"],
    },
    "char_base_ops": {
        "string·slice": ["string·slice", "kvspace·set"],
        "string·concat": ["kvspace·get"],
    },
    "member_ops": {
        "string·slice": ["kvspace·set"] * 5,
        "string·concat": ["kvspace·get"] * 4,
    },
    "ptr_ops": {"string·slice": ["kvspace·set"], "string·concat": ["kvspace·get"]},
    "missing_ops": {"string·concat": ["kvspace·get"]},
    "delete_ops": {
        "string·concat": ["kvspace·set", "kvspace·set"],
        "string·len": ["kvspace·get"],
    },
    "cache_int": {"string·concat": ["kvspace·set"]},
    "cache_bool": {"string·concat": ["kvspace·set"]},
    "cache_scalar_view": {"string·concat": ["kvspace·set"]},
    "cache_delete": {"string·concat": ["kvspace·set"]},
    "cache_noslot": {"string·concat": ["kvspace·set"]},
    "cache_ptr_alias": {"string·slice": ["kvspace·set"]},
    "cache_array_layout": {"string·concat": ["kvspace·set"]},
    "child_set_cross_frame": {"string·concat": ["kvspace·set"]},
    "child_delete_cross_frame": {"string·concat": ["kvspace·set"]},
    "float_key_runtime_canonical": {
        "string·slice": ["kvspace·set"] * 6,
        "string·concat": ["kvspace·get"] * 5,
    },
    "invalid_nan_key": {"string·concat": ["kvspace·get"]},
    "invalid_inf_key": {"string·concat": ["kvspace·get"]},
    "invalid_direct_scalar_get": {"string·len": ["kvspace·get"]},
    "invalid_direct_scalar_set": {"string·concat": ["kvspace·set"]},
    "invalid_direct_relative": {"string·len": ["kvspace·get"]},
    "invalid_direct_bool": {"bool": ["kvspace·get"]},
    "invalid_direct_container": {"string·len": ["kvspace·get"]},
    "invalid_member_relative": {"string·concat": ["kvspace·get"]},
    "invalid_scalar": {"string·concat": ["kvspace·get"]},
    "invalid_compact": {"string·concat": ["kvspace·get"]},
    "invalid_key": {"string·concat": ["kvspace·get"]},
    "missing_memhead": {"string·slice": ["kvspace·set"]},
    "watch_stop": {"string·find": ["kvspace·watch"]},
}

handle = lib.kvspaceConnect(dsn.encode())
if not handle:
    raise SystemExit("FAIL: kvspaceConnect")
try:
    set_bool(handle, "/tmp/kv583/cache_bool", True)
    for function, expected_ops in patch_plan.items():
        seen = {opcode: 0 for opcode in expected_ops}
        for s0 in range(1, 128):
            key = f"/lib/{function}/[{s0},0]"
            raw = get_raw(handle, key)
            if raw is None:
                continue
            instruction = body(raw)
            if len(instruction) < 5:
                continue
            opcode = instruction[5:].decode()
            if opcode not in expected_ops:
                continue
            index = seen[opcode]
            replacements = expected_ops[opcode]
            if index >= len(replacements):
                raise AssertionError(f"unexpected extra {function}:{opcode}")
            set_rwir(handle, key, instruction, replacements[index])
            seen[opcode] += 1
        for opcode, replacements in expected_ops.items():
            if seen[opcode] != len(replacements):
                raise AssertionError(
                    f"{function}: expected {len(replacements)} {opcode}, found {seen[opcode]}"
                )
finally:
    lib.kvspaceClose(handle)


def run_ok(function, expected_lines):
    result = subprocess.run(
        [m1, dsn, function], text=True, capture_output=True, check=False
    )
    if result.returncode != 0 or result.stdout.splitlines() != expected_lines:
        raise AssertionError(
            f"{function}: rc={result.returncode}, stdout={result.stdout!r}, "
            f"stderr={result.stderr!r}"
        )


def run_error(function, expected_error):
    result = subprocess.run(
        [m1, dsn, function], text=True, capture_output=True, check=False
    )
    if result.returncode == 0 or expected_error not in result.stderr:
        raise AssertionError(
            f"{function}: rc={result.returncode}, stderr={result.stderr!r}"
        )


run_ok("direct_ops", ["42"])
run_ok("direct_variable_path", ["42"])
run_ok("char_base_ops", ["73"])
run_ok("member_ops", ["41", "77", "88", "99"])
run_ok("ptr_ops", ["9"])
run_ok("missing_ops", ["true"])
run_ok("delete_ops", ["true"])
run_ok("cache_int", ["7", "9"])
run_ok("cache_bool", ["true", "9"])
run_ok("cache_scalar_view", ["1.25", "2.5"])
run_ok("cache_delete", ["5", "true"])
run_ok("cache_noslot", ["true", "17"])
run_ok("cache_ptr_alias", ["1", "9"])
run_ok("cache_array_layout", ["1", "9"])
run_ok("caller_cache_child_set", ["7", "9"])
run_ok("caller_cache_child_delete", ["5", "true"])
run_ok("float_key_runtime_canonical", ["32", "33", "34", "64", "65", "66"])
run_error("invalid_nan_key", "floating member key must be finite")
run_error("invalid_inf_key", "floating member key must be finite")
run_error("invalid_direct_scalar_get", "requires an absolute path")
run_error("invalid_direct_scalar_set", "requires an absolute path")
run_error("invalid_direct_relative", "requires an absolute path")
run_error("invalid_direct_bool", "requires an absolute path")
run_error("invalid_direct_container", "requires an absolute path")
run_error("invalid_member_relative", "member base must be an absolute path string")
run_error("invalid_scalar", "requires a struct/map container or path string")
run_error("invalid_compact", "requires a struct/map container or path string")
run_error("invalid_key", "member key must be a scalar or string")
run_error("missing_memhead", "TypeError: memhead b does not exist")

handle = lib.kvspaceConnect(dsn.encode())
if not handle:
    raise AssertionError("cannot reconnect for observations")
try:
    direct = get_raw(handle, "/tmp/kv583/direct")
    if direct is None or body(direct)[:8] != (42).to_bytes(8, "little", signed=True):
        raise AssertionError("direct path set did not preserve int64 value")
    if langtype(direct) != "int64":
        raise AssertionError("direct path set did not preserve int64 langtype")
    if get_raw(handle, "/tmp/kv583/deleted") is not None:
        raise AssertionError("set(None) left an explicit key")
    for key, expected in (
        ("/tmp/kv583/float32·0.1", 32),
        ("/tmp/kv583/float32·1e-20", 33),
        ("/tmp/kv583/float32·-0.0", 34),
        ("/tmp/kv583/float64·0.1", 64),
        ("/tmp/kv583/float64·1e-20", 65),
        ("/tmp/kv583/float64·-0.0", 66),
    ):
        raw = get_raw(handle, key)
        if raw is None or body(raw)[:8] != expected.to_bytes(8, "little", signed=True):
            raise AssertionError(f"runtime-formatted raw key is not canonical: {key}")

    count = ctypes.c_int32()
    if lib.kvspaceListLen(handle, b"/tmp/kv583/", 0, 0, ctypes.byref(count)) != 0:
        raise AssertionError("kvspaceListLen failed")
    names = set()
    for index in range(count.value):
        name = (ctypes.c_uint8 * 256)()
        name_len = ctypes.c_uint32()
        if lib.kvspaceListAt(
            handle,
            b"/tmp/kv583/",
            0,
            0,
            index,
            name,
            len(name),
            ctypes.byref(name_len),
        ) != 0:
            raise AssertionError("kvspaceListAt failed")
        names.add(bytes(name[: name_len.value]).decode())
    if "direct" not in names or "deleted" in names:
        raise AssertionError(f"unexpected /tmp/kv583 enumeration: {sorted(names)}")
finally:
    lib.kvspaceClose(handle)

runtime = ctypes.CDLL(str(root / "lib/libkvlang_runtimec2.so"))
runtime.kvlangRuntimeConnect.argtypes = [ctypes.c_char_p]
runtime.kvlangRuntimeConnect.restype = ctypes.c_void_p
runtime.kvlangRuntimeDisconnect.argtypes = [ctypes.c_void_p]
runtime.kvlangRuntimeKvspaceHandle.argtypes = [ctypes.c_void_p]
runtime.kvlangRuntimeKvspaceHandle.restype = ctypes.c_void_p
runtime.kvlangRuntimeBootstrap.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.c_int,
]
runtime.kvlangRuntimeBootstrap.restype = ctypes.c_void_p
runtime.kvlangRuntimeExecuteVthread.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_void_p),
]
runtime.kvlangRuntimeExecuteVthread.restype = ctypes.c_int

rt = runtime.kvlangRuntimeConnect(dsn.encode())
if not rt:
    raise AssertionError("runtime connect failed")
try:
    vid_ptr = runtime.kvlangRuntimeBootstrap(rt, b"watch_stop", None, 0)
    if not vid_ptr:
        raise AssertionError("watch bootstrap failed")
    try:
        vid = ctypes.string_at(vid_ptr).decode()
    finally:
        libc.free(vid_ptr)

    stop_ptr = ctypes.c_void_p()
    rc = runtime.kvlangRuntimeExecuteVthread(rt, vid.encode(), ctypes.byref(stop_ptr))
    try:
        stop_pc = ctypes.string_at(stop_ptr.value).decode() if stop_ptr.value else ""
    finally:
        if stop_ptr.value:
            libc.free(stop_ptr.value)
    expected_pc = f"/vthread/{vid}/[1]/[4,0]"
    if rc != 1 or stop_pc != expected_pc:
        raise AssertionError(f"watch handoff: rc={rc}, pc={stop_pc!r}, expected={expected_pc!r}")

    kv_handle = runtime.kvlangRuntimeKvspaceHandle(rt)
    flushed = get_raw(kv_handle, f"/vthread/{vid}/[1]/x")
    if flushed is None or body(flushed)[:8] != (123).to_bytes(8, "little", signed=True):
        raise AssertionError("watch handoff did not flush frame-local x")
    if langtype(flushed) != "int64":
        raise AssertionError("watch handoff flush lost x langtype")
finally:
    runtime.kvlangRuntimeDisconnect(rt)

print("OK kvspace path safety, cache invalidation, float keys, get/set semantics, Ptr, and watch Stop/flush")
PY
