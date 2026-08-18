"""numpy rwir 扩展：注册 numpy.add/mul，serve handoff，ndarray 零拷贝。

- 手递协议复用 C runtime 的 rwext_* ABI（rwext_connect/register/list/get/set/del/params/next_pc）。
- tensor 数据本体用 numpy_ext.NumpyExt（kvspace-c SHM 零拷贝 view）。
"""

import ctypes
import time

from numpy_ext import NumpyExt

_rwext = ctypes.CDLL("libkvlang_runtime.so")
_rwext.rwext_connect.argtypes = [ctypes.c_char_p]
_rwext.rwext_connect.restype = ctypes.c_void_p
_rwext.rwext_register.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_char_p]
_rwext.rwext_register.restype = ctypes.c_int
_rwext.rwext_list.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_rwext.rwext_list.restype = ctypes.c_void_p
_rwext.rwext_get.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_rwext.rwext_get.restype = ctypes.c_void_p
_rwext.rwext_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
_rwext.rwext_set.restype = ctypes.c_int
_rwext.rwext_del.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_rwext.rwext_del.restype = ctypes.c_int
_rwext.rwext_params.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_rwext.rwext_params.restype = ctypes.c_void_p
_rwext.rwext_next_pc.argtypes = [ctypes.c_char_p]
_rwext.rwext_next_pc.restype = ctypes.c_void_p

_libc = ctypes.CDLL(None)
_libc.free.argtypes = [ctypes.c_void_p]

OPS = {
    "numpy.add": (2, 1),
    "numpy.mul": (2, 1),
}


def _s(p):
    if not p:
        return ""
    s = ctypes.string_at(p).decode()
    _libc.free(p)
    return s


def serve(dsn: str):
    kv = NumpyExt(dsn)  # 零拷贝 buffer（kvspace-c SHM）
    conn = _rwext.rwext_connect(dsn.encode())
    if not conn:
        raise RuntimeError("rwext_connect failed")

    for op, (nr, nw) in OPS.items():
        sig = "\n".join(["any"] * (nr + nw))
        _rwext.rwext_register(conn, op.encode(), nr, nw, sig.encode())

    while True:
        for op, (nr, nw) in OPS.items():
            base = f"/lib/{op}"
            children = _s(_rwext.rwext_list(conn, f"{base}/".encode()))
            for child in children.split("\n"):
                if not child.startswith(".todo<") or not child.endswith(">"):
                    continue
                vid = child[6:-1]
                todo = f"{base}/{child}"
                pcid = _s(_rwext.rwext_get(conn, todo.encode()))
                pc, _, pid = pcid.rpartition("|")

                params = _s(_rwext.rwext_params(conn, pc.encode())).split("\n")
                opcode = params[0]
                reads = params[1:1 + nr]
                writes = params[1 + nr:1 + nr + nw]

                a = kv.tensor_view(reads[0])
                b = kv.tensor_view(reads[1])
                if opcode == "numpy.add":
                    c = a + b
                elif opcode == "numpy.mul":
                    c = a * b
                else:
                    continue
                kv.tensor_alloc(writes[0], c)

                nxt = _s(_rwext.rwext_next_pc(pc.encode()))
                _rwext.rwext_set(conn, f"/vthread/{vid}/‥pc".encode(), nxt.encode())
                _rwext.rwext_set(conn, f"{base}/.done<{vid}>".encode(), pid.encode())
                _rwext.rwext_del(conn, todo.encode())
        time.sleep(0.05)


if __name__ == "__main__":
    import sys
    serve(sys.argv[1] if len(sys.argv) > 1 else "shm:///tmp/numpy_rwext")
