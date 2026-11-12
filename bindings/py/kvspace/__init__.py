
import ctypes, os, struct, sys
from pathlib import Path
from typing import Optional

_EXT = ".dylib" if sys.platform == "darwin" else ".so"
_KVLANG_ROOT = Path(__file__).resolve().parent.parent.parent.parent

_lib = None
_KVSPACE_LIB = Path("")

def configure(lib_directory) -> None:
    global _lib, _KVSPACE_LIB
    _KVSPACE_LIB = Path(lib_directory)
    so = _KVSPACE_LIB / f"libkvspace{_EXT}"
    if not so.exists():
        raise RuntimeError(
            f"{so} 不存在。调用 kvspace.configure(lib_directory='/path/to/kvspace/build')")
    _lib = ctypes.CDLL(str(so))
    _bind_all()

def _L():
    if _lib is None:
        raise RuntimeError(
            "kvspace 未配置：先调用 kvspace.configure(lib_directory='/path/to/kvspace/build')")
    return _lib

_libc = ctypes.CDLL(None)
_libc.free.argtypes = [ctypes.c_void_p]
_libc.free.restype = None

def _bind(fn, argtypes, restype):
    fn.argtypes = argtypes
    fn.restype = restype

_U8P = ctypes.POINTER(ctypes.c_uint8)
_U8PP = ctypes.POINTER(_U8P)
_U64P = ctypes.POINTER(ctypes.c_uint64)

def _bind_all():
    _bind(_L().kvspaceConnect, [ctypes.c_char_p], ctypes.c_void_p)
    _bind(_L().kvspaceClose, [ctypes.c_void_p], None)
    _bind(_L().kvspaceXvalueInProcessAddress, [ctypes.c_void_p], _U8P)
    _bind(_L().kvspaceResolveOffset,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
           ctypes.POINTER(ctypes.c_uint64), ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)
    _bind(_L().kvspaceRealloc,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64, ctypes.c_uint64,
           ctypes.POINTER(ctypes.c_uint64), ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)
    _bind(_L().kvspaceFree,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)
    _bind(_L().kvspaceCp,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
           ctypes.c_uint32], ctypes.c_int)
    _bind(_L().kvspaceMkindex,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)
    _bind(_L().kvspaceDeltree,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)
    _bind(_L().kvspaceCpdir,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
           ctypes.c_uint32], ctypes.c_int)
    _bind(_L().kvspaceListlen,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int64),
           ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)
    _bind(_L().kvspaceList,
          [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32,
           ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32),
           ctypes.POINTER(ctypes.c_uint64), ctypes.c_char_p, ctypes.c_uint32],
          ctypes.c_int)

KVSPACE_OK = 0
KVSPACE_MISSING = -2

HEAD_LEN = 16
PADDING_LEN = 6
ATTR_TAG_LANGTYPE = 1
BANNED_ATTR_TAG_REALLEN = 2

REF_INLINE, REF_PTR, REF_EXT = 0, 1, 2

def _take(out, total: ctypes.c_uint64) -> bytes:
    data = ctypes.string_at(out, total.value)
    _libc.free(out)
    return data

def _build(body: bytes, langtype: str = "", prefix: str = "") -> bytes:
    record_body = (prefix + langtype).encode() if (prefix or langtype) else b""
    attrs = (bytes([ATTR_TAG_LANGTYPE, len(record_body) & 0xFF,
                    (len(record_body) >> 8) & 0xFF]) + record_body) if record_body else b""
    head = len(body).to_bytes(8, "little") + len(attrs).to_bytes(2, "little") + b"\x00" * PADDING_LEN
    return head + body + attrs

def _u8buf(raw: bytes):
    if not raw:
        return None
    return ctypes.cast((ctypes.c_uint8 * len(raw)).from_buffer_copy(raw), _U8P)

def _xv_scalar(raw: bytes, langtype: str) -> bytes:
    return _build(raw, langtype)

def _xv_slack(raw: bytes, langtype: str) -> bytes:
    return _build(raw, langtype)

def _split_wire(data: Optional[bytes]):
    if not data or len(data) < HEAD_LEN:
        return None
    body_len = int.from_bytes(data[0:8], "little")
    attr_len = int.from_bytes(data[8:10], "little")
    if any(data[10:10 + PADDING_LEN]):
        return None
    total = HEAD_LEN + body_len + attr_len
    if total != len(data):
        return None
    attr = data[HEAD_LEN + body_len:total]
    p = 0
    while p + 3 <= len(attr):
        tag, rlen = attr[p], int.from_bytes(attr[p + 1:p + 3], "little")
        if p + 3 + rlen > len(attr):
            return None
        if tag == BANNED_ATTR_TAG_REALLEN:
            return None
        p += 3 + rlen
    return data[HEAD_LEN:HEAD_LEN + body_len], attr

def _langtype_of(attr: bytes) -> bytes:
    p = 0
    while p + 3 <= len(attr):
        tag, rlen = attr[p], int.from_bytes(attr[p + 1:p + 3], "little")
        if tag == ATTR_TAG_LANGTYPE:
            return attr[p + 3:p + 3 + rlen]
        p += 3 + rlen
    return b""

def _xv_from_wire(data: Optional[bytes]) -> tuple[int, bytes]:
    parts = _split_wire(data)
    if parts is None:
        return (REF_INLINE, b"")
    body, attr = parts
    langtype = _langtype_of(attr)
    if langtype[:1] == b"*":
        return (REF_PTR, body)
    if langtype[:1] == b"@":
        return (REF_EXT, body)
    return (REF_INLINE, body)

def xv_langtype(data: Optional[bytes]) -> str:
    parts = _split_wire(data)
    if parts is None:
        return ""
    return _langtype_of(parts[1]).decode()

def xv_int(v: int, langtype: str = "") -> bytes:
    return _xv_scalar(struct.pack("<q", v), langtype)

def xv_float(v: float, langtype: str = "") -> bytes:
    return _xv_scalar(struct.pack("<d", v), langtype)

def xv_str(s: str, langtype: str = "") -> bytes:
    return _xv_slack(s.encode(), langtype)

def xv_index() -> bytes:
    return _xv_scalar(b"", "")

def xv_link(target: str, langtype: str = "") -> bytes:
    if langtype.startswith(("*", "@")):
        raise ValueError("langtype 已带引用前缀：双重前缀说明两处对格式理解不一致")
    return _build(target.encode(), langtype, prefix="*")

def xv_ext(locator: str, langtype: str = "") -> bytes:
    if not locator:
        raise ValueError("ext 的 locator 不得为空")
    if langtype.startswith(("*", "@")):
        raise ValueError("langtype 已带引用前缀：双重前缀说明两处对格式理解不一致")
    return _build(locator.encode(), langtype, prefix="@")

class KVSpace:

    def __init__(self, path: str, data_size: int = 32768):
        del data_size
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
        self._path = path
        self._kv = _L().kvspaceConnect(f"shm://{path}".encode())
        if not self._kv:
            raise RuntimeError(f"kvspaceConnect(shm://{path}) failed")
        self._base = _L().kvspaceXvalueInProcessAddress(self._kv)
        if not self._base:
            raise RuntimeError("shm backend must expose an in-process value region")

    def close(self):
        if self._kv:
            _L().kvspaceClose(self._kv)
            self._kv = None
        try:
            os.unlink(self._path)
        except FileNotFoundError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def _view(self, key: str) -> Optional[bytes]:
        off = ctypes.c_uint64(0)
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceResolveOffset(self._kv, key.encode(), 0,
                                 ctypes.byref(off), err, len(err))
        if rc != KVSPACE_OK:
            return None
        base = _L().kvspaceXvalueInProcessAddress(self._kv)
        self._base = base
        head = ctypes.string_at(ctypes.cast(base, ctypes.c_void_p).value + off.value,
                                HEAD_LEN)
        body_len = int.from_bytes(head[0:8], "little")
        attr_len = int.from_bytes(head[8:10], "little")
        return ctypes.string_at(
            ctypes.cast(base, ctypes.c_void_p).value + off.value,
            HEAD_LEN + body_len + attr_len)

    def _write(self, key: str, wire: bytes):
        parts = _split_wire(wire)
        if parts is None:
            raise ValueError(f"malformed wire for {key}")
        body, attr = parts
        off = ctypes.c_uint64(0)
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceRealloc(self._kv, key.encode(), len(body), len(attr),
                                 ctypes.byref(off), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"realloc({key}) failed")
        base = _L().kvspaceXvalueInProcessAddress(self._kv)
        self._base = base
        addr = ctypes.cast(base, ctypes.c_void_p).value + off.value
        if body:
            ctypes.memmove(addr + HEAD_LEN, body, len(body))
        if attr:
            ctypes.memmove(addr + HEAD_LEN + len(body), attr, len(attr))

    def get(self, key: str, resolve: bool = True) -> Optional[bytes]:
        del resolve
        return self._view(key)

    def set(self, key: str, val: bytes):
        self._write(key, val)

    def delete(self, key: str):
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceFree(self._kv, key.encode(), err, len(err))
        if rc not in (KVSPACE_OK, KVSPACE_MISSING):
            raise RuntimeError(err.value.decode() or f"free({key}) failed")

    def deltree(self, prefix: str):
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceDeltree(self._kv, prefix.encode(), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"deltree({prefix}) failed")

    def cp(self, src: str, dst: str):
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceCp(self._kv, src.encode(), dst.encode(), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"cp({src},{dst}) failed")

    def cpdir(self, src: str, dst: str):
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceCpdir(self._kv, src.encode(), dst.encode(), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"cpdir({src},{dst}) failed")

    def mkindex(self, path: str, capacity: int = 0):
        del capacity
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceMkindex(self._kv, path.encode(), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"mkindex({path}) failed")

    def list(self, prefix: str, resolve: bool = True) -> list[str]:
        del resolve
        err = ctypes.create_string_buffer(256)
        n = ctypes.c_int64(0)
        m = ctypes.c_uint32(0)
        rc = _L().kvspaceListlen(self._kv, prefix.encode(),
                                 ctypes.byref(n), ctypes.byref(m), err, len(err))
        if rc != KVSPACE_OK:
            if rc == KVSPACE_MISSING:
                return []
            raise RuntimeError(err.value.decode() or f"listlen({prefix}) failed")
        if n.value <= 0:
            return []
        cap = n.value * (m.value + 1)
        buf = ctypes.create_string_buffer(cap)
        length = ctypes.c_uint32(0)
        count = ctypes.c_uint32(0)
        generation = ctypes.c_uint64(0)
        rc = _L().kvspaceList(self._kv, prefix.encode(), buf, cap,
                              ctypes.byref(length), ctypes.byref(count),
                              ctypes.byref(generation), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"list({prefix}) failed")
        raw = buf.raw[:length.value]
        return [p.decode() for p in raw.split(b"\x00") if p]
