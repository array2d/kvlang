#!/usr/bin/env python3
"""numpy rwirext —— 单文件引擎 + 编排器。

    python numpy.py <file.kv>

依序执行：
  1. kvlang layout（bin/kvlanglayout）把 .kv 布局进 shm；
  2. 本进程注册五大类 numpy 算子到 /lib/numpy.*，起后台 serve 线程（mode-1 WATCH 手递）；
  3. bin/run 执行主程序，遇 numpy.* 指令通过 .todo/.done 手递给本进程计算/打印。

设计要点：
  · ndarray 不是新类型，就是 [dims]dtype——一维字面量由 kvlang 产生，更高的秩由 numpy
    算子（reshape/matmul/…）产出并按 [dims]dtype 存回 kvspace。
  · tensor data 零拷贝：读走权威 kvspaceDecodeHead，在 kvspace-c SHM 地址上建 ndarray 视图；
    写走权威 N 维 kvspaceTlvEncode，dims 直接落盘。
  · 遵照读写码：从 kvlang_rwirextParams 取 opcode 与读/写操作数名；读参优先按路径零拷贝 view，
    否则回退 kvlang_rwirextResolveRead；写参经 kvlang_rwirextResolveWrite 解析为 KV 路径。
  · numpy.print 走本进程自身的 stdout（不经 kvlang println）。
"""

import ast
import ctypes
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

# 本文件名为 numpy.py，须先把自身所在目录移出 sys.path，才能 import 到真正的 numpy 包。
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path = [p for p in sys.path if os.path.abspath(p or os.getcwd()) != _HERE]
import numpy as np  # noqa: E402

# ── 动态库解析（env 覆盖 → 仓库内构建目录 → 交给链接器）─────────────────
ROOT = Path(__file__).resolve().parents[4]          # .../array2d
KVLANG = ROOT / "kvlang"
BIN = KVLANG / "bin"


def _find_lib(name, env, rel):
    for c in ([os.environ[env]] if os.environ.get(env) else []) + [str(ROOT / rel)]:
        if os.path.exists(c):
            return ctypes.CDLL(c)
    return ctypes.CDLL(name)


_ks = _find_lib("libkvspace-c.so", "KVSPACE_C_LIB", "kvspace-c/build/libkvspace-c.so")
_rt = _find_lib("libkvlang_runtime.so", "KVLANG_RUNTIME_LIB", "kvlang/bin/libkvlang_runtime.so")
_lay = _find_lib("libkvlang_layout.so", "KVLANG_LAYOUT_LIB", "kvlang/layout/target/release/libkvlang_layout.so")


class kvspace_head_t(ctypes.Structure):
    _fields_ = [("kindexpr", ctypes.c_uint8 * 256), ("ro", ctypes.c_uint8),
                ("vid", ctypes.c_uint32), ("body_len", ctypes.c_int32),
                ("body_offset", ctypes.c_int32)]


class kvlang_kindexpr_t(ctypes.Structure):
    _fields_ = [("ref", ctypes.c_int32), ("ndim", ctypes.c_int32),
                ("dims", ctypes.c_int32 * 8), ("array_len", ctypes.c_int32),
                ("kind", ctypes.c_uint8 * 64)]


def _bind():
    # kvspace ABI（扩展宿主自连）：句柄 = kvspaceConnect(dsn)，shm 下即 ShmOpen，可直传 Shm* 零拷贝
    _ks.kvspaceConnect.argtypes = [ctypes.c_char_p]; _ks.kvspaceConnect.restype = ctypes.c_void_p
    _ks.kvspaceFree.argtypes = [ctypes.c_void_p]
    _ks.kvspaceShmGet.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int32)]
    _ks.kvspaceShmGet.restype = ctypes.POINTER(ctypes.c_uint8)
    _ks.kvspaceShmSet.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int32]
    _ks.kvspaceDecodeHead.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32, ctypes.POINTER(kvspace_head_t)]
    _ks.kvspaceDecodeHead.restype = ctypes.c_int
    _ks.kvspaceTlvEncode.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                                      ctypes.POINTER(ctypes.c_int32), ctypes.c_int32,
                                      ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint32)]
    _ks.kvspaceTlvEncode.restype = ctypes.c_int
    _ks.kvspaceBytesFree.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
    _ks.kvspaceList.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int,
                                ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint32)]
    _ks.kvspaceList.restype = ctypes.c_int
    _ks.kvspaceNewChar.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32,
                                   ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.POINTER(ctypes.c_uint32)]
    _ks.kvspaceNewChar.restype = ctypes.c_int
    _ks.kvspaceDel.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_uint32,
                               ctypes.c_char_p, ctypes.c_uint32]
    _ks.kvspaceDel.restype = ctypes.c_int
    # rwirext ABI（kvspace 不提供的 runtime 语义）：句柄传扩展自连的 kvspace
    for fn in ("kvlang_rwirextParams", "kvlang_rwirextResolveRead", "kvlang_rwirextResolveReadPath",
               "kvlang_rwirextResolveWrite", "kvlang_rwirextNextPc"):
        getattr(_rt, fn).restype = ctypes.c_void_p
    _rt.kvlang_rwirextRegister.restype = ctypes.c_int
    _rt.kvlang_rwirextRegister.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_char_p]
    _rt.kvlang_rwirextParams.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _rt.kvlang_rwirextResolveRead.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.kvlang_rwirextResolveReadPath.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.kvlang_rwirextResolveWrite.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    _rt.kvlang_rwirextNextPc.argtypes = [ctypes.c_char_p]
    _lay.kvlangLayoutFile.restype = ctypes.c_int
    _lay.kvlangLayoutFile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
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
OPS = {f"numpy.{n}": spec for cat in _CATEGORIES.values() for n, spec in cat.items()}


class Engine:
    def __init__(self, dsn):
        self.kv = _ks.kvspaceConnect(dsn.encode())
        if not self.kv:
            raise RuntimeError(f"kvspaceConnect failed: {dsn}")

    def close(self):
        if self.kv:
            _ks.kvspaceFree(self.kv); self.kv = None

    # ── 句柄自带的 KV 存取（handoff：list todo / read todo / write pc,done / del todo）──
    def kv_get(self, key):
        ol = ctypes.c_int32()
        d = _ks.kvspaceShmGet(self.kv, key.encode(), 1, ctypes.byref(ol))
        if not d or ol.value <= 0:
            return ""
        h = kvspace_head_t()
        if _ks.kvspaceDecodeHead(d, ol.value, ctypes.byref(h)) != 0 or h.body_len <= 0:
            return ""
        raw = ctypes.string_at(ctypes.cast(d, ctypes.c_void_p).value + h.body_offset, h.body_len)
        return raw.decode("utf-8", "replace")

    def kv_set(self, key, val):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        b = val.encode()
        if _ks.kvspaceNewChar(b, len(b), ctypes.byref(out), ctypes.byref(ol)) != 0:
            return
        _ks.kvspaceShmSet(self.kv, key.encode(), out, ol.value)
        _ks.kvspaceBytesFree(out, ol.value)

    def kv_list(self, prefix):
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        if _ks.kvspaceList(self.kv, prefix.encode(), 0, 0, ctypes.byref(out), ctypes.byref(ol)) != 0 \
                or not out or ol.value == 0:
            if out:
                _ks.kvspaceBytesFree(out, ol.value)
            return []
        s = ctypes.string_at(out, ol.value).decode("utf-8", "replace")
        _ks.kvspaceBytesFree(out, ol.value)
        return s.split("\n") if s else []

    def kv_del(self, key):
        keys = (ctypes.c_char_p * 1)(key.encode())
        err = ctypes.create_string_buffer(256)
        _ks.kvspaceDel(self.kv, keys, 1, err, 256)

    # ── tensor 零拷贝读写 ───────────────────────────────────────────
    def view(self, key):
        ol = ctypes.c_int32()
        d = _ks.kvspaceShmGet(self.kv, key.encode(), 1, ctypes.byref(ol))
        if not d:
            return None
        h = kvspace_head_t()
        if _ks.kvspaceDecodeHead(d, ol.value, ctypes.byref(h)) != 0:
            return None
        kx = kvlang_kindexpr_t()
        if _lay.kvlangKindexprParse(ctypes.cast(h.kindexpr, ctypes.c_char_p), ctypes.byref(kx)) != 0:
            return None
        kind = bytes(kx.kind).split(b"\x00", 1)[0].decode()
        if kx.ref != 0 or kind not in _KIND_CTYPE:
            return None
        elem = _KIND_CTYPE[kind]
        n = h.body_len // ctypes.sizeof(elem)
        base = ctypes.cast(d, ctypes.c_void_p).value
        ptr = ctypes.cast(base + h.body_offset, ctypes.POINTER(elem))
        flat = np.ctypeslib.as_array(ptr, shape=(max(n, 1),))
        if kx.ndim == 0:
            return flat[0]
        return flat[:n].reshape(tuple(kx.dims[i] for i in range(kx.ndim)))

    def alloc(self, key, arr):
        arr = np.asarray(arr)                       # 保形（ascontiguousarray 会把 0 维升 1 维）
        shape = arr.shape
        raw = np.ascontiguousarray(arr).tobytes()
        ndim = len(shape)
        dims = (ctypes.c_int32 * ndim)(*shape) if ndim > 0 else None
        buf = (ctypes.c_uint8 * len(raw)).from_buffer_copy(raw) if raw else None
        out = ctypes.POINTER(ctypes.c_uint8)(); ol = ctypes.c_uint32()
        if _ks.kvspaceTlvEncode(_kind_of(arr.dtype).encode(),
                                 ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8)), len(raw),
                                 dims, ndim, ctypes.byref(out), ctypes.byref(ol)) != 0:
            raise RuntimeError(f"tlv_encode failed: {key}")
        _ks.kvspaceShmSet(self.kv, key.encode(), out, ol.value)
        _ks.kvspaceBytesFree(out, ol.value)

    # ── 读参：解析为帧槽路径后零拷贝 view；内联字面量回退 resolve_read ──
    def read_arg(self, pc, i):
        path = _s(_rt.kvlang_rwirextResolveReadPath(self.kv, pc.encode(), i))
        if path:
            v = self.view(path)
            if v is not None:
                return v
        return _parse(_s(_rt.kvlang_rwirextResolveRead(self.kv, pc.encode(), i)))

    # ── 注册五大类 ─────────────────────────────────────────────────
    def register(self):
        for op, (nr, _) in OPS.items():
            _rt.kvlang_rwirextRegister(self.kv, op.encode(), nr, 1, ("\n".join(["any"] * (nr + 1))).encode())
        _rt.kvlang_rwirextRegister(self.kv, b"numpy.print", 1, 0, b"any...")

    def _handle(self, op, nr, fn, pc):
        params = _s(_rt.kvlang_rwirextParams(self.kv, pc.encode())).split("\n")
        if op == "numpy.print":
            parts = []
            for i in range(len(params) - 1):
                path = _s(_rt.kvlang_rwirextResolveReadPath(self.kv, pc.encode(), i))
                v = self.view(path) if path else None
                parts.append(_fmt(v) if v is not None
                             else _s(_rt.kvlang_rwirextResolveRead(self.kv, pc.encode(), i)))
            print(" ".join(parts), flush=True)
        else:
            args = [self.read_arg(pc, i) for i in range(nr)]
            self.alloc(_s(_rt.kvlang_rwirextResolveWrite(self.kv, pc.encode(), 0)), np.asarray(fn(args)))

    def serve(self, stop):
        table = {**OPS, "numpy.print": (1, None)}
        while not stop.is_set():
            for op, (nr, fn) in table.items():
                base = f"/lib/{op}"
                for child in self.kv_list(f"{base}/"):
                    if not (child.startswith(".todo<") and child.endswith(">")):
                        continue
                    vid = child[6:-1]
                    todo = f"{base}/{child}"
                    pc, _, pid = self.kv_get(todo).rpartition("|")
                    try:
                        self._handle(op, nr, fn, pc)
                    except Exception as e:                 # 计算失败也要放行，避免 run 阻塞超时
                        print(f"[numpy] {op} @ {pc}: {e}", file=sys.stderr, flush=True)
                    nxt = _s(_rt.kvlang_rwirextNextPc(pc.encode()))
                    self.kv_set(f"/vthread/{vid}/‥pc", nxt)
                    self.kv_set(f"{base}/.done<{vid}>", pid)
                    self.kv_del(todo)
            time.sleep(0.01)


def _env():
    return {**os.environ, "LD_LIBRARY_PATH": f"{BIN}:{ROOT / 'kvspace-c' / 'build'}"}


def run(kvfile, dsn=None):
    kvfile = Path(kvfile).resolve()
    dsn = dsn or f"shm:///tmp/kvlang_numpy_{os.getpid()}"
    shm = dsn[len("shm://"):]
    try:
        os.unlink(shm)
    except OSError:
        pass

    entry_buf = ctypes.create_string_buffer(512)
    err_buf = ctypes.create_string_buffer(512)
    if _lay.kvlangLayoutFile(str(kvfile).encode(), dsn.encode(),
                               entry_buf, 512, err_buf, 512) != 0:
        sys.exit(f"layout failed: {err_buf.value.decode()}")
    entry = entry_buf.value.decode() or "init"

    eng = Engine(dsn)
    eng.register()
    stop = threading.Event()
    th = threading.Thread(target=eng.serve, args=(stop,), daemon=True)
    th.start()
    try:
        r = subprocess.run([str(BIN / "run"), entry], capture_output=True, text=True,
                           cwd=str(KVLANG), env={**_env(), "KVSPACE": dsn}, timeout=60)
    finally:
        stop.set(); th.join(timeout=2); eng.close()
    if r.returncode != 0:
        sys.exit(f"run failed: {r.stderr.strip()}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python numpy.py <file.kv>")
    run(sys.argv[1])
