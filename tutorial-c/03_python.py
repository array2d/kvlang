
import ctypes
import os
import struct
import sys

if len(sys.argv) != 2:
    print("usage: 03_python.py <path/to/libpathspace.so>", file=sys.stderr)
    sys.exit(2)
lib = ctypes.CDLL(sys.argv[1])

U8P = ctypes.POINTER(ctypes.c_uint8)
U32P = ctypes.POINTER(ctypes.c_uint32)
U64P = ctypes.POINTER(ctypes.c_uint64)
CCHARPP = ctypes.POINTER(ctypes.c_char_p)

PATHSPACE_XVALUE_HEAD_LEN = 16
PATHLANG_XVALUE_ATTR_TAG_LANGTYPE = 1

def xvalue_head_scan(wire):
    if len(wire) < PATHSPACE_XVALUE_HEAD_LEN:
        return b""
    bodylen = int.from_bytes(wire[0:8], "little")
    attrslen = int.from_bytes(wire[8:10], "little")
    if wire[10:16] != b"\0" * 6:
        return b""
    if PATHSPACE_XVALUE_HEAD_LEN + bodylen + attrslen > len(wire):
        return b""
    langtype, p, left = b"", PATHSPACE_XVALUE_HEAD_LEN + bodylen, attrslen
    while left >= 3:
        tag = wire[p]
        rlen = int.from_bytes(wire[p + 1:p + 3], "little")
        if left < 3 + rlen:
            return b""
        if tag == PATHLANG_XVALUE_ATTR_TAG_LANGTYPE:
            langtype = bytes(wire[p + 3:p + 3 + rlen])
        elif tag == 2:
            return b""
        p += 3 + rlen
        left -= 3 + rlen
    return langtype

def xvalue_head_effective_len(wire):
    if len(wire) < PATHSPACE_XVALUE_HEAD_LEN:
        return None
    return int.from_bytes(wire[0:8], "little")

def bind(fn, args, restype):
    fn.argtypes = args
    fn.restype = restype

bind(lib.pathspaceConnect, [ctypes.c_char_p], ctypes.c_void_p)
bind(lib.pathspaceClose, [ctypes.c_void_p], None)
bind(lib.pathspaceResolveOffset, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
                          U64P, ctypes.c_char_p, ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceRealloc, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
                          ctypes.c_uint64, U64P, ctypes.c_char_p,
                          ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceFree, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                       ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceDeltree, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                          ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceMkindex, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                          ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceListlen, [ctypes.c_void_p, ctypes.c_char_p,
                          ctypes.POINTER(ctypes.c_int64), U32P,
                          ctypes.c_char_p, ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceList, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                       ctypes.c_uint32, U32P, U32P, U64P, ctypes.c_char_p,
                       ctypes.c_uint32], ctypes.c_int)
bind(lib.pathspaceXvalueInProcessAddress, [ctypes.c_void_p], U8P)

ERR = ctypes.create_string_buffer(256)

def err_text(buf):
    return buf.value.decode(errors="replace")

class KV:
    def __init__(self, path):
        try:
            os.unlink(path)
        except FileNotFoundError:
            pass
        self.path = path
        self.kv = lib.pathspaceConnect(f"shm://{path}".encode())
        if not self.kv:
            raise RuntimeError(f"connect shm://{path} failed")
        self.base = lib.pathspaceXvalueInProcessAddress(self.kv)
        if not self.base:
            raise RuntimeError("no in-process address space (shm backend expected)")

    def close(self):
        lib.pathspaceClose(self.kv)
        try:
            os.unlink(self.path)
        except FileNotFoundError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _addr(self, offset):
        return ctypes.byref(self.base.contents, offset)

    def set(self, key, langtype, body):
        lt = langtype.encode() if langtype else b""
        attr_len = 3 + len(lt) if lt else 0
        off = ctypes.c_uint64()
        err = ctypes.create_string_buffer(256)
        rc = lib.pathspaceRealloc(self.kv, key.encode(), len(body), attr_len,
                                ctypes.byref(off), err, 256)
        if rc != 0:
            raise RuntimeError(f"realloc {key}: {err_text(err)}")
        if body:
            ctypes.memmove(self._addr(off.value + PATHSPACE_XVALUE_HEAD_LEN),
                           body, len(body))
        if lt:
            a = off.value + PATHSPACE_XVALUE_HEAD_LEN + len(body)
            ctypes.memmove(self._addr(a), bytes([PATHLANG_XVALUE_ATTR_TAG_LANGTYPE,
                                                 len(lt) & 0xFF, len(lt) >> 8]), 3)
            ctypes.memmove(self._addr(a + 3), lt, len(lt))

    def get(self, key):
        off = ctypes.c_uint64()
        err = ctypes.create_string_buffer(256)
        rc = lib.pathspaceResolveOffset(self.kv, key.encode(), 0, ctypes.byref(off),
                                err, 256)
        if rc != 0:
            return None
        wire = ctypes.string_at(self._addr(off.value),
                                PATHSPACE_XVALUE_HEAD_LEN)
        attrs_len = int.from_bytes(wire[8:10], "little")
        body_len = int.from_bytes(wire[0:8], "little")
        total = PATHSPACE_XVALUE_HEAD_LEN + body_len + attrs_len
        full = ctypes.string_at(self._addr(off.value), total)
        langtype = xvalue_head_scan(full)
        kind = langtype.decode(errors="replace")
        if kind[:1] in ("*", "@"):
            kind = kind[1:]
        if kind.startswith("["):
            kind = kind[kind.index("]") + 1:]
        body = full[PATHSPACE_XVALUE_HEAD_LEN:PATHSPACE_XVALUE_HEAD_LEN + body_len]
        return kind, body

    def delete(self, key):
        err = ctypes.create_string_buffer(256)
        lib.pathspaceFree(self.kv, key.encode(), err, 256)

    def deltree(self, prefix):
        err = ctypes.create_string_buffer(256)
        lib.pathspaceDeltree(self.kv, prefix.encode(), err, 256)

    def mkindex(self, path):
        err = ctypes.create_string_buffer(256)
        lib.pathspaceMkindex(self.kv, path.encode(), err, 256)

    def list(self, prefix):
        count = ctypes.c_int64()
        max_name = ctypes.c_uint32()
        err = ctypes.create_string_buffer(256)
        rc = lib.pathspaceListlen(self.kv, prefix.encode(), ctypes.byref(count),
                                ctypes.byref(max_name), err, 256)
        if rc != 0 or count.value <= 0:
            return []
        cap = count.value * (max_name.value + 1)
        buf = ctypes.create_string_buffer(cap)
        length = ctypes.c_uint32()
        listed = ctypes.c_uint32()
        gen = ctypes.c_uint64()
        rc = lib.pathspaceList(self.kv, prefix.encode(), buf, cap,
                             ctypes.byref(length), ctypes.byref(listed),
                             ctypes.byref(gen), err, 256)
        if rc != 0:
            return []
        names = []
        for name in buf.raw[:length.value].split(b"\0"):
            if name:
                names.append(name.decode())
        return names

with KV("/tmp/pathspace_py.shm") as kv:
    kv.mkindex("/py/")

    kv.set("/py/a", "int64", struct.pack("<q", 42))
    kv.set("/py/b", "char/utf8", "你好".encode())

    for k in ["/py/a", "/py/b"]:
        got = kv.get(k)
        if got is None:
            print(f"{k}\t(nil)")
            continue
        kind, raw = got
        print(f"{k}\t{kind}:{raw.decode() if kind != 'int64' else struct.unpack('<q', raw)[0]}")

    ns = kv.list("/py/")
    print(f"list /py/: {sorted(ns)} (count={len(ns)})")

    kv.delete("/py/b")
    assert len(kv.list("/py/")) == 1
    kv.deltree("/py/")
    assert kv.list("/py/") == []

print("PASS python")
