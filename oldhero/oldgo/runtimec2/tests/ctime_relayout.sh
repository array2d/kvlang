#!/usr/bin/env bash
# Regression test: segment cache invalidation via <func>/‥ctime on live relayout
# across both shm:// and fs:// backends.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAYOUT="${1:-$ROOT/bin/kvlanglayout}"
BYTESEEK="${2:-$ROOT/bin/byteseek}"
TEST_DIR="$(cd "$(dirname "$0")" && pwd)"

for f in "$LAYOUT" "$BYTESEEK" "$ROOT/lib/libkvlang_runtimec2.so"; do
    if [[ ! -e "$f" ]]; then
        echo "SKIP: missing $f"
        exit 77
    fi
done

export LD_LIBRARY_PATH="$ROOT/lib:$ROOT/lib/kvspace${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export KVSPACE_BACKEND_PATH="$ROOT/lib/kvspace"

tmp="$(mktemp -d)"
worker_pid=""
cleanup() {
    if [[ -n "$worker_pid" ]]; then
        kill "$worker_pid" 2>/dev/null || true
        wait "$worker_pid" 2>/dev/null || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT

read_live_val() {
    local dsn="$1"
    python3 - "$ROOT/lib/libkvspace.so.1" "$dsn" <<'PY'
import ctypes, sys
lib = ctypes.CDLL(sys.argv[1])
lib.kvspaceConnect.argtypes = [ctypes.c_char_p]
lib.kvspaceConnect.restype = ctypes.c_void_p
lib.kvspaceGet.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
    ctypes.POINTER(ctypes.c_uint32),
]
lib.kvspaceClose.argtypes = [ctypes.c_void_p]
h = lib.kvspaceConnect(sys.argv[2].encode())
if not h:
    sys.exit(0)
d = ctypes.POINTER(ctypes.c_uint8)()
n = ctypes.c_uint32()
if lib.kvspaceGet(h, b"/live/val", 0, ctypes.byref(d), ctypes.byref(n)) == 0 and n.value >= 16:
    blen = int.from_bytes(bytes(d[:6]), "little")
    if blen == 8:
        print(int.from_bytes(bytes(d[8:16]), "little", signed=True))
lib.kvspaceClose(h)
PY
}

wait_for_val() {
    local dsn="$1" want="$2" label="$3"
    local val=""
    for _ in $(seq 1 200); do
        val="$(read_live_val "$dsn" 2>/dev/null || true)"
        if [[ "$val" == "$want" ]]; then
            echo "OK   [$label]"
            return 0
        fi
        sleep 0.05
    done
    echo "FAIL [$label]: timed out waiting for /live/val == $want (last='$val')"
    return 1
}

run_case() {
    local kind="$1" dsn="$2"
    KVSPACE="$dsn" "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1
    "$LAYOUT" "$TEST_DIR/ctime_relayout_a.kv" "$dsn" >/dev/null 2>&1

    KVSPACE="$dsn" "$BYTESEEK" worker >"$tmp/worker_${kind}.out" 2>"$tmp/worker_${kind}.err" &
    worker_pid=$!

    wait_for_val "$dsn" "1" "ctime_relayout_${kind}_initial"

    "$LAYOUT" "$TEST_DIR/ctime_relayout_b.kv" "$dsn" >/dev/null 2>&1

    wait_for_val "$dsn" "2" "ctime_relayout_${kind}_updated"

    kill "$worker_pid" 2>/dev/null || true
    wait "$worker_pid" 2>/dev/null || true
    worker_pid=""
}

# 1. Cross-process live worker relayout on both shm:// and fs://
run_case "shm" "shm://$tmp/ctime.shm"
mkdir -p "$tmp/ctime_fs"
run_case "fs" "fs://$tmp/ctime_fs"

# 2. Same-handle relayout on both shm:// and fs:// (verifying cached_segment
# invalidation via <func>/‥ctime on durable backends as well as shm://).
run_same_handle_case() {
    local kind="$1" dsn="$2"
    python3 - "$ROOT" "$dsn" "$kind" <<'PY'
import ctypes, os, sys
root, dsn, kind = sys.argv[1], sys.argv[2], sys.argv[3]
rt_lib = ctypes.CDLL(f"{root}/lib/libkvlang_runtimec2.so")
rt_lib.kvlangRuntimeConnect.argtypes = [ctypes.c_char_p]
rt_lib.kvlangRuntimeConnect.restype = ctypes.c_void_p
rt_lib.kvlangRuntimeDisconnect.argtypes = [ctypes.c_void_p]
rt_lib.kvlangRuntimeKvspaceHandle.argtypes = [ctypes.c_void_p]
rt_lib.kvlangRuntimeKvspaceHandle.restype = ctypes.c_void_p
rt_lib.kvlangRuntimeBootstrap.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int]
rt_lib.kvlangRuntimeBootstrap.restype = ctypes.c_void_p
rt_lib.kvlangRuntimeExecuteVthread.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
rt_lib.kvlangRuntimeExecuteVthread.restype = ctypes.c_int

kv_lib = ctypes.CDLL(f"{root}/lib/libkvspace.so.1")
kv_lib.kvspaceGet.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint32)]
kv_lib.kvspaceSetValue.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
kv_lib.kvspaceNewInt64.argtypes = [ctypes.c_int64, ctypes.c_char_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint32)]
libc = ctypes.CDLL(None)
libc.free.argtypes = [ctypes.c_void_p]

def get_i64(h, key):
    dp = ctypes.POINTER(ctypes.c_uint8)()
    n = ctypes.c_uint32()
    assert kv_lib.kvspaceGet(h, key, 0, ctypes.byref(dp), ctypes.byref(n)) == 0
    return int.from_bytes(bytes(dp[8:16]), "little", signed=True)

def set_i64(h, key, val):
    out = ctypes.POINTER(ctypes.c_uint8)()
    olen = ctypes.c_uint32()
    err = ctypes.create_string_buffer(256)
    assert kv_lib.kvspaceNewInt64(val, b"", ctypes.byref(out), ctypes.byref(olen)) == 0
    assert kv_lib.kvspaceSetValue(h, key, out, olen.value, err, 256) == 0
    libc.free(ctypes.cast(out, ctypes.c_void_p))

def run_entry(rt, name):
    vp = rt_lib.kvlangRuntimeBootstrap(rt, name, None, 0)
    assert vp
    vid = ctypes.string_at(vp)
    libc.free(vp)
    pc = ctypes.c_char_p()
    assert rt_lib.kvlangRuntimeExecuteVthread(rt, vid, ctypes.byref(pc)) == 0

rt = rt_lib.kvlangRuntimeConnect(dsn.encode())
assert rt
h = rt_lib.kvlangRuntimeKvspaceHandle(rt)

run_entry(rt, b"step_once")
v1 = get_i64(h, b"/live/val")
assert v1 == 1, f"expected v1 == 1, got {v1}"

# Mutate target literal [1,-1] from 1 -> 2 and bump /lib/target/‥ctime on the SAME handle
lit_key = b"/lib/target/[1,-1]"
dp = ctypes.POINTER(ctypes.c_uint8)()
n = ctypes.c_uint32()
assert kv_lib.kvspaceGet(h, lit_key, 1, ctypes.byref(dp), ctypes.byref(n)) == 0
raw = bytearray(bytes(dp[:n.value]))
raw[8:16] = (2).to_bytes(8, "little", signed=True)
buf = (ctypes.c_uint8 * len(raw)).from_buffer(raw)
err = ctypes.create_string_buffer(256)
assert kv_lib.kvspaceSetValue(h, lit_key, buf, len(raw), err, 256) == 0

dp2 = ctypes.POINTER(ctypes.c_uint8)()
n2 = ctypes.c_uint32()
assert kv_lib.kvspaceGet(h, b"/lib/target/\xe2\x80\xa5ctime", 1, ctypes.byref(dp2), ctypes.byref(n2)) == 0
old_ct = int.from_bytes(bytes(dp2[8:16]), "little", signed=True)
set_i64(h, b"/lib/target/\xe2\x80\xa5ctime", old_ct + 1)

run_entry(rt, b"step_once")
v2 = get_i64(h, b"/live/val")
rt_lib.kvlangRuntimeDisconnect(rt)
assert v2 == 2, f"expected v2 == 2 after ctime bump, got {v2}"
print(f"OK   [ctime_relayout_same_handle_{kind}]")
PY
}

setup_step_once() {
    local dsn="$1" kv="$2"
    cat >"$kv" <<'KV'
rwfunc target() -> (r:int64) {
    r = 1
}
rwfunc step_once() -> () {
    target() -> v
    kvspace·set("/live/val", v)
}
KV
    KVSPACE="$dsn" "$BYTESEEK" -c 'print("boot")' >/dev/null 2>&1
    "$LAYOUT" "$kv" "$dsn" >/dev/null 2>&1
}

setup_step_once "shm://$tmp/sh.shm" "$tmp/sh_shm.kv"
run_same_handle_case "shm" "shm://$tmp/sh.shm"

mkdir -p "$tmp/sh_fs"
setup_step_once "fs://$tmp/sh_fs" "$tmp/sh_fs.kv"
run_same_handle_case "fs" "fs://$tmp/sh_fs"
