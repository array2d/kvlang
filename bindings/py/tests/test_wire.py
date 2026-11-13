import ctypes, os, struct, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pathspace
from pathspace import (xv_int, xv_float, xv_str, xv_index, xv_link, xv_ext,
                     xv_langtype, _xv_from_wire, PathSpace,
                     HEAD_LEN, PADDING_LEN, ATTR_TAG_LANGTYPE,
                     BANNED_ATTR_TAG_REALLEN, REF_INLINE, REF_PTR, REF_EXT)
_U8P = ctypes.POINTER(ctypes.c_uint8)
_U8PP = ctypes.POINTER(_U8P)
_U64P = ctypes.POINTER(ctypes.c_uint64)
_CS = ctypes.c_char_p

def c_new(ref, body, langtype=""):
    prefix = "*" if ref == REF_PTR else ("@" if ref == REF_EXT else "")
    return pathspace._build(body, langtype, prefix=prefix)

def c_peek(data):
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
    check("minimal", _xv_from_wire(
        (1).to_bytes(8, "little") + (0).to_bytes(2, "little") + b"\x00" * 6 + b"\x2a"),
        (REF_INLINE, b"\x2a"))

    print("-- ShmSet + readback --")
    d = tempfile.mkdtemp()
    kv = PathSpace(os.path.join(d, "db"))
    kv.set("/x", xv_int(99, "int64"))
    got = kv.get("/x", resolve=False)
    check("shm.nonnull", got is not None, True)
    check("shm.value", _xv_from_wire(got), (REF_INLINE, struct.pack("<q", 99)))
    check("shm.langtype", xv_langtype(got), "int64")
    kv.mkindex("/d/")
    kv.set("/d/a", xv_int(1))
    kv.set("/d/b", xv_str("s"))
    check("shm.list", kv.list("/d/"), ["a", "b"])
    kv.close()
    print(f"\n{_total - _fails}/{_total} passed")
    return 1 if _fails else 0

def test_wire():
    assert main() == 0

if __name__ == "__main__":
    sys.exit(main())
