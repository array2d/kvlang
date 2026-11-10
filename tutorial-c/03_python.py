#!/usr/bin/env python3
"""03_python — ctypes 直调 libkvspace（dispatch 前端），shm:// 后端
head 固定 16 字节 [bodylen:u64@0][attrslen:u16@8][padding:6@10-15]，body 偏移恒 16，
langtype 在 body 之后的 attr 区。bodylen 恒为有效字节数，容量归存储后端。"""

import ctypes
import os
import struct
import sys

# 库路径走命令行参数：全生态只允许 `KVSPACE` 一个环境变量，路径类输入一律显式给。
if len(sys.argv) != 2:
    print("usage: 03_python.py <path/to/libkvspace.so>", file=sys.stderr)
    sys.exit(2)
lib = ctypes.CDLL(sys.argv[1])

U8P = ctypes.POINTER(ctypes.c_uint8)
U32P = ctypes.POINTER(ctypes.c_uint32)
U64P = ctypes.POINTER(ctypes.c_uint64)
CCHARPP = ctypes.POINTER(ctypes.c_char_p)

KVSPACE_XVALUE_HEAD_LEN = 16
KVLANG_XVALUE_ATTR_TAG_LANGTYPE = 1


def xvalue_head_scan(wire):
    """扫描 16 字节 head + attr 区，返回 langtype_bytes（无记录/坏 wire → b""）。

    与 C 的 `kvspaceXvalueGetAttr` 同语义；Python 侧没有 inline 头可用，
    这里按线格式直接解，**不复制 C 的结构体字段**（那是第二份字段真相）。
    wire: [bodylen:u64][attrslen:u16][padding:6][body][attrs]
    """
    if len(wire) < KVSPACE_XVALUE_HEAD_LEN:
        return b""
    bodylen = int.from_bytes(wire[0:8], "little")
    attrslen = int.from_bytes(wire[8:10], "little")
    if wire[10:16] != b"\0" * 6:
        return b""
    if KVSPACE_XVALUE_HEAD_LEN + bodylen + attrslen > len(wire):
        return b""
    langtype, p, left = b"", KVSPACE_XVALUE_HEAD_LEN + bodylen, attrslen
    while left >= 3:
        tag = wire[p]
        rlen = int.from_bytes(wire[p + 1:p + 3], "little")
        if left < 3 + rlen:
            return b""
        if tag == KVLANG_XVALUE_ATTR_TAG_LANGTYPE:
            langtype = bytes(wire[p + 3:p + 3 + rlen])
        elif tag == 2:
            return b""  # 旧 reallen tag 已删除，出现即拒
        p += 3 + rlen
        left -= 3 + rlen
    return langtype


def xvalue_head_effective_len(wire):
    """有效长度 = bodylen（head 里没有 layout，也没有 tag 2）。"""
    if len(wire) < KVSPACE_XVALUE_HEAD_LEN:
        return None
    return int.from_bytes(wire[0:8], "little")


def bind(fn, args, restype):
    fn.argtypes = args
    fn.restype = restype


bind(lib.kvspaceConnect, [ctypes.c_char_p], ctypes.c_void_p)
bind(lib.kvspaceClose, [ctypes.c_void_p], None)
# 新 ABI 三个动作：resolve 拿 offset / realloc 拿写入位置 / listlen+list 枚举目录。
bind(lib.kvspaceResolveOffset, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
                          U64P, ctypes.c_char_p, ctypes.c_uint32], ctypes.c_int)
bind(lib.kvspaceRealloc, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
                          ctypes.c_uint64, U64P, ctypes.c_char_p,
                          ctypes.c_uint32], ctypes.c_int)
bind(lib.kvspaceFree, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                       ctypes.c_uint32], ctypes.c_int)
bind(lib.kvspaceDeltree, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                          ctypes.c_uint32], ctypes.c_int)
bind(lib.kvspaceMkindex, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                          ctypes.c_uint32], ctypes.c_int)
bind(lib.kvspaceListlen, [ctypes.c_void_p, ctypes.c_char_p,
                          ctypes.POINTER(ctypes.c_int64), U32P,
                          ctypes.c_char_p, ctypes.c_uint32], ctypes.c_int)
bind(lib.kvspaceList, [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                       ctypes.c_uint32, U32P, U32P, U64P, ctypes.c_char_p,
                       ctypes.c_uint32], ctypes.c_int)
# 值区基址：offset 只有加上它才是地址。
bind(lib.kvspaceXvalueInProcessAddress, [ctypes.c_void_p], U8P)

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
        self.kv = lib.kvspaceConnect(f"shm://{path}".encode())
        if not self.kv:
            raise RuntimeError(f"connect shm://{path} failed")
        self.base = lib.kvspaceXvalueInProcessAddress(self.kv)
        if not self.base:
            raise RuntimeError("no in-process address space (shm backend expected)")

    def close(self):
        lib.kvspaceClose(self.kv)
        try:
            os.unlink(self.path)
        except FileNotFoundError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def _addr(self, offset):
        # 必须是 base.contents 而不是 base：base 本身是「指向基址的指针变量」，
        # byref(base, off) 偏移的是那个指针变量的地址（栈上），不是它指向的值区。
        # 这个错法不会崩在所有平台上——偏移小的时候恰好落进可读写区，读出来是
        # 垃圾；偏移大就直接段错。用 .contents 才是「基址 + offset」。
        return ctypes.byref(self.base.contents, offset)

    def set(self, key, langtype, body):
        """realloc 出 body+attr 空间，再直接写 base+offset。"""
        lt = langtype.encode() if langtype else b""
        attr_len = 3 + len(lt) if lt else 0
        off = ctypes.c_uint64()
        err = ctypes.create_string_buffer(256)
        rc = lib.kvspaceRealloc(self.kv, key.encode(), len(body), attr_len,
                                ctypes.byref(off), err, 256)
        if rc != 0:
            raise RuntimeError(f"realloc {key}: {err_text(err)}")
        if body:
            ctypes.memmove(self._addr(off.value + KVSPACE_XVALUE_HEAD_LEN),
                           body, len(body))
        if lt:
            a = off.value + KVSPACE_XVALUE_HEAD_LEN + len(body)
            ctypes.memmove(self._addr(a), bytes([KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                                                 len(lt) & 0xFF, len(lt) >> 8]), 3)
            ctypes.memmove(self._addr(a + 3), lt, len(lt))

    def get(self, key):
        """resolve 拿 offset，在 base+offset 处解 head/body/attr。

        返回 (kind, body)；不存在或 wire 不合法返回 None。"""
        off = ctypes.c_uint64()
        err = ctypes.create_string_buffer(256)
        rc = lib.kvspaceResolveOffset(self.kv, key.encode(), 0, ctypes.byref(off),
                                err, 256)
        if rc != 0:
            return None
        wire = ctypes.string_at(self._addr(off.value),
                                KVSPACE_XVALUE_HEAD_LEN)
        attrs_len = int.from_bytes(wire[8:10], "little")
        body_len = int.from_bytes(wire[0:8], "little")
        total = KVSPACE_XVALUE_HEAD_LEN + body_len + attrs_len
        full = ctypes.string_at(self._addr(off.value), total)
        langtype = xvalue_head_scan(full)
        kind = langtype.decode(errors="replace")
        if kind[:1] in ("*", "@"):
            kind = kind[1:]
        if kind.startswith("["):
            kind = kind[kind.index("]") + 1:]
        body = full[KVSPACE_XVALUE_HEAD_LEN:KVSPACE_XVALUE_HEAD_LEN + body_len]
        return kind, body

    def delete(self, key):
        err = ctypes.create_string_buffer(256)
        lib.kvspaceFree(self.kv, key.encode(), err, 256)

    def deltree(self, prefix):
        err = ctypes.create_string_buffer(256)
        lib.kvspaceDeltree(self.kv, prefix.encode(), err, 256)

    def mkindex(self, path):
        err = ctypes.create_string_buffer(256)
        lib.kvspaceMkindex(self.kv, path.encode(), err, 256)

    def list(self, prefix):
        """listlen 给 N/M，list 一次取全部名字（NUL 分隔）。"""
        count = ctypes.c_int64()
        max_name = ctypes.c_uint32()
        err = ctypes.create_string_buffer(256)
        rc = lib.kvspaceListlen(self.kv, prefix.encode(), ctypes.byref(count),
                                ctypes.byref(max_name), err, 256)
        if rc != 0 or count.value <= 0:
            return []
        cap = count.value * (max_name.value + 1)
        buf = ctypes.create_string_buffer(cap)
        length = ctypes.c_uint32()
        listed = ctypes.c_uint32()
        gen = ctypes.c_uint64()
        rc = lib.kvspaceList(self.kv, prefix.encode(), buf, cap,
                             ctypes.byref(length), ctypes.byref(listed),
                             ctypes.byref(gen), err, 256)
        if rc != 0:
            return []
        names = []
        for name in buf.raw[:length.value].split(b"\0"):
            if name:
                names.append(name.decode())
        return names


with KV("/tmp/kvspace_py.shm") as kv:
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
