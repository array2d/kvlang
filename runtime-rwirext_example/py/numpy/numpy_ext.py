"""numpy 扩展：把 numpy ndarray 的 buffer 建立在 kvspace-c SHM 上（零拷贝）。

kvspace 存 tensor 为 XValue 数组（float64/int64 等）。`tensor_view` 直接解析
TLV 头拿到 SHM 里 raw data 的地址，用 numpy 在该地址上建立 ndarray——读写 ndarray
即读写 kvspace，无拷贝。
"""

import ctypes
import struct

import numpy as np

_lib = None

_KIND_CTYPE = {
    "float64": ctypes.c_double,
    "float32": ctypes.c_float,
    "int64": ctypes.c_int64,
    "int32": ctypes.c_int32,
    "int16": ctypes.c_int16,
    "int8": ctypes.c_int8,
    "uint64": ctypes.c_uint64,
    "uint32": ctypes.c_uint32,
    "uint16": ctypes.c_uint16,
    "uint8": ctypes.c_uint8,
}

_NP_DTYPE = {
    "float64": np.float64,
    "float32": np.float32,
    "int64": np.int64,
    "int32": np.int32,
    "int16": np.int16,
    "int8": np.int8,
    "uint64": np.uint64,
    "uint32": np.uint32,
    "uint16": np.uint16,
    "uint8": np.uint8,
}


def _load():
    global _lib
    if _lib is None:
        _lib = ctypes.CDLL("libkvspace-c.so")
        _lib.kvsc_open.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
        _lib.kvsc_open.restype = ctypes.c_void_p
        _lib.kvsc_close.argtypes = [ctypes.c_void_p]
        _lib.kvsc_get.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int32)]
        _lib.kvsc_get.restype = ctypes.POINTER(ctypes.c_uint8)
        _lib.kvsc_set.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int32]
        _lib.kvsc_set.restype = ctypes.c_int
        _lib.kvsc_del.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        _lib.kvsc_del.restype = ctypes.c_int
    return _lib


class NumpyExt:
    """kvspace-c SHM 上的 numpy tensor 容器。"""

    def __init__(self, dsn: str):
        if not dsn.startswith("shm://"):
            raise ValueError(f"numpy 扩展走 shm 后端（大 data 零拷贝），got {dsn}")
        self.path = dsn[len("shm://"):]
        self.lib = _load()
        # data_size = 8 * 64^4 = 128MB，与 kvlang runtime 默认一致
        self.kv = self.lib.kvsc_open(self.path.encode(), 8 * 64 ** 4)
        if not self.kv:
            raise RuntimeError(f"kvsc_open failed: {self.path}")

    def close(self):
        if self.kv:
            self.lib.kvsc_close(self.kv)
            self.kv = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # ---- tensor 写入 ----

    def tensor_alloc(self, key: str, arr: np.ndarray):
        """把 numpy 数组写到 kvspace（float64/int64 等定长数组）。"""
        arr = np.ascontiguousarray(arr)
        kind = _dtype_to_kind(arr.dtype)
        raw = arr.tobytes()
        n = arr.size
        if n > 1:
            tlv = (struct.pack("<B", len(kind)) + kind.encode()
                   + b"\x00\x01\x01" + struct.pack("<I", n)
                   + struct.pack("<I", len(raw)) + raw)
        else:
            tlv = (struct.pack("<B", len(kind)) + kind.encode()
                   + b"\x00\x00\x00" + struct.pack("<I", len(raw)) + raw)
        buf = ctypes.create_string_buffer(tlv, len(tlv))
        rc = self.lib.kvsc_set(self.kv, key.encode(),
                               ctypes.cast(buf, ctypes.POINTER(ctypes.c_uint8)), len(tlv))
        if rc != 0:
            raise RuntimeError(f"kvsc_set failed: {key}")

    # ---- tensor 零拷贝视图 ----

    def tensor_view(self, key: str) -> np.ndarray | None:
        """零拷贝视图：返回的 ndarray 的 buffer 直接指向 kvspace SHM 的 raw data。"""
        out_len = ctypes.c_int32()
        d = self.lib.kvsc_get(self.kv, key.encode(), 1, ctypes.byref(out_len))
        if not d:
            return None
        kl = d[0]
        kind = bytes(d[1:1 + kl]).decode()
        o = 1 + kl
        ref = d[o]
        arr_flag = d[o + 1]
        ndim = d[o + 2]
        if ref != 0 or kind not in _KIND_CTYPE:
            return None
        raw_off = o + 3 + 4 * ndim
        raw_len = int.from_bytes(bytes(d[raw_off:raw_off + 4]), "little")
        base = ctypes.cast(d, ctypes.c_void_p).value
        elem = _KIND_CTYPE[kind]
        n = raw_len // ctypes.sizeof(elem)
        ptr = ctypes.cast(base + raw_off + 4, ctypes.POINTER(elem))
        if arr_flag == 0:
            shape = ()
        else:
            shape = tuple(
                int.from_bytes(bytes(d[o + 3 + 4 * i: o + 3 + 4 * i + 4]), "little")
                for i in range(ndim)
            )
        arr = np.ctypeslib.as_array(ptr, shape=(n,))
        return arr.reshape(shape) if shape else arr[0]


def _dtype_to_kind(dt) -> str:
    for kind, npdt in _NP_DTYPE.items():
        if np.dtype(dt) == np.dtype(npdt):
            return kind
    raise ValueError(f"unsupported dtype: {dt}")
