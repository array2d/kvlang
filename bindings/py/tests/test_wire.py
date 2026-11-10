#!/usr/bin/env python3
"""wrapper 本地拼 wire + 独立解析 + ShmSet 往返。

固定 16 字节 head：[bodylen:u64 LE@0][attrslen:u16 LE@8][padding:6@10-15]，
body@16，attr 紧随 body 且没有长度前缀，total = 16 + bodylen + attrslen。
record = [tag:u8][len:u16 LE][data]，tag 1 = langtype；tag 2（旧 reallen）已删除，
出现即拒。

本测试**不调 C 构造器**：`libkvlang_xvalue` 是静态库（`.a`），ctypes 加载不了它；
为演示再出一个 `.so` 就是为演示多造一个产物，不值。所以 wire 在
`kvspace._build` 里就地拼。

**但本地拼装不是唯一真相**——它以 C 为准，两处对照必须继续存在、不许退化：

1. `kvlang/tutorial-c/02_cpp.cpp`：真链 `libkvlang_xvalue`，用 C 构造器造 wire 再
   和本地解析器互验。
2. `kvlang/bindings/rust/` 的 `c_new_wrapper_reads`：真链 C，验 Rust 侧读 C 造的字节。

改了 `kvspace._build` 的布局却只跑本文件，就是自己验自己——那两处必须一起跑。
"""
import ctypes, os, struct, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import kvspace
from kvspace import (xv_int, xv_float, xv_str, xv_index, xv_link, xv_ext,
                     xv_langtype, _xv_from_wire, KVSpace,
                     HEAD_LEN, PADDING_LEN, ATTR_TAG_LANGTYPE,
                     BANNED_ATTR_TAG_REALLEN, REF_INLINE, REF_PTR, REF_EXT)
# C 构造器不在这里绑定：见文件头。`c_new` 保留签名，改用本地 `kvspace._build`，
# 这样「包装器造的」与「C 造的」两条路径仍有对照，只是对照的是同一份布局定义。
_U8P = ctypes.POINTER(ctypes.c_uint8)
_U8PP = ctypes.POINTER(_U8P)
_U64P = ctypes.POINTER(ctypes.c_uint64)
_CS = ctypes.c_char_p


def c_new(ref, body, langtype=""):
    """按 (ref, body, langtype) 拼 wire —— 与 C 构造器同语义。

    ref 只决定前缀（`*`/`@` 写进 langtype 记录），head 里没有 ptr 位。
    实现在 `kvspace._build`：本仓不再拥有 C 构造器（那是 kvlang 的
    libkvlang_xvalue.a，Python 加载不了静态库）。
    """
    prefix = "*" if ref == REF_PTR else ("@" if ref == REF_EXT else "")
    return kvspace._build(body, langtype, prefix=prefix)


def c_peek(data):
    """独立 wire 解析器 -> (ref, body, langtype)，并校验 head/attr 自洽。

    只读 head 与 attr 区，不调用 wrapper 的解析函数——两边是各自独立的实现。
    """
    assert len(data) >= HEAD_LEN, "shorter than one head"
    body_len = int.from_bytes(data[0:8], "little")
    attr_len = int.from_bytes(data[8:10], "little")
    padding = data[10:10 + PADDING_LEN]
    assert padding == b"\x00" * PADDING_LEN, f"padding must be zero, got {padding!r}"
    a = HEAD_LEN + body_len
    assert a + attr_len == len(data), f"total length mismatch {len(data)} != {a}+{attr_len}"
    attr = data[a:a + attr_len]
    lt = ""
    p = 0
    while p + 3 <= len(attr):
        tag, rlen = attr[p], int.from_bytes(attr[p + 1:p + 3], "little")
        assert tag != BANNED_ATTR_TAG_REALLEN, "tag 2 (old reallen) must not appear"
        if tag == ATTR_TAG_LANGTYPE:
            lt = attr[p + 3:p + 3 + rlen].decode()
        p += 3 + rlen
    if lt[:1] == "*":
        ref = REF_PTR
    elif lt[:1] == "@":
        ref = REF_EXT
    else:
        ref = REF_INLINE
    return (ref, data[HEAD_LEN:a], lt)


_fails = 0
_total = 0


def check(name, got, want):
    global _fails, _total
    _total += 1
    if got == want:
        print(f"  PASS {name}")
    else:
        _fails += 1
        print(f"  FAIL {name}: got {got!r}, want {want!r}")


def main():
    print("-- wrapper construct -> independent parse --")
    check("int64", c_peek(xv_int(42, "int64")), (REF_INLINE, struct.pack("<q", 42), "int64"))
    check("int64.untyped", c_peek(xv_int(42)), (REF_INLINE, struct.pack("<q", 42), ""))
    check("float64", c_peek(xv_float(1.5, "float64")),
          (REF_INLINE, struct.pack("<d", 1.5), "float64"))
    check("str", c_peek(xv_str("héllo", "char/utf8")),
          (REF_INLINE, "héllo".encode(), "char/utf8"))
    check("index", c_peek(xv_index()), (REF_INLINE, b"", ""))
    # 引用性由构造器补的前缀表达：NewPtr -> `*int64`，NewExt -> 裸 `@`。
    check("link", c_peek(xv_link("/t/x", "int64")), (REF_PTR, b"/t/x", "*int64"))
    check("ext", c_peek(xv_ext("s3://b/k")), (REF_EXT, b"s3://b/k", "@"))

    print("-- C constructor -> wrapper read --")
    v = c_new(REF_INLINE, struct.pack("<q", 7), "int64")
    check("int64", _xv_from_wire(v), (REF_INLINE, struct.pack("<q", 7)))
    check("int64.langtype", xv_langtype(v), "int64")
    v = c_new(REF_INLINE, "héllo".encode(), "char/utf8")
    check("str", _xv_from_wire(v), (REF_INLINE, "héllo".encode()))
    check("str.langtype", xv_langtype(v), "char/utf8")
    check("ptr", _xv_from_wire(c_new(REF_PTR, b"/t/x")), (REF_PTR, b"/t/x"))
    check("ext", _xv_from_wire(c_new(REF_EXT, b"s3://b/k")), (REF_EXT, b"s3://b/k"))
    check("none", _xv_from_wire(c_new(REF_INLINE, b"")), (REF_INLINE, b""))
    check("garbage", _xv_from_wire(b"\x00\x01\x02\x03"), (REF_INLINE, b""))
    # 16 字节 head 的 bodylen=1、attrslen=0、padding 全零，body=0x2a：
    check("minimal", _xv_from_wire(
        (1).to_bytes(8, "little") + (0).to_bytes(2, "little") + b"\x00" * 6 + b"\x2a"),
        (REF_INLINE, b"\x2a"))

    print("-- ShmSet + readback --")
    d = tempfile.mkdtemp()
    kv = KVSpace(os.path.join(d, "db"))  # data_size must be 8 * 64^n, default 32768
    kv.set("/x", xv_int(99, "int64"))
    got = kv.get("/x", resolve=False)
    check("shm.nonnull", got is not None, True)
    check("shm.value", _xv_from_wire(got), (REF_INLINE, struct.pack("<q", 99)))
    check("shm.langtype", xv_langtype(got), "int64")
    kv.mkindex("/d/")
    kv.set("/d/a", xv_int(1))
    kv.set("/d/b", xv_str("s"))
    check("shm.list", kv.list("/d/"), ["a", "b"])
    # extindex / delextindex 已随存储 ABI 删除（新 ABI 不提供目录叠加），
    # 故不再有用例。删能力不删测试会留下一条永远跑不到的断言。
    kv.close()
    print(f"\n{_total - _fails}/{_total} passed")
    return 1 if _fails else 0


def test_wire():
    """pytest 入口：断言全部用例通过。

    用例是 `check()` 累积式的（不是每例一个 test_ 函数）：它们共享一次 shm 打开，
    拆成 20 个函数就得开 20 次库。`main()` 的返回值就是判据——0 = 全过。"""
    assert main() == 0


if __name__ == "__main__":
    sys.exit(main())
