#!/usr/bin/env python3
"""numpy rwirext —— 单文件引擎 + 编排器。

    python numpy.py <file.kv>

依序执行：
  1. Publish /‥isa control store via `byteseek isa publish`;
  2. Register numpy·* ops under /lib/numpy·* via kvlangDefRwir and serve vids/<vid> handoffs;
  3. Lay out .kv into kvspace via kvlanglayout (normalizing legacy numpy.foo syntax);
  4. Run main via byteseek, handing off numpy·* ops through /lib/<op>/vids/<vid>.

设计要点：
  · ndarray 不是新类型，就是 [dims]dtype——一维字面量由 kvlang 产生，更高的秩由 numpy
    算子（reshape/matmul/…）产出并按 [dims]dtype 存回 kvspace。
  - Uses kvspace v2 single-key wire format (8B head + trailing attr):
    reads via kvspaceGet + kvspaceGetHead across shm:// and fs://;
    writes via kvlangXvalueHeadNewTensor / kvlangXvalueHeadNewScalar.
  - Resolves opcode and operands via kvlangRwirextParams, reading ndarray slots by path first
    and falling back to kvlangRwirextResolveRead; resolves write slots via kvlangRwirextResolveWrite.
  - numpy·print writes directly to this process's stdout.
"""

import ast
import ctypes
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

# 本文件名为 numpy.py，须先把自身所在目录移出 sys.path，才能 import 到真正的 numpy 包。
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path = [p for p in sys.path if os.path.abspath(p or os.getcwd()) != _HERE]
import numpy as np  # noqa: E402

# Shared library lookup (env override -> repo lib/ -> system linker)
KVLANG = Path(__file__).resolve().parents[3]
BIN = KVLANG / "bin"
LIB = KVLANG / "lib"


def _find_lib(name, env, *rels):
    cands = ([os.environ[env]] if os.environ.get(env) else []) + [str(KVLANG / r) for r in rels]
    for c in cands:
        if os.path.exists(c):
            return ctypes.CDLL(c)
    return ctypes.CDLL(name)


if os.path.isdir(LIB / "kvspace") and "KVSPACE_BACKEND_PATH" not in os.environ:
    os.environ["KVSPACE_BACKEND_PATH"] = str(LIB / "kvspace")

_ks = _find_lib("libkvspace.so.1", "KVSPACE_LIB", "lib/libkvspace.so.1", "lib/libkvspace.so")
_rt = _find_lib("libkvlang_runtime.so", "KVLANG_RUNTIME_LIB",
                "lib/libkvlang_runtime.so", "lib/libkvlang_runtimec2.so")
_lay = _find_lib("libkvlanglayout.so", "KVLANG_LAYOUT_LIB",
                 "lib/libkvlanglayout.so", "layout/target/release/libkvlanglayout.so")


class kvspace_head_t(ctypes.Structure):
    _fields_ = [("headlen", ctypes.c_uint16), ("ref", ctypes.c_uint8),
                ("storetype", ctypes.c_uint8), ("body_len", ctypes.c_int32),
                ("ndim", ctypes.c_int32), ("dims", ctypes.c_int32 * 8),
                ("langtype", ctypes.c_uint8 * 256), ("langtype_len", ctypes.c_int32),
                ("body_offset", ctypes.c_int32), ("body_cap", ctypes.c_uint64)]


class kvlang_kindexpr_t(ctypes.Structure):
    _fields_ = [("ref", ctypes.c_int32), ("ndim", ctypes.c_int32),
                ("dims", ctypes.c_int32 * 8), ("array_len", ctypes.c_int32),
                ("kind", ctypes.c_uint8 * 64)]


def _bind():
    # kvspace v0.3.23+ ABI (8B head + trailing attr)
    _ks.kvspaceConnect.argtypes = [ctypes.c_char_p]; _ks.kvspaceConnect.restype = ctypes.c_void_p
    _ks.kvspaceClose.argtypes = [ctypes.c_void_p]
    _ks.kvspaceGet.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
                               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                               ctypes.POINTER(ctypes.c_uint32)]
    _ks.kvspaceGet.restype = ctypes.c_int
    _ks.kvspaceGetHead.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(kvspace_head_t)]
    _ks.kvspaceGetHead.restype = ctypes.c_int
    _ks.kvspaceSetValue.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                    ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                                    ctypes.c_char_p, ctypes.c_uint32]
    _ks.kvspaceSetValue.restype = ctypes.c_int
    _ks.kvspaceListLen.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_int32)]
    _ks.kvspaceListLen.restype = ctypes.c_int
    _ks.kvspaceListAt.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int32, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                                  ctypes.POINTER(ctypes.c_uint32)]
    _ks.kvspaceListAt.restype = ctypes.c_int
    _ks.kvspaceDel.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_uint32,
                               ctypes.c_char_p, ctypes.c_uint32]
    _ks.kvspaceDel.restype = ctypes.c_int
    _ks.kvlangXvalueNewChar.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_char_p,
                                   ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                   ctypes.POINTER(ctypes.c_uint32)]
    _ks.kvlangXvalueNewChar.restype = ctypes.c_int
    _ks.kvlangXvalueHeadNewScalar.argtypes = [ctypes.c_uint8, ctypes.POINTER(ctypes.c_uint8),
                                               ctypes.c_uint32, ctypes.c_char_p,
                                               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                               ctypes.POINTER(ctypes.c_uint64)]
    _ks.kvlangXvalueHeadNewScalar.restype = ctypes.c_int
    _ks.kvlangXvalueHeadNewTensor.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_uint32,
                                               ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint64,
                                               ctypes.c_char_p,
                                               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                               ctypes.POINTER(ctypes.c_uint64)]
    _ks.kvlangXvalueHeadNewTensor.restype = ctypes.c_int
    # rwirext ABI（kvspace 不提供的 runtime 语义）：句柄传扩展自连的 kvspace
    for fn in ("kvlangRwirextParams", "kvlangRwirextResolveRead", "kvlangRwirextResolveReadPath",
               "kvlangRwirextResolveWrite", "kvlangRwirextNextPc"):
        getattr(_rt, fn).restype = ctypes.c_void_p
    _rt.kvlangDefRwir.restype = ctypes.c_int
    _rt.kvlangDefRwir.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                  ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32,
                                  ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32]
    _rt.kvlangRwirextParams.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _rt.kvlangRwirextResolveRead.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.kvlangRwirextResolveReadPath.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.kvlangRwirextResolveWrite.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.kvlangRwirextNextPc.argtypes = [ctypes.c_char_p]
    _lay.kvlangLayoutCode.restype = ctypes.c_int
    _lay.kvlangLayoutCode.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                      ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
    _lay.kvlangKindexprParse.restype = ctypes.c_int
    _lay.kvlangKindexprParse.argtypes = [ctypes.c_char_p, ctypes.POINTER(kvlang_kindexpr_t)]


_bind()
_libc = ctypes.CDLL(None); _libc.free.argtypes = [ctypes.c_void_p]

_KIND_CTYPE = {"float64": ctypes.c_double, "float32": ctypes.c_float,
               "int64": ctypes.c_int64, "int32": ctypes.c_int32, "int16": ctypes.c_int16,
               "int8": ctypes.c_int8, "uint64": ctypes.c_uint64, "uint32": ctypes.c_uint32,
               "uint16": ctypes.c_uint16, "uint8": ctypes.c_uint8, "bool": ctypes.c_bool}
_NP_KIND = {np.float64: "float64", np.float32: "float32", np.int64: "int64", np.int32: "int32",
            np.int16: "int16", np.int8: "int8", np.uint64: "uint64", np.uint32: "uint32",
            np.uint16: "uint16", np.uint8: "uint8", np.bool_: "bool"}


def _kind_of(dt):
    for npdt, k in _NP_KIND.items():
        if np.dtype(dt) == np.dtype(npdt):
            return k
    raise ValueError(f"unsupported dtype: {dt}")


def _s(p):
    if not p:
        return ""
    v = ctypes.string_at(p).decode(); _libc.free(p); return v


def _fmt(a):
    a = np.asarray(a)
    if a.ndim == 0:
        v = a.item()
        return repr(float(v)) if isinstance(v, float) else repr(v)
    return "[" + ", ".join(_fmt(x) for x in a) + "]"


def _parse(s):
    try:
        return ast.literal_eval(s)
    except (ValueError, SyntaxError):
        return s


_shape = lambda x: tuple(int(v) for v in np.asarray(x).ravel())
_int = lambda x: int(np.asarray(x).ravel()[0])

# ── 五大类算子（nw 恒为 1；numpy.print 单列变参）─────────────────────────
_CREATION = {
    "zeros":    (1, lambda a: np.zeros(_shape(a[0]))),
    "ones":     (1, lambda a: np.ones(_shape(a[0]))),
    "full":     (2, lambda a: np.full(_shape(a[0]), a[1])),
    "eye":      (1, lambda a: np.eye(_int(a[0]))),
    "arange":   (1, lambda a: np.arange(_int(a[0]))),
    "linspace": (3, lambda a: np.linspace(float(a[0]), float(a[1]), _int(a[2]))),
    "rand":     (1, lambda a: np.random.rand(*_shape(a[0]))),
}
_ELEMENTWISE = {
    "add": (2, lambda a: np.add(a[0], a[1])), "sub": (2, lambda a: np.subtract(a[0], a[1])),
    "mul": (2, lambda a: np.multiply(a[0], a[1])), "div": (2, lambda a: np.divide(a[0], a[1])),
    "pow": (2, lambda a: np.power(a[0], a[1])), "mod": (2, lambda a: np.mod(a[0], a[1])),
    "maximum": (2, lambda a: np.maximum(a[0], a[1])), "minimum": (2, lambda a: np.minimum(a[0], a[1])),
    "neg": (1, lambda a: np.negative(a[0])), "abs": (1, lambda a: np.abs(a[0])),
    "sqrt": (1, lambda a: np.sqrt(a[0])), "exp": (1, lambda a: np.exp(a[0])),
    "log": (1, lambda a: np.log(a[0])), "sin": (1, lambda a: np.sin(a[0])),
    "cos": (1, lambda a: np.cos(a[0])),
}
_MATMUL = {
    "matmul": (2, lambda a: np.matmul(a[0], a[1])),
    "dot":    (2, lambda a: np.dot(a[0], a[1])),
}
_REDUCE = {
    "sum": (1, lambda a: np.sum(a[0])), "prod": (1, lambda a: np.prod(a[0])),
    "mean": (1, lambda a: np.mean(a[0])), "max": (1, lambda a: np.max(a[0])),
    "min": (1, lambda a: np.min(a[0])),
}
_MANIPULATION = {
    "reshape":     (2, lambda a: np.reshape(a[0], _shape(a[1]))),
    "transpose":   (1, lambda a: np.transpose(a[0])),
    "concatenate": (2, lambda a: np.concatenate([a[0], a[1]])),
    "stack":       (2, lambda a: np.stack([a[0], a[1]])),
    "ravel":       (1, lambda a: np.ravel(a[0])),
}
_CATEGORIES = {"creation": _CREATION, "elementwise": _ELEMENTWISE, "matmul": _MATMUL,
               "reduce": _REDUCE, "manipulation": _MANIPULATION}
OPS = {f"numpy·{n}": spec for cat in _CATEGORIES.values() for n, spec in cat.items()}
PRINT_OP = "numpy·print"
SHARED_VIDS = "/vthread/‥vids"
SHARED_VIDS_TYPE = "[]char/utf32·[]char/utf32"


class Engine:
    def __init__(self, dsn):
        self.kv = _ks.kvspaceConnect(dsn.encode())
        if not self.kv:
            raise RuntimeError(f"kvspaceConnect failed: {dsn}")

    def close(self):
        if self.kv:
            _ks.kvspaceClose(self.kv); self.kv = None

    def _set_wire(self, key, ptr, length):
        err = ctypes.create_string_buffer(256)
        if _ks.kvspaceSetValue(self.kv, key.encode(), ptr, length, err, 256) != 0:
            raise RuntimeError(f"kvspaceSetValue({key}): {err.value.decode('utf-8', 'replace')}")

    # KV helpers for handoff (list vids / read pc / write ‥pc / del vids)
    def kv_get(self, key, resolve=1):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        if _ks.kvspaceGet(self.kv, key.encode(), resolve, ctypes.byref(out), ctypes.byref(ol)) != 0:
            return ""
        if not out or ol.value < 8:
            return ""
        raw_wire = ctypes.string_at(out, ol.value)
        blen = int.from_bytes(raw_wire[:6], "little")
        if 8 + blen > len(raw_wire):
            return ""
        return raw_wire[8:8 + blen].decode("utf-8", "replace")

    def kv_set(self, key, val):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        b = val.encode()
        last_seg = key.rsplit("/", 1)[-1]
        lt = b"" if "\u2025" in last_seg else b"char/utf8"
        buf = (ctypes.c_uint8 * max(len(b), 1))()
        if b:
            ctypes.memmove(buf, b, len(b))
        if _ks.kvlangXvalueNewChar(buf, len(b), lt, ctypes.byref(out), ctypes.byref(ol)) != 0:
            return
        try:
            self._set_wire(key, out, ol.value)
        finally:
            _libc.free(ctypes.cast(out, ctypes.c_void_p))

    def kv_exists(self, key):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        return _ks.kvspaceGet(self.kv, key.encode(), 0, ctypes.byref(out), ctypes.byref(ol)) == 0 and bool(out)

    # Prefix listing: ListLen + per-index ListAt into caller buffer (do not free).
    def kv_list(self, prefix, resolve=1):
        count = ctypes.c_int32()
        if _ks.kvspaceListLen(self.kv, prefix.encode(), 0, resolve, ctypes.byref(count)) != 0 or count.value <= 0:
            return []
        names = []
        buf = (ctypes.c_uint8 * 1024)()
        for i in range(count.value):
            ol = ctypes.c_uint32()
            if _ks.kvspaceListAt(self.kv, prefix.encode(), 0, resolve, i, buf, 1024, ctypes.byref(ol)) == 0 and ol.value > 0:
                names.append(ctypes.string_at(buf, ol.value).decode("utf-8", "replace"))
        return names

    def kv_del(self, key):
        keys = (ctypes.c_char_p * 1)(key.encode())
        err = ctypes.create_string_buffer(256)
        _ks.kvspaceDel(self.kv, keys, 1, err, 256)

    # Tensor read/write (v2 8B head + attr, compatible with shm:// and fs://)
    def view(self, key):
        h = kvspace_head_t()
        if _ks.kvspaceGetHead(self.kv, key.encode(), ctypes.byref(h)) != 0:
            return None
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        if _ks.kvspaceGet(self.kv, key.encode(), 1, ctypes.byref(out), ctypes.byref(ol)) != 0 or not out:
            return None
        kx = kvlang_kindexpr_t()
        if _lay.kvlangKindexprParse(ctypes.cast(h.langtype, ctypes.c_char_p), ctypes.byref(kx)) != 0:
            return None
        kind = bytes(kx.kind).split(b"\x00", 1)[0].decode()
        if kx.ref != 0 or kind not in _KIND_CTYPE:
            return None
        dt = np.dtype(_KIND_CTYPE[kind])
        raw_wire = ctypes.string_at(out, ol.value)
        blen = int.from_bytes(raw_wire[:6], "little")
        body = raw_wire[8:8 + blen]
        n = len(body) // dt.itemsize
        flat = np.frombuffer(body[:n * dt.itemsize], dtype=dt).copy()
        if kx.ndim == 0:
            return flat[0] if flat.size > 0 else dt.type(0)
        shape = tuple(kx.dims[i] for i in range(kx.ndim))
        return flat.reshape(shape)

    def alloc(self, key, arr):
        arr = np.asarray(arr)                       # 保形（ascontiguousarray 会把 0 维升 1 维）
        shape = arr.shape
        raw = np.ascontiguousarray(arr).tobytes()
        ndim = len(shape)
        kind = _kind_of(arr.dtype)
        lt = (f"[{','.join(str(d) for d in shape)}]{kind}" if ndim > 0 else kind).encode()
        buf = (ctypes.c_uint8 * max(len(raw), 1))()
        if raw:
            ctypes.memmove(buf, raw, len(raw))
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint64()
        if ndim > 0:
            dims = (ctypes.c_uint64 * ndim)(*shape)
            rc = _ks.kvlangXvalueHeadNewTensor(dims, ndim, buf, len(raw), lt,
                                                ctypes.byref(out), ctypes.byref(ol))
        else:
            rc = _ks.kvlangXvalueHeadNewScalar(0, buf, len(raw), lt,
                                                ctypes.byref(out), ctypes.byref(ol))
        if rc != 0 or not out:
            raise RuntimeError(f"xvalue encode failed: {key}")
        try:
            self._set_wire(key, out, ol.value)
        finally:
            _libc.free(ctypes.cast(out, ctypes.c_void_p))

    # Read arg: load ndarray from frame slot path, or fall back to resolve_read for literals.
    def read_arg(self, pc, i):
        path = _s(_rt.kvlangRwirextResolveReadPath(self.kv, pc.encode(), i))
        if path:
            v = self.view(path)
            if v is not None:
                return v
        return _parse(_s(_rt.kvlangRwirextResolveRead(self.kv, pc.encode(), i)))

    def _ensure_vids_queue(self, op):
        if not self.kv_exists(SHARED_VIDS):
            out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint64()
            dummy = (ctypes.c_uint8 * 1)()
            if _ks.kvlangXvalueHeadNewScalar(0, dummy, 0, SHARED_VIDS_TYPE.encode(),
                                              ctypes.byref(out), ctypes.byref(ol)) == 0 and out:
                try:
                    self._set_wire(SHARED_VIDS, out, ol.value)
                finally:
                    _libc.free(ctypes.cast(out, ctypes.c_void_p))
        vids_key = f"/lib/{op}/vids"
        if not self.kv_exists(vids_key):
            _ks.kvlangXvalueNewPtr.argtypes = [ctypes.c_char_p, ctypes.c_char_p,
                                          ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                          ctypes.POINTER(ctypes.c_uint32)]
            _ks.kvlangXvalueNewPtr.restype = ctypes.c_int
            out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
            if _ks.kvlangXvalueNewPtr(SHARED_VIDS_TYPE.encode(), SHARED_VIDS.encode(),
                                 ctypes.byref(out), ctypes.byref(ol)) == 0 and out:
                try:
                    self._set_wire(vids_key, out, ol.value)
                finally:
                    _libc.free(ctypes.cast(out, ctypes.c_void_p))

    # Register operator families (*T pointer-slot convention + shared vids queue)
    def register(self):
        def _arr(lst):
            return (ctypes.c_char_p * len(lst))(*[s.encode() for s in lst])
        for op, (nr, _) in OPS.items():
            rp = _arr(["*"] * nr)
            wp = _arr(["*"])
            _rt.kvlangDefRwir(self.kv, op.encode(), rp, nr, wp, 1)
            self._ensure_vids_queue(op)
        rp = _arr(["*..."])
        _rt.kvlangDefRwir(self.kv, PRINT_OP.encode(), rp, 1, None, 0)
        self._ensure_vids_queue(PRINT_OP)

    def _handle(self, op, nr, fn, pc):
        params = _s(_rt.kvlangRwirextParams(self.kv, pc.encode())).split("\n")
        if op == PRINT_OP:
            parts = []
            for i in range(len(params) - 1):
                path = _s(_rt.kvlangRwirextResolveReadPath(self.kv, pc.encode(), i))
                v = self.view(path) if path else None
                parts.append(_fmt(v) if v is not None
                             else _s(_rt.kvlangRwirextResolveRead(self.kv, pc.encode(), i)))
            print(" ".join(parts), flush=True)
        else:
            args = [self.read_arg(pc, i) for i in range(nr)]
            self.alloc(_s(_rt.kvlangRwirextResolveWrite(self.kv, pc.encode(), 0)), np.asarray(fn(args)))

    def serve(self, stop):
        table = {**OPS, PRINT_OP: (1, None)}
        while not stop.is_set():
            claimed = set()
            for scan_op in table:
                for vid in self.kv_list(f"/lib/{scan_op}/vids/", 1):
                    vid = vid.rstrip("/")
                    if not vid or vid in claimed:
                        continue
                    pc = self.kv_get(f"/lib/{scan_op}/vids/{vid}", 1)
                    if not pc:
                        continue
                    claimed.add(vid)
                    ps = _s(_rt.kvlangRwirextParams(self.kv, pc.encode())).split("\n")
                    op = ps[0] if ps else ""
                    if op not in table:
                        continue
                    nr, fn = table[op]
                    try:
                        self._handle(op, nr, fn, pc)
                    except Exception as e:                 # 计算失败也要放行，避免 run 阻塞超时
                        print(f"[numpy] {op} @ {pc}: {e}", file=sys.stderr, flush=True)
                    nxt = _s(_rt.kvlangRwirextNextPc(pc.encode()))
                    self.kv_set(f"/vthread/{vid}/‥pc", nxt)
                    self.kv_del(f"/lib/{op}/vids/{vid}")
            time.sleep(0.005)


def _normalize_legacy_syntax(src: str) -> str:
    # Normalize legacy dot member syntax (numpy.foo / demo.main) outside comments and strings.
    out = []
    for line in src.splitlines():
        if line.lstrip().startswith("//"):
            out.append(line)
            continue
        line = re.sub(r"\b(numpy|demo)\.([A-Za-z_]\w*)\b", r"\1·\2", line)
        out.append(line)
    return "\n".join(out) + ("\n" if src.endswith("\n") else "")


def _env():
    ld = f"{LIB}:{LIB / 'kvspace'}"
    if os.environ.get("LD_LIBRARY_PATH"):
        ld = f"{ld}:{os.environ['LD_LIBRARY_PATH']}"
    return {**os.environ, "LD_LIBRARY_PATH": ld,
            "KVSPACE_BACKEND_PATH": os.environ.get("KVSPACE_BACKEND_PATH", str(LIB / "kvspace"))}


def _setup_c_runtime_prefix(tmp_dir: Path) -> Path:
    """Build a private prefix linking byteseek against the C runtime for external handoff."""
    pfx = tmp_dir / "pfx"
    (pfx / "bin").mkdir(parents=True, exist_ok=True)
    (pfx / "lib").mkdir(parents=True, exist_ok=True)
    shutil.copy2(BIN / "byteseek", pfx / "bin" / "byteseek")
    for p in LIB.glob("libkvspace.so*"):
        shutil.copy2(p, pfx / "lib" / p.name, follow_symlinks=False)
    shutil.copy2(LIB / "libkvlanglayout.so", pfx / "lib" / "libkvlanglayout.so")
    if (LIB / "kvspace").is_dir():
        shutil.copytree(LIB / "kvspace", pfx / "lib" / "kvspace", symlinks=True)
    rt_so = Path(os.environ.get("KVLANG_RUNTIME_LIB", str(LIB / "libkvlang_runtime.so"))).resolve()
    os.symlink(rt_so, pfx / "lib" / "libkvlang_runtimec2.so")
    return pfx / "bin" / "byteseek"


def _cleanup_dsn(dsn: str):
    if dsn.startswith("shm://"):
        base = dsn[len("shm://"):]
        for stem in (base, base + ".isa"):
            for suf in ("", ".sbo.data", ".sbo.head", ".garb", ".gen"):
                try:
                    os.unlink(stem + suf)
                except OSError:
                    pass
    elif dsn.startswith("fs://"):
        base = dsn[len("fs://"):]
        shutil.rmtree(base, ignore_errors=True)
        shutil.rmtree(base + ".isa", ignore_errors=True)


def run(kvfile, dsn=None):
    kvfile = Path(kvfile).resolve()
    own_dsn = dsn is None
    dsn = dsn or os.environ.get("KVSPACE") or f"shm:///tmp/kvlang_numpy_{os.getpid()}"
    if own_dsn:
        _cleanup_dsn(dsn)

    with tempfile.TemporaryDirectory(prefix="kvlang-np-pfx-") as tmp:
        driver = _setup_c_runtime_prefix(Path(tmp))
        env = {**_env(), "KVSPACE": dsn}
        pub = subprocess.run([str(driver), "isa", "publish"], capture_output=True, text=True,
                             cwd=str(KVLANG), env=env, timeout=30)
        if pub.returncode != 0:
            if own_dsn:
                _cleanup_dsn(dsn)
            sys.exit(f"isa publish failed: {pub.stderr.strip()}")

        eng = Engine(dsn)
        eng.register()

        src = _normalize_legacy_syntax(kvfile.read_text(encoding="utf-8"))
        entry_buf = ctypes.create_string_buffer(512)
        err_buf = ctypes.create_string_buffer(512)
        if _lay.kvlangLayoutCode(src.encode(), dsn.encode(),
                                 entry_buf, 512, err_buf, 512) != 0:
            eng.close()
            if own_dsn:
                _cleanup_dsn(dsn)
            sys.exit(f"layout failed: {err_buf.value.decode('utf-8', 'replace')}")
        entries = [e for e in entry_buf.value.decode("utf-8", "replace").splitlines() if e.strip()]
        entry = entries[-1] if entries else "init"

        stop = threading.Event()
        th = threading.Thread(target=eng.serve, args=(stop,), daemon=True)
        th.start()
        try:
            r = subprocess.run([str(driver), entry], capture_output=True, text=True,
                               cwd=str(KVLANG), env=env, timeout=60)
        finally:
            stop.set(); th.join(timeout=2); eng.close()
            if own_dsn:
                _cleanup_dsn(dsn)
        if r.stdout:
            print(r.stdout, end="", flush=True)
        if r.returncode != 0:
            sys.exit(f"run failed: {r.stderr.strip()}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python numpy.py <file.kv>")
    run(sys.argv[1])
