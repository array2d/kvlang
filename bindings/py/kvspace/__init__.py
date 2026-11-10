"""
kvspace Python wrapper — ctypes FFI over libkvspace (.so / .dylib)

**只用公开 12 存储符号**：Connect / Close / XvalueInProcessAddress / Resolve /
Realloc / Free / Cp / Mkindex / Deltree / Cpdir / Listlen / List。

原先这里直接调 `kvspaceShm*` 内部引擎符号（Open/Get/Set/Del/List/...）。单库化后
那些符号在生产配置下已被 `-fvisibility=hidden` 隐藏——它们是实现细节，不是 API。
依赖内部符号的 wrapper 每次重构都要跟着改，而依赖 12 符号的不会。

wire 在本包内就地拼（L1 示例）：`libkvlang_xvalue` 是静态库，ctypes 加载不了 `.a`，
为演示再出一个 .so 就是为演示多造一个产物。真链 C 的示例在 kvlang/tutorial-c/
与 kvlang/bindings/rust/。
"""

import ctypes, os, struct, sys
from pathlib import Path
from typing import Optional

_EXT = ".dylib" if sys.platform == "darwin" else ".so"
# bindings/py/kvspace/__init__.py → 上三级 = kvlang 仓根
_KVLANG_ROOT = Path(__file__).resolve().parent.parent.parent.parent


# 库句柄：**由调用方显式配置**，不读任何环境变量（全生态只允许 `KVSPACE` 一个）。
# 环境变量是隐式输入——不进命令行、不进日志，两个人跑同一条命令可能结果不同。
_lib = None
_KVSPACE_LIB = Path("")


def configure(lib_directory) -> None:
    """加载 libkvspace。使用本包前必须先调用；路径由调用方给，不猜兄弟目录。"""
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


# 构造器所在库：**不是** libkvspace，是 kvlang 的 libkvlang_xvalue（TASK-082 迁走）。
# 两个库都按环境变量定位，默认名不同——构造器永远不会出现在 libkvspace 里，所以这里
# 没有「未配置就回落到主库」这一支：回落只会得到一堆 undefined symbol。
# 注意：这里 CDLL 的是**静态库** libkvlang_xvalue.a —— ctypes 不能加载 `.a`。
# 语言层示例（本包）因此不复用 C 构造器，而是本地拼 wire（见下方 `_KVSPACE_LANGTYPE_TAG`）；
# 需要真构造器的测试用 C 侧（tutorial-c/）或 rust（bindings/rust/）。
# 构造器库路径：测试会自己再 CDLL 一次以独立绑定 argtypes（不复用本模块的绑定，
# 那样测的是「绑定对不对」而不是「wire 对不对」）。
# codec 产出为 frontend malloc 缓冲，拷出后以 libc free 释放。
_libc = ctypes.CDLL(None)
_libc.free.argtypes = [ctypes.c_void_p]
_libc.free.restype = None


def _bind(fn, argtypes, restype):
    fn.argtypes = argtypes
    fn.restype = restype


_U8P = ctypes.POINTER(ctypes.c_uint8)
_U8PP = ctypes.POINTER(_U8P)
_U64P = ctypes.POINTER(ctypes.c_uint64)

# ── 公开 12 存储符号 ──────────────────────────────────────────────
# 单库就是标准实现：没有 adapter、没有 dlopen、没有内部引擎符号可调。
# 读写模型见 docs：Resolve 给偏移，InProcessAddress 给基址，值就在
# base+offset 起的那段内存里；写走 Realloc（head 由它写，调用方填 body/attr）。
def _bind_all():
    _bind(_L().kvspaceConnect, [ctypes.c_char_p], ctypes.c_void_p)
    _bind(_L().kvspaceClose, [ctypes.c_void_p], None)
    _bind(_L().kvspaceXvalueInProcessAddress, [ctypes.c_void_p], _U8P)
    _bind(_L().kvspaceResolve,
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



# kvspace_status_t（负数 errno，不是 0/-1 哨兵）
KVSPACE_OK = 0
KVSPACE_MISSING = -2   # -ENOENT

# ── wire 布局与本包定位 ─────────────────────────────────────────────
# 固定 16 字节 head：[bodylen:u64 LE@0][attrslen:u16 LE@8][padding:6@10-15]，
# body@16，attr 紧随 body 且**没有**长度前缀（attrslen 在 head 里）。
# record = [tag:u8][len:u16 LE][data]，tag 1 = langtype；tag 2（旧 reallen）
# 已删除，出现即拒。
#
# **本包是语言层示例（L1）**：wire 在这里就地拼，不调 C 构造器。理由不是省事——
# `libkvlang_xvalue` 是静态库（`.a`），ctypes 加载不了它；为示例再出一个 .so 就是
# 为演示多造一个产物。要验「C 与本地拼的字节一致」，看 tutorial-c/ 与
# bindings/rust/（它们真链 C）。


HEAD_LEN = 16
PADDING_LEN = 6
ATTR_TAG_LANGTYPE = 1
# 旧 wire 的 tag 2（reallen）：已删除，出现即拒。
BANNED_ATTR_TAG_REALLEN = 2

# 引用性：head 里没有 ptr 位，由 langtype 首字节的 `*` / `@` 前缀表达。
REF_INLINE, REF_PTR, REF_EXT = 0, 1, 2


# ── XValue helpers ────────────────────────────────────────────────
# Construct via C constructors (_take copies and frees malloc buffer); parse header + attr locally.
# Empty langtype = untyped (empty attr).


def _take(out, total: ctypes.c_uint64) -> bytes:
    data = ctypes.string_at(out, total.value)
    _libc.free(out)
    return data


def _build(body: bytes, langtype: str = "", prefix: str = "") -> bytes:
    """拼一条 wire：16 字节 head + body + 可选一条 langtype attr 记录。

    `prefix` 非空时写在 langtype 前（`*` 指针 / `@` 外部引用）——这两个前缀是
    「这个值是引用」的唯一表达，head 里没有对应字段。"""
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
    """校验并切分 wire -> (body, attr)；畸形返回 None。

    判据全部来自 head 本身，没有第二个长度来源：
      - 长度够一个 head；padding 10-15 全零；16+bodylen+attrslen == len(data)；
      - attr 区逐条 record 自洽，且不含已删除的 tag 2（旧 reallen）。"""
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
    """attr 区里 tag 1 的原始字节；没有就是空。"""
    p = 0
    while p + 3 <= len(attr):
        tag, rlen = attr[p], int.from_bytes(attr[p + 1:p + 3], "little")
        if tag == ATTR_TAG_LANGTYPE:
            return attr[p + 3:p + 3 + rlen]
        p += 3 + rlen
    return b""


def _xv_from_wire(data: Optional[bytes]) -> tuple[int, bytes]:
    """读 wire -> (ref, body)；畸形返回 (REF_INLINE, b"")。

    ref 由 langtype 首字节决定（head 里没有 ptr 位）。"""
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
    """读 attr tag 1（langtype）；无类型或畸形返回 ""。"""
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
    """Directory value (empty body, untyped)."""
    return _xv_scalar(b"", "")


def xv_link(target: str, langtype: str = "") -> bytes:
    """Pointer value：body=target key，`*` 前缀进 langtype（"" = 无约束指针）。"""
    if langtype.startswith(("*", "@")):
        raise ValueError("langtype 已带引用前缀：双重前缀说明两处对格式理解不一致")
    return _build(target.encode(), langtype, prefix="*")


def xv_ext(locator: str, langtype: str = "") -> bytes:
    """External locator：body=locator，`@` 前缀进 langtype。"""
    if not locator:
        raise ValueError("ext 的 locator 不得为空")
    if langtype.startswith(("*", "@")):
        raise ValueError("langtype 已带引用前缀：双重前缀说明两处对格式理解不一致")
    return _build(locator.encode(), langtype, prefix="@")


# ── KVSpace ─────────────────────────────────────────────────────

class KVSpace:
    """shm KVSpace 句柄。

    `path` 是 store 文件路径；DSN 形如 `shm://<path>`。

    读写模型与旧 wrapper 不同：不再有 `kvspaceShmGet/Set` 这类整值接口，
    而是 **Resolve 拿偏移 → 直接在基址+偏移上读写 wire**。
    """

    def __init__(self, path: str, data_size: int = 32768):
        # data_size 保留在签名里但不再生效：单库后 store 扩容由引擎自己按需做，
        # 调用方不再指定初始容量（旧 kvspaceShmOpen 的第二参已不存在）。
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

    # ── wire 读写 ────────────────────────────────────────────

    def _view(self, key: str) -> Optional[bytes]:
        """读该 key 的完整 wire（拷贝）；不存在返回 None。"""
        off = ctypes.c_uint64(0)
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceResolve(self._kv, key.encode(), 0,
                                 ctypes.byref(off), err, len(err))
        if rc != KVSPACE_OK:
            return None
        base = _L().kvspaceXvalueInProcessAddress(self._kv)
        self._base = base
        # head 的 bodylen/attrslen 决定整条 wire 长度；读 16 字节头先算总长。
        head = ctypes.string_at(ctypes.cast(base, ctypes.c_void_p).value + off.value,
                                HEAD_LEN)
        body_len = int.from_bytes(head[0:8], "little")
        attr_len = int.from_bytes(head[8:10], "little")
        return ctypes.string_at(
            ctypes.cast(base, ctypes.c_void_p).value + off.value,
            HEAD_LEN + body_len + attr_len)

    def _write(self, key: str, wire: bytes):
        """写整条 wire：Realloc 写 head 并交出位置，本层只拷 body 与 attr。"""
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
        # head 由 Realloc 写好；只填 body@+16 与 attr@+16+bodylen。
        if body:
            ctypes.memmove(addr + HEAD_LEN, body, len(body))
        if attr:
            ctypes.memmove(addr + HEAD_LEN + len(body), attr, len(attr))

    # ── CRUD ─────────────────────────────────────────────────

    def get(self, key: str, resolve: bool = True) -> Optional[bytes]:
        # resolve 参数保留兼容：公开 ABI 的 Resolve **不跟 Ptr**，故两者同义。
        del resolve
        return self._view(key)

    def set(self, key: str, val: bytes):
        self._write(key, val)

    def delete(self, key: str):
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceFree(self._kv, key.encode(), err, len(err))
        # 删不存在的键不算错误（与旧 Del 一致）。
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

    # ── Directory ────────────────────────────────────────────

    def mkindex(self, path: str, capacity: int = 0):
        # capacity 随 ABI 删除（容量是存储层的物理分配属性，不进 wire）。
        del capacity
        err = ctypes.create_string_buffer(256)
        rc = _L().kvspaceMkindex(self._kv, path.encode(), err, len(err))
        if rc != KVSPACE_OK:
            raise RuntimeError(err.value.decode() or f"mkindex({path}) failed")

    def list(self, prefix: str, resolve: bool = True) -> list[str]:
        """直接子项名。**顺序是后端顺序，不排序**（排序在上层做）。"""
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
