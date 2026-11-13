
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

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path = [p for p in sys.path if os.path.abspath(p or os.getcwd()) != _HERE]
import numpy as np

PATHLANG = Path(__file__).resolve().parents[3]
BIN = PATHLANG / "bin"
LIB = PATHLANG / "lib"

def _argv_option(name):
    prefix = f"{name}="
    for a in sys.argv[1:]:
        if a.startswith(prefix):
            return a[len(prefix):]
    return None

_RUNTIME_LIB_OVERRIDE = _argv_option("runtime-lib")

def _find_lib(name, path, *rels):
    cands = ([path] if path else []) + [str(PATHLANG / r) for r in rels]
    for c in cands:
        if os.path.exists(c):
            return ctypes.CDLL(c)
    return ctypes.CDLL(name)

if os.path.isdir(LIB / "pathspace") and "PATHSPACE_BACKEND_PATH" not in os.environ:
    os.environ["PATHSPACE_BACKEND_PATH"] = str(LIB / "pathspace")

_ks = _find_lib("libpathspace.so.1", None, "lib/libpathspace.so.1", "lib/libpathspace.so")
_rt = _find_lib("libpathlang_runtime.so", _RUNTIME_LIB_OVERRIDE,
                "lib/libpathlang_runtime.so", "lib/libpathlang_runtimec2.so")
_lay = _find_lib("libpathlanglayout.so", None,
                 "lib/libpathlanglayout.so", "layout/target/release/libpathlanglayout.so")

class pathspace_head_t(ctypes.Structure):
    _fields_ = [("headlen", ctypes.c_uint16), ("ref", ctypes.c_uint8),
                ("storetype", ctypes.c_uint8), ("body_len", ctypes.c_int32),
                ("ndim", ctypes.c_int32), ("dims", ctypes.c_int32 * 8),
                ("langtype", ctypes.c_uint8 * 256), ("langtype_len", ctypes.c_int32),
                ("body_offset", ctypes.c_int32), ("body_cap", ctypes.c_uint64)]

class pathlang_kindexpr_t(ctypes.Structure):
    _fields_ = [("ref", ctypes.c_int32), ("ndim", ctypes.c_int32),
                ("dims", ctypes.c_int32 * 8), ("array_len", ctypes.c_int32),
                ("kind", ctypes.c_uint8 * 64)]

def _bind():
    _ks.pathspaceConnect.argtypes = [ctypes.c_char_p]; _ks.pathspaceConnect.restype = ctypes.c_void_p
    _ks.pathspaceClose.argtypes = [ctypes.c_void_p]
    _ks.pathspaceGet.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
                               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                               ctypes.POINTER(ctypes.c_uint32)]
    _ks.pathspaceGet.restype = ctypes.c_int
    _ks.pathspaceGetHead.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(pathspace_head_t)]
    _ks.pathspaceGetHead.restype = ctypes.c_int
    _ks.pathspaceSetValue.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                    ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                                    ctypes.c_char_p, ctypes.c_uint32]
    _ks.pathspaceSetValue.restype = ctypes.c_int
    _ks.pathspaceListLen.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_int32)]
    _ks.pathspaceListLen.restype = ctypes.c_int
    _ks.pathspaceListAt.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int32, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                                  ctypes.POINTER(ctypes.c_uint32)]
    _ks.pathspaceListAt.restype = ctypes.c_int
    _ks.pathspaceDel.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_uint32,
                               ctypes.c_char_p, ctypes.c_uint32]
    _ks.pathspaceDel.restype = ctypes.c_int
    _ks.pathlangXvalueNewChar.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.c_char_p,
                                   ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                   ctypes.POINTER(ctypes.c_uint32)]
    _ks.pathlangXvalueNewChar.restype = ctypes.c_int
    _ks.pathlangXvalueHeadNewScalar.argtypes = [ctypes.c_uint8, ctypes.POINTER(ctypes.c_uint8),
                                               ctypes.c_uint32, ctypes.c_char_p,
                                               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                               ctypes.POINTER(ctypes.c_uint64)]
    _ks.pathlangXvalueHeadNewScalar.restype = ctypes.c_int
    _ks.pathlangXvalueHeadNewTensor.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_uint32,
                                               ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint64,
                                               ctypes.c_char_p,
                                               ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                               ctypes.POINTER(ctypes.c_uint64)]
    _ks.pathlangXvalueHeadNewTensor.restype = ctypes.c_int
    for fn in ("pathlangRwirextParams", "pathlangRwirextResolveRead", "pathlangRwirextResolveReadPath",
               "pathlangRwirextResolveWrite", "pathlangRwirextNextPc"):
        getattr(_rt, fn).restype = ctypes.c_void_p
    _rt.pathlangDefRwir.restype = ctypes.c_int
    _rt.pathlangDefRwir.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                  ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32,
                                  ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32]
    _rt.pathlangRwirextParams.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _rt.pathlangRwirextResolveRead.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.pathlangRwirextResolveReadPath.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.pathlangRwirextResolveWrite.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.pathlangRwirextNextPc.argtypes = [ctypes.c_char_p]
    _lay.pathlangLayoutCode.restype = ctypes.c_int
    _lay.pathlangLayoutCode.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
                                      ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]
    _lay.pathlangKindexprParse.restype = ctypes.c_int
    _lay.pathlangKindexprParse.argtypes = [ctypes.c_char_p, ctypes.POINTER(pathlang_kindexpr_t)]

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
        self.kv = _ks.pathspaceConnect(dsn.encode())
        if not self.kv:
            raise RuntimeError(f"pathspaceConnect failed: {dsn}")

    def close(self):
        if self.kv:
            _ks.pathspaceClose(self.kv); self.kv = None

    def _set_wire(self, key, ptr, length):
        err = ctypes.create_string_buffer(256)
        if _ks.pathspaceSetValue(self.kv, key.encode(), ptr, length, err, 256) != 0:
            raise RuntimeError(f"pathspaceSetValue({key}): {err.value.decode('utf-8', 'replace')}")

    def kv_get(self, key, resolve=1):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        if _ks.pathspaceGet(self.kv, key.encode(), resolve, ctypes.byref(out), ctypes.byref(ol)) != 0:
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
        if _ks.pathlangXvalueNewChar(buf, len(b), lt, ctypes.byref(out), ctypes.byref(ol)) != 0:
            return
        try:
            self._set_wire(key, out, ol.value)
        finally:
            _libc.free(ctypes.cast(out, ctypes.c_void_p))

    def kv_exists(self, key):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        return _ks.pathspaceGet(self.kv, key.encode(), 0, ctypes.byref(out), ctypes.byref(ol)) == 0 and bool(out)

    def kv_list(self, prefix, resolve=1):
        count = ctypes.c_int32()
        if _ks.pathspaceListLen(self.kv, prefix.encode(), 0, resolve, ctypes.byref(count)) != 0 or count.value <= 0:
            return []
        names = []
        buf = (ctypes.c_uint8 * 1024)()
        for i in range(count.value):
            ol = ctypes.c_uint32()
            if _ks.pathspaceListAt(self.kv, prefix.encode(), 0, resolve, i, buf, 1024, ctypes.byref(ol)) == 0 and ol.value > 0:
                names.append(ctypes.string_at(buf, ol.value).decode("utf-8", "replace"))
        return names

    def kv_del(self, key):
        keys = (ctypes.c_char_p * 1)(key.encode())
        err = ctypes.create_string_buffer(256)
        _ks.pathspaceDel(self.kv, keys, 1, err, 256)

    def view(self, key):
        h = pathspace_head_t()
        if _ks.pathspaceGetHead(self.kv, key.encode(), ctypes.byref(h)) != 0:
            return None
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        if _ks.pathspaceGet(self.kv, key.encode(), 1, ctypes.byref(out), ctypes.byref(ol)) != 0 or not out:
            return None
        kx = pathlang_kindexpr_t()
        if _lay.pathlangKindexprParse(ctypes.cast(h.langtype, ctypes.c_char_p), ctypes.byref(kx)) != 0:
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
        arr = np.asarray(arr)
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
            rc = _ks.pathlangXvalueHeadNewTensor(dims, ndim, buf, len(raw), lt,
                                                ctypes.byref(out), ctypes.byref(ol))
        else:
            rc = _ks.pathlangXvalueHeadNewScalar(0, buf, len(raw), lt,
                                                ctypes.byref(out), ctypes.byref(ol))
        if rc != 0 or not out:
            raise RuntimeError(f"xvalue encode failed: {key}")
        try:
            self._set_wire(key, out, ol.value)
        finally:
            _libc.free(ctypes.cast(out, ctypes.c_void_p))

    def read_arg(self, pc, i):
        path = _s(_rt.pathlangRwirextResolveReadPath(self.kv, pc.encode(), i))
        if path:
            v = self.view(path)
            if v is not None:
                return v
        return _parse(_s(_rt.pathlangRwirextResolveRead(self.kv, pc.encode(), i)))

    def _ensure_vids_queue(self, op):
        if not self.kv_exists(SHARED_VIDS):
            out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint64()
            dummy = (ctypes.c_uint8 * 1)()
            if _ks.pathlangXvalueHeadNewScalar(0, dummy, 0, SHARED_VIDS_TYPE.encode(),
                                              ctypes.byref(out), ctypes.byref(ol)) == 0 and out:
                try:
                    self._set_wire(SHARED_VIDS, out, ol.value)
                finally:
                    _libc.free(ctypes.cast(out, ctypes.c_void_p))
        vids_key = f"/lib/{op}/vids"
        if not self.kv_exists(vids_key):
            _ks.pathlangXvalueNewPtr.argtypes = [ctypes.c_char_p, ctypes.c_char_p,
                                          ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
                                          ctypes.POINTER(ctypes.c_uint32)]
            _ks.pathlangXvalueNewPtr.restype = ctypes.c_int
            out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
            if _ks.pathlangXvalueNewPtr(SHARED_VIDS_TYPE.encode(), SHARED_VIDS.encode(),
                                 ctypes.byref(out), ctypes.byref(ol)) == 0 and out:
                try:
                    self._set_wire(vids_key, out, ol.value)
                finally:
                    _libc.free(ctypes.cast(out, ctypes.c_void_p))

    def register(self):
        def _arr(lst):
            return (ctypes.c_char_p * len(lst))(*[s.encode() for s in lst])
        for op, (nr, _) in OPS.items():
            rp = _arr(["*"] * nr)
            wp = _arr(["*"])
            _rt.pathlangDefRwir(self.kv, op.encode(), rp, nr, wp, 1)
            self._ensure_vids_queue(op)
        rp = _arr(["*..."])
        _rt.pathlangDefRwir(self.kv, PRINT_OP.encode(), rp, 1, None, 0)
        self._ensure_vids_queue(PRINT_OP)

    def _handle(self, op, nr, fn, pc):
        params = _s(_rt.pathlangRwirextParams(self.kv, pc.encode())).split("\n")
        if op == PRINT_OP:
            parts = []
            for i in range(len(params) - 1):
                path = _s(_rt.pathlangRwirextResolveReadPath(self.kv, pc.encode(), i))
                v = self.view(path) if path else None
                parts.append(_fmt(v) if v is not None
                             else _s(_rt.pathlangRwirextResolveRead(self.kv, pc.encode(), i)))
            print(" ".join(parts), flush=True)
        else:
            args = [self.read_arg(pc, i) for i in range(nr)]
            self.alloc(_s(_rt.pathlangRwirextResolveWrite(self.kv, pc.encode(), 0)), np.asarray(fn(args)))

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
                    ps = _s(_rt.pathlangRwirextParams(self.kv, pc.encode())).split("\n")
                    op = ps[0] if ps else ""
                    if op not in table:
                        continue
                    nr, fn = table[op]
                    try:
                        self._handle(op, nr, fn, pc)
                    except Exception as e:
                        print(f"[numpy] {op} @ {pc}: {e}", file=sys.stderr, flush=True)
                    nxt = _s(_rt.pathlangRwirextNextPc(pc.encode()))
                    self.kv_set(f"/vthread/{vid}/‥pc", nxt)
                    self.kv_del(f"/lib/{op}/vids/{vid}")
            time.sleep(0.005)

def _normalize_legacy_syntax(src: str) -> str:
    out = []
    for line in src.splitlines():
        if line.lstrip().startswith("//"):
            out.append(line)
            continue
        line = re.sub(r"\b(numpy|demo)\.([A-Za-z_]\w*)\b", r"\1·\2", line)
        out.append(line)
    return "\n".join(out) + ("\n" if src.endswith("\n") else "")

def _env():
    ld = f"{LIB}:{LIB / 'pathspace'}"
    if os.environ.get("LD_LIBRARY_PATH"):
        ld = f"{ld}:{os.environ['LD_LIBRARY_PATH']}"
    return {**os.environ, "LD_LIBRARY_PATH": ld,
            "PATHSPACE_BACKEND_PATH": os.environ.get("PATHSPACE_BACKEND_PATH", str(LIB / "pathspace"))}

def _setup_c_runtime_prefix(tmp_dir: Path) -> Path:
    pfx = tmp_dir / "pfx"
    (pfx / "bin").mkdir(parents=True, exist_ok=True)
    (pfx / "lib").mkdir(parents=True, exist_ok=True)
    shutil.copy2(BIN / "pathseek", pfx / "bin" / "pathseek")
    for p in LIB.glob("libpathspace.so*"):
        shutil.copy2(p, pfx / "lib" / p.name, follow_symlinks=False)
    shutil.copy2(LIB / "libpathlanglayout.so", pfx / "lib" / "libpathlanglayout.so")
    if (LIB / "pathspace").is_dir():
        shutil.copytree(LIB / "pathspace", pfx / "lib" / "pathspace", symlinks=True)
    rt_so = Path(_RUNTIME_LIB_OVERRIDE or (LIB / "libpathlang_runtime.so")).resolve()
    os.symlink(rt_so, pfx / "lib" / "libpathlang_runtimec2.so")
    return pfx / "bin" / "pathseek"

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
    dsn = dsn or os.environ.get("PATHSPACE") or f"shm:///tmp/pathlang_numpy_{os.getpid()}"
    if own_dsn:
        _cleanup_dsn(dsn)

    with tempfile.TemporaryDirectory(prefix="pathlang-np-pfx-") as tmp:
        driver = _setup_c_runtime_prefix(Path(tmp))
        env = {**_env(), "PATHSPACE": dsn}
        pub = subprocess.run([str(driver), "isa", "publish"], capture_output=True, text=True,
                             cwd=str(PATHLANG), env=env, timeout=30)
        if pub.returncode != 0:
            if own_dsn:
                _cleanup_dsn(dsn)
            sys.exit(f"isa publish failed: {pub.stderr.strip()}")

        eng = Engine(dsn)
        eng.register()

        src = _normalize_legacy_syntax(kvfile.read_text(encoding="utf-8"))
        entry_buf = ctypes.create_string_buffer(512)
        err_buf = ctypes.create_string_buffer(512)
        if _lay.pathlangLayoutCode(src.encode(), dsn.encode(),
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
                               cwd=str(PATHLANG), env=env, timeout=60)
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
