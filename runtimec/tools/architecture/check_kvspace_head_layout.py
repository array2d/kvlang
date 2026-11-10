#!/usr/bin/env python3
"""判 kvspace 的 XValue head 是 8 字节还是 16 字节布局。

为什么需要它：`lib/` 里可能同时存在新旧两份产物，装错位置或加载到旧的那份时，
真后端用例会红，但**红的原因不是代码错**。先判库，再判代码。

为什么必须判两份：**布局由后端决定，不是前端**。`libkvspace.so` 只是个转发壳
（`kvspace/src/frontend.c`），真正造 wire / 读 head 的是 `libkvspace-c`（shm）
或 `libkvspace_durable`。前端重建、后端没重建时，探针只看前端会给出「16 字节，
没问题」的假结论——这条是用一次完整的红换来的。

判据不看符号是否存在（两版都有 `kvlangXvalueHeadNewSlack`），而是直接造一个值
看字节：

    8 字节布局 : [len:u48][storetype][ptr]  body  [attr_len:u16][records]
    16 字节布局: [bodylen:u64][attrslen:u16][padding:6]  body  [records]

后端查找顺序的**依据是规范条款** `workspace/622/routing/README.md` §十
「后端查找顺序（规范性条款）」，不是 `frontend.c` 的实现：

    1. <前端目录>/<soname>
    2. <前端目录>/kvspace/<soname>
    3. <前端目录>/../kvspace/<soname>
    4. /usr/lib/kvspace/<soname>（macOS: /usr/local/lib/kvspace/<soname>）

第一条存在者即所用。

**为什么依据是条款而不是实现**：先前本文件抄 `frontend.c:backend_paths`，于是实现与
探针「一致」——但两边同时错也无人能发现，那是自证而不是独立真值。改成对照条款后，
`frontend.c` 是实现、本文件是校验，两者不一致即漂移；两者一致但都违反条款，由规范
检查发现。

**已知边界**：条款本身仍需人核（文字不会自己发现错误）。本工具不解析 C 源码来自动比对
实现与条款——那条路太脆，且会把「解析失败」伪装成「实现合规」。

用法:
    python3 tools/check_kvspace_head_layout.py [--frontend PATH] [--soname NAME]
    python3 tools/check_kvspace_head_layout.py --selftest

默认前端 $HOME/github.com/array2d/kvlang/lib/libkvspace.so，默认 soname
libkvspace-c.so.1（shm 后端，即默认后端）。

退出码：0 = 前端与后端都是 16 字节；1 = 至少一个仍是 8 字节（真后端必红，红得对）；
2 = 判不出（缺文件 / 构造失败 / 布局无法识别）。

`--selftest` 另有一组语义：它检验的是**本工具**，不是被探的库。0 = 自测通过，
非零 = 自测失败；编译不出 stub 库同样算自测失败，因为那是「工具跑不了」，不是
「布局判不出」，不能伪装成 exit 2。
"""

from __future__ import annotations

import contextlib
import ctypes
import io
import os
import shutil
import subprocess
import sys
import tempfile

EXPECTED_BODY = 2
# `body_cap` 是 `kvspaceWriteNewPlace` 的**物理分配**参数：wire 上不表达容量，所以
# 探针用 body_cap > body_len 写一条，再看 wire 里的 bodylen 到底是 2 还是 80。
# 这是把「wire 仍带容量」的旧后端抓出来的唯一手段——构造器已无 capacity 参数。
PROBE_CAPACITY = 80
PROBE_LANGTYPE = "int64"
LANGTYPE_RECORD_BYTES = 3 + len(PROBE_LANGTYPE)  # tag + u16 len + data
REALLEN_RECORD_BYTES = 11  # tag + u16 len(=8) + u64 value（旧格式的 tag 2）
ZERO_PADDING = b"\x00" * 6  # head offset 10-15：恒为零

RESULT_SIXTEEN = "16-byte"
RESULT_SIXTEEN_LAYOUT_BYTE = "16-byte-layout-byte"
RESULT_SIXTEEN_CAPACITY_FORM = "16-byte-capacity-form"
RESULT_EIGHT = "8-byte"
RESULT_UNKNOWN = "unknown"
RESULT_SKIPPED = "skipped"

EXPECTED_HEAD_LEN = 16
LEGACY_HEAD_LEN = 8


def classify_wire(data: bytes) -> tuple[str, str]:
    """按字节判一条 wire 的 head 是哪一代。返回 (verdict, detail)。

    **判据只有一处**：`probe_library`（构造器造的 wire）与 `probe_backend`（后端
    读回的 wire）走同一段分类，否则两处判据迟早对「尾部残余字节」这类边角判不同。

    现行头：[bodylen:u64][attrslen:u16][padding:6]，bodylen 恒为有效字节数，
    没有 tag 2。上一代头有两处可辨认的痕迹，都比现行头多东西：
      - offset 12 是 `layout` 字节而不是 padding 的一部分；
      - bodylen 是容量（> 有效长度），且带 tag 2 reallen。
    """
    if len(data) < EXPECTED_HEAD_LEN:
        return RESULT_UNKNOWN, f"wire too short: {len(data)} bytes"
    bodylen = int.from_bytes(data[0:8], "little")
    attrslen = int.from_bytes(data[8:10], "little")

    # 旧 8 字节布局：head 8 + body + u16 attr 长度 + tag 记录。
    if (int.from_bytes(data[0:6], "little") == EXPECTED_BODY and
            len(data) == LEGACY_HEAD_LEN + EXPECTED_BODY + 2 +
            LANGTYPE_RECORD_BYTES):
        return RESULT_EIGHT, (f"u48 len={EXPECTED_BODY} storetype={data[6]} "
                              f"pointer={data[7]} total={len(data)}")

    if len(data) != EXPECTED_HEAD_LEN + bodylen + attrslen:
        return RESULT_UNKNOWN, (
            f"bodylen={bodylen} attrslen={attrslen} total={len(data)} "
            "matches no known layout")

    # 上一代：bodylen 是容量，有效长度在 attr tag 2。
    if bodylen != EXPECTED_BODY:
        reallen = data[EXPECTED_HEAD_LEN + bodylen + LANGTYPE_RECORD_BYTES:
                       EXPECTED_HEAD_LEN + bodylen + LANGTYPE_RECORD_BYTES +
                       REALLEN_RECORD_BYTES]
        has_tag2 = (len(reallen) == REALLEN_RECORD_BYTES and reallen[0] == 2 and
                    reallen[1] == 8 and reallen[2] == 0)
        if has_tag2:
            return RESULT_SIXTEEN_CAPACITY_FORM, (
                f"bodylen={bodylen} is a capacity, reallen="
                f"{int.from_bytes(reallen[3:11], 'little')} in attr tag 2")

    # 上一代：offset 12 是 layout 字节。现行头该位置属于 padding，必须为零，
    # 而 layout 在「容量 == 长度」时为 0——所以只能靠 bodylen 恒等来判，
    # 单看 offset 12 会假绿。这里只在既有两代格式都对不上时才用它指认。
    if data[10:16] != ZERO_PADDING:
        if (data[10:12] == b"\x00\x00" and data[12] in (0, 1) and
                data[13:16] == b"\x00\x00\x00"):
            return RESULT_SIXTEEN_LAYOUT_BYTE, (
                f"head still carries the removed `layout` byte "
                f"(layout={data[12]}, bodylen={bodylen})")
        return RESULT_UNKNOWN, (f"padding is not zero: {data[10:16].hex(' ')}")

    if (bodylen == EXPECTED_BODY and
            attrslen == LANGTYPE_RECORD_BYTES):
        return RESULT_SIXTEEN, (f"bodylen={bodylen} attrslen={attrslen} "
                                f"padding zero, no tag 2, total={len(data)}")
    return RESULT_UNKNOWN, (f"bodylen={bodylen} attrslen={attrslen} "
                            f"total={len(data)} matches no known layout")


def probe_library(library_path: str) -> str:
    """构造一条值、按字节判 head 代次。返回 RESULT_*，并打印证据。

    构造器已无 capacity 参数，所以这条只判「16 vs 8」与「padding 是否为零」；
    「bodylen 是不是容量」那条判据只有后端写路径能触发（见 `probe_backend`）。
    """
    try:
        library = ctypes.CDLL(library_path)
        new_slack = library.kvlangXvalueHeadNewSlack
    except (OSError, AttributeError) as error:
        print(f"  cannot load or bind: {error}")
        return RESULT_UNKNOWN
    new_slack.restype = ctypes.c_int
    new_slack.argtypes = [
        ctypes.c_char_p, ctypes.c_uint64, ctypes.c_char_p,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_uint64),
    ]
    wire = ctypes.POINTER(ctypes.c_uint8)()
    wire_size = ctypes.c_uint64(0)
    status = new_slack(
        b"ab", EXPECTED_BODY, PROBE_LANGTYPE.encode(),
        ctypes.byref(wire), ctypes.byref(wire_size))
    if status != 0 or wire_size.value == 0:
        print(f"  cannot construct a probe value (status={status})")
        return RESULT_UNKNOWN

    total = int(wire_size.value)
    data = bytes(wire[index] for index in range(total))
    print(f"  wire: {total} bytes: {data.hex(' ')}")
    verdict, detail = classify_wire(data)
    print(f"  layout: {verdict} ({detail})")
    return verdict


def backend_candidates(frontend_path: str, soname: str) -> list[str]:
    """复刻 frontend.c 的 backend_paths：四条候选，按序。"""
    directory = os.path.dirname(os.path.abspath(frontend_path))
    candidates = [
        os.path.join(directory, soname),
        os.path.join(directory, "kvspace", soname),
        os.path.join(directory, "..", "kvspace", soname),
    ]
    if sys.platform == "darwin":
        candidates.append(os.path.join("/usr/local/lib/kvspace", soname))
    else:
        candidates.append(os.path.join("/usr/lib/kvspace", soname))
    return candidates


# ---------------------------------------------------------------------------------------
# 后端覆盖：head 布局由**后端**决定，前端只是个转发壳。只探 shm 会让「durable 还是 8 字节」
# 在验收阶段才暴露。这里对每个能被前端装载的后端各写一个值、读回原始字节判布局。
#
# 判据是 `kvspaceGet` 读回的**整条 wire 的字节**，不是某个后端的私有导出，也不是
# `headlen` 字段：后者来自 `kvspaceGetHead` + 一个手抄的 head 结构体，那一对已按
# `kvspace/include/kvspace/kvspace.h` 删除（手抄结构体是第二份字段真相，字段一改副本
# 不跟着变）。`kvspaceGet` 是两个后端都实现的 C ABI，返回值含 head，足够判布局。
# ---------------------------------------------------------------------------------------
# DSN 里给的是**文件路径**不是目录：shm 后端会自己拼 `.sbo.head` 等后缀，
# 直接传一个已存在的目录会被 O_RDWR 打开成功、随后格式校验失败。
BACKEND_CASES = (
    ("shm", "shm://{store}/store"),
    ("fs", "fs://{store}/store"),
)


def _load_frontend(frontend_path: str):
    library = ctypes.CDLL(frontend_path)
    library.kvspaceConnect.restype = ctypes.c_void_p
    library.kvspaceConnect.argtypes = [ctypes.c_char_p]
    library.kvspaceClose.restype = None
    library.kvspaceClose.argtypes = [ctypes.c_void_p]
    library.kvspaceWriteNewPlace.restype = ctypes.c_int
    library.kvspaceWriteNewPlace.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint8, ctypes.c_uint8,
        ctypes.c_uint32, ctypes.c_uint64, ctypes.c_char_p,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)), ctypes.c_char_p,
        ctypes.c_uint32]
    library.kvspaceGet.restype = ctypes.c_int
    library.kvspaceGet.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
        ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_uint32)]
    return library


def probe_backend(frontend_path: str, scheme: str, dsn_template: str,
                  backend_path: str | None) -> tuple[str, str]:
    """Write one value through the frontend and read the raw wire back.

    值不是裸 body：后端会把它当 wire 解析并按各自 head 布局校验。所以**让后端自己造**
    ——用 `kvspaceWriteNewPlace`，这是两个后端都实现的 C ABI 入口。

    **判据是读回的原始字节，不是 `headlen` 字段。**
    原先用 `kvspaceGetHead` 的 `headlen`（8 或 16）——那个「解码结构体 + 取头函数」
    已按 `kvspace/include/kvspace/kvspace.h` 删除（手抄结构体是第二份字段真相）。
    现在用 `kvspaceGet` 拿整条 wire，直接按字节判布局，与 `probe_library` 同一套判据。
    """
    if backend_path is None:
        return RESULT_SKIPPED, "backend library is not built"
    try:
        library = _load_frontend(frontend_path)
    except OSError as error:
        return RESULT_UNKNOWN, f"cannot load the frontend: {error}"
    os.environ["KVSPACE_SELFTEST_BACKEND"] = backend_path or ""
    with tempfile.TemporaryDirectory(prefix=f"kvspace-{scheme}-") as store:
        dsn = dsn_template.format(store=store)
        handle = library.kvspaceConnect(dsn.encode())
        if not handle:
            return RESULT_UNKNOWN, f"cannot connect with {dsn!r}"
        try:
            error_buffer = ctypes.create_string_buffer(256)
            body = ctypes.POINTER(ctypes.c_uint8)()
            status = library.kvspaceWriteNewPlace(
                handle, b"/probe/value", 0, 1, EXPECTED_BODY, PROBE_CAPACITY,
                PROBE_LANGTYPE.encode(), ctypes.byref(body), error_buffer, 256)
            if status != 0:
                return RESULT_UNKNOWN, (
                    f"write failed: {error_buffer.value.decode(errors='replace')}")
            wire = ctypes.POINTER(ctypes.c_uint8)()
            wire_len = ctypes.c_uint32(0)
            status = library.kvspaceGet(
                handle, b"/probe/value", 1, ctypes.byref(wire),
                ctypes.byref(wire_len))
            if status != 0 or wire_len.value == 0:
                return RESULT_UNKNOWN, "read failed"
            data = bytes(wire[index] for index in range(int(wire_len.value)))
            return classify_wire(data)
        finally:
            library.kvspaceClose(handle)
def run_main(arguments: list[str],
             capture_output: bool = False) -> tuple[int, str, str]:
    """Run main with captured stdout so the self-test can read the exit status.

    `main` follows the `sys.argv` shape, so arguments[0] is the program name and is
    skipped by the option loop; the helper inserts a placeholder so callers can pass only
    the real arguments. Returns (status, stdout, stderr); without capture the output still
    goes to the caller.
    """
    buffer = io.StringIO()
    error = io.StringIO()
    argv = ["check_kvspace_head_layout.py"] + arguments
    if capture_output:
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(error):
            status = main(argv)
    else:
        status = main(argv)
    return status, buffer.getvalue(), error.getvalue()


# ---------------------------------------------------------------------------------------
# Self-test. The probe's verdict is only trustworthy if it can reach each of its three
# outcomes; a probe that always answers "16-byte" looks identical to a correct one on a
# correct tree. Fixtures build real stub libraries with the C compiler — the probe judges
# bytes produced by a library, so a Python fake would test the fake instead of the probe.
# ---------------------------------------------------------------------------------------
# 构造器 stub：签名与现行头一致（无 capacity 参数）。三个宏控制它写哪一代 wire——
# 探针判的是**字节**，所以每个分支必须真的按各自格式写，否则测的是 stub 不是探针。
STUB_SOURCE = """\
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int kvlangXvalueHeadNewSlack(const uint8_t *data, uint64_t data_length,
                              const char *langtype,
                              uint8_t **output_wire,
                              uint64_t *output_wire_length) {
    if (output_wire == NULL || output_wire_length == NULL || langtype == NULL ||
        (data_length != 0U && data == NULL)) {
        return -1;
    }
    *output_wire = NULL;
    *output_wire_length = 0;
    const uint64_t record_length = 3U + (uint64_t)strlen(langtype);
#if defined(LEGACY_EIGHT_BYTE_HEAD)
    /* 旧 8 字节头：[len:u48][storetype][ptr][body][attr_len:u16][records] */
    const uint64_t total = 8U + data_length + 2U + record_length;
    uint8_t *wire = calloc(1, (size_t)total);
    if (wire == NULL) {
        return -1;
    }
    for (int index = 0; index < 6; ++index) {
        wire[index] = (uint8_t)((data_length >> (8 * index)) & 0xFFu);
    }
    wire[6] = 1U;
    wire[7] = 0U;
    if (data_length != 0U) {
        memcpy(wire + 8, data, (size_t)data_length);
    }
    uint8_t *attrs = wire + 8 + data_length;
    const uint16_t attr_length = (uint16_t)record_length;
    memcpy(attrs, &attr_length, 2);
    attrs[2] = 1;
    attrs[3] = (uint8_t)(strlen(langtype) & 0xFFu);
    attrs[4] = (uint8_t)((strlen(langtype) >> 8) & 0xFFu);
    memcpy(attrs + 5, langtype, strlen(langtype));
#else
    /* [bodylen:u64][attrslen:u16][padding:6][body][records] */
#ifdef LEGACY_CAPACITY_FORM
    /* 上一代：bodylen 记容量，有效长度在 tag 2。 */
    const uint64_t extra = 11U;
    const uint64_t body_field = 80U;
#else
    const uint64_t extra = 0U;
    const uint64_t body_field = data_length;
#endif
    const uint64_t total = 16U + body_field + record_length + extra;
    uint8_t *wire = calloc(1, (size_t)total);
    if (wire == NULL) {
        return -1;
    }
    memcpy(wire + 0, &body_field, 8);
    const uint16_t attrs_length = (uint16_t)(record_length + extra);
    memcpy(wire + 8, &attrs_length, 2);
    memset(wire + 10, 0, 6);
#ifdef LEGACY_LAYOUT_BYTE
    /* 上一代头：offset 12 是 layout 而不是 padding 的一部分。 */
    wire[12] = 1U;
#endif
    if (data_length != 0U) {
        memcpy(wire + 16, data, (size_t)data_length);
    }
    uint8_t *attrs = wire + 16 + body_field;
    attrs[0] = 1;
    attrs[1] = (uint8_t)(strlen(langtype) & 0xFFu);
    attrs[2] = (uint8_t)((strlen(langtype) >> 8) & 0xFFu);
    memcpy(attrs + 3, langtype, strlen(langtype));
    if (extra != 0U) {
        /* 上一代：attr 尾部一条 tag 2 reallen。 */
        uint8_t *reallen = attrs + record_length;
        reallen[0] = 2;
        reallen[1] = 8;
        reallen[2] = 0;
        for (int index = 0; index < 8; ++index) {
            reallen[3 + index] =
                (uint8_t)((data_length >> (8 * index)) & 0xFFu);
        }
    }
#endif
    *output_wire = wire;
    *output_wire_length = total;
    return 0;
}
"""

# 最小后端：补齐前端会 dlsym 的全部符号。`kvspaceWriteNewPlace` 写出来的字节决定
# 探针判什么，所以它必须真的按目标代的格式写，否则测的是 stub 而不是探针。
#   LEGACY_CAPACITY_FORM —— bodylen 记容量、attr 尾带 tag 2 reallen
#   LEGACY_LAYOUT_BYTE   —— offset 12 是 layout 字节而不是 padding
STUB_BACKEND = """/* 最小后端 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HEAD_LEN __HEAD_LEN__

static uint8_t g_wire[512];
static uint32_t g_wire_len = 0;

void *kvspaceConnect(const char *dsn) { (void)dsn; return (void *)0x1000; }
void kvspaceClose(void *h) { (void)h; }
int kvspaceSetValue(void *h, const char *k, const uint8_t *v, uint32_t n,
                    char *e, uint32_t ec)
    { (void)h;(void)k;(void)v;(void)n;(void)e;(void)ec; return 0; }
int kvspaceGetPart(void *h, const char *k, uint32_t o, uint8_t *b, uint32_t c,
                   uint32_t *n)
    { (void)h;(void)k;(void)o;(void)b;(void)c; *n=0; return 0; }
int kvspaceSetPart(void *h, const char *k, uint32_t o, const uint8_t *v, uint32_t n)
    { (void)h;(void)k;(void)o;(void)v;(void)n; return 0; }
int kvspaceWriteInPlace(void *h, const char *k, int r, uint32_t n, uint8_t **b,
                        char *e, uint32_t ec)
    { (void)h;(void)k;(void)r;(void)n;(void)e;(void)ec; *b = g_wire + HEAD_LEN; return 0; }
int kvspaceListLen(void *h, const char *p, int a, int r, int32_t *o)
    { (void)h;(void)p;(void)a;(void)r; *o=0; return 0; }
int kvspaceListAt(void *h, const char *p, int a, int r, int32_t i, uint8_t *b,
                  uint32_t c, uint32_t *o)
    { (void)h;(void)p;(void)a;(void)r;(void)i;(void)b;(void)c; *o=0; return 0; }
int kvspaceDel(void *h, const char *const *k, uint32_t n, char *e, uint32_t ec)
    { (void)h;(void)k;(void)n;(void)e;(void)ec; return 0; }
int kvspaceDelTree(void *h, const char *k, char *e, uint32_t ec)
    { (void)h;(void)k;(void)e;(void)ec; return 0; }
int kvspaceCp(void *h, const char *s, const char *d, char *e, uint32_t ec)
    { (void)h;(void)s;(void)d;(void)e;(void)ec; return 0; }
int kvspaceCpTree(void *h, const char *s, const char *d, char *e, uint32_t ec)
    { (void)h;(void)s;(void)d;(void)e;(void)ec; return 0; }
int kvspaceCplist(void *h, const char *s, const char *d, char *e, uint32_t ec)
    { (void)h;(void)s;(void)d;(void)e;(void)ec; return 0; }
int kvspaceMkindex(void *h, const char *k, uint64_t c, char *e, uint32_t ec)
    { (void)h;(void)k;(void)c;(void)e;(void)ec; return 0; }
int kvspaceMkindexExt(void *h, const char *k, const char *x, char *e, uint32_t ec)
    { (void)h;(void)k;(void)x;(void)e;(void)ec; return 0; }
int kvspaceRmindexExt(void *h, const char *k, char *e, uint32_t ec)
    { (void)h;(void)k;(void)e;(void)ec; return 0; }
int kvspaceClear(void *h, char *e, uint32_t ec) { (void)h;(void)e;(void)ec; return 0; }
int kvspaceWatch(void *h, const char *k, uint32_t t, uint8_t **v, uint32_t *n,
                 char *e, uint32_t ec)
    { (void)h;(void)k;(void)t;(void)e;(void)ec; *v=NULL; *n=0; return 0; }

int kvspaceWriteNewPlace(void *h, const char *key, uint8_t ref, uint8_t storetype,
                         uint32_t body_len, uint64_t body_cap, const char *langtype,
                         uint8_t **body, char *err, uint32_t err_cap) {
    (void)h; (void)key; (void)storetype; (void)ref; (void)err; (void)err_cap;
    (void)body_cap;
    const unsigned lt = (unsigned)strlen(langtype);
    memset(g_wire, 0, sizeof g_wire);
    const unsigned record = 3 + lt;
#if HEAD_LEN == 16
#ifdef LEGACY_CAPACITY_FORM
    const uint64_t body_field = 80U;
    const unsigned extra = 11U;
#else
    const uint64_t body_field = (uint64_t)body_len;
    const unsigned extra = 0U;
#endif
    memcpy(g_wire + 0, &body_field, 8);
    const uint16_t attrs_len = (uint16_t)(record + extra);
    memcpy(g_wire + 8, &attrs_len, 2);
    memset(g_wire + 10, 0, 6);
#ifdef LEGACY_LAYOUT_BYTE
    g_wire[12] = 1U;
#endif
    memset(g_wire + 16, 'a', (size_t)body_field);
    uint8_t *attrs = g_wire + 16 + body_field;
    attrs[0] = 1;
    attrs[1] = (uint8_t)(lt & 0xFF); attrs[2] = (uint8_t)(lt >> 8);
    memcpy(attrs + 3, langtype, lt);
    if (extra != 0U) {
        uint8_t *reallen = attrs + record;
        reallen[0] = 2; reallen[1] = 8; reallen[2] = 0;
        for (int index = 0; index < 8; ++index) {
            reallen[3 + index] =
                (uint8_t)(((uint64_t)body_len >> (8 * index)) & 0xFFu);
        }
    }
    g_wire_len = (uint32_t)(HEAD_LEN + body_field + record + extra);
#else
    /* [len:u48][storetype][ptr][body][attr_len:u16][tag][len][data] */
    const uint64_t body_len_u48 = (uint64_t)body_len;
    for (int i = 0; i < 6; ++i) g_wire[i] = (uint8_t)(body_len_u48 >> (8 * i));
    g_wire[6] = storetype; g_wire[7] = ref;
    memset(g_wire + 8, 'a', body_len);
    uint8_t *attrs = g_wire + 8 + body_len;
    const uint16_t attr_len = (uint16_t)record;
    memcpy(attrs, &attr_len, 2);
    attrs[2] = 1;
    attrs[3] = (uint8_t)(lt & 0xFF); attrs[4] = (uint8_t)(lt >> 8);
    memcpy(attrs + 5, langtype, lt);
    g_wire_len = (uint32_t)(HEAD_LEN + body_len + 2 + record);
#endif
    *body = g_wire + HEAD_LEN;
    return 0;
}

/* 返回整条 wire（含 head）：探针按字节判布局，与真前端同一条判据。 */
int kvspaceGet(void *h, const char *key, int resolve, uint8_t **out,
               uint32_t *out_len) {
    (void)h; (void)key; (void)resolve;
    if (!out || !out_len) return 1;
    *out = g_wire;
    *out_len = g_wire_len;
    return 0;
}
"""

# 自测前端：既要能被 probe_library 直接探（导出构造），又要能转发到某个后端
# （导出 connect/close/write/get）。它把后端选在环境变量里，避免自测依赖 dlopen 路径。
STUB_FRONTEND = """\
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*connect_fn)(const char *);
typedef void (*close_fn)(void *);
typedef int (*write_fn)(void *, const char *, uint8_t, uint8_t, uint32_t, uint64_t,
                        const char *, uint8_t **, char *, uint32_t);
typedef int (*get_fn)(void *, const char *, int, uint8_t **, uint32_t *);

typedef struct { void *handle; close_fn close; write_fn write; get_fn get; } forward_t;

void *kvspaceConnect(const char *dsn) {
    (void)dsn;
    const char *path = getenv("KVSPACE_SELFTEST_BACKEND");
    if (path == NULL) {
        return NULL;
    }
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        return NULL;
    }
    forward_t *forward = calloc(1, sizeof(*forward));
    if (forward == NULL) {
        return NULL;
    }
    forward->close = (close_fn)dlsym(library, "kvspaceClose");
    forward->write = (write_fn)dlsym(library, "kvspaceWriteNewPlace");
    forward->get = (get_fn)dlsym(library, "kvspaceGet");
    if (forward->close == NULL || forward->write == NULL || forward->get == NULL) {
        free(forward);
        return NULL;
    }
    forward->handle = ((connect_fn)dlsym(library, "kvspaceConnect"))(dsn);
    if (forward->handle == NULL) {
        free(forward);
        return NULL;
    }
    return forward;
}

void kvspaceClose(void *h) {
    forward_t *forward = h;
    if (forward == NULL) {
        return;
    }
    forward->close(forward->handle);
    free(forward);
}

int kvspaceWriteNewPlace(void *h, const char *key, uint8_t ref, uint8_t storetype,
                         uint32_t body_len, uint64_t body_cap, const char *langtype,
                         uint8_t **body, char *err, uint32_t err_cap) {
    forward_t *forward = h;
    return forward->write(forward->handle, key, ref, storetype, body_len, body_cap,
                          langtype, body, err, err_cap);
}

int kvspaceGet(void *h, const char *key, int resolve, uint8_t **out,
               uint32_t *out_len) {
    forward_t *forward = h;
    return forward->get(forward->handle, key, resolve, out, out_len);
}

int kvlangXvalueHeadNewSlack(const uint8_t *data, uint64_t data_length,
                              const char *langtype,
                              uint8_t **output_wire,
                              uint64_t *output_wire_length) {
    if (output_wire == NULL || output_wire_length == NULL || langtype == NULL ||
        (data_length != 0U && data == NULL)) {
        return -1;
    }
    *output_wire = NULL;
    *output_wire_length = 0;
    const uint64_t record = 3U + (uint64_t)strlen(langtype);
    const uint64_t total = 16U + data_length + record;
    uint8_t *wire = calloc(1, (size_t)total);
    if (wire == NULL) {
        return -1;
    }
    memcpy(wire + 0, &data_length, 8);
    const uint16_t attrs_length = (uint16_t)record;
    memcpy(wire + 8, &attrs_length, 2);
    memset(wire + 10, 0, 6);
    if (data_length != 0U) {
        memcpy(wire + 16, data, (size_t)data_length);
    }
    uint8_t *attrs = wire + 16 + data_length;
    attrs[0] = 1;
    attrs[1] = (uint8_t)(strlen(langtype) & 0xFFu);
    attrs[2] = (uint8_t)((strlen(langtype) >> 8) & 0xFFu);
    memcpy(attrs + 3, langtype, strlen(langtype));
    *output_wire = wire;
    *output_wire_length = total;
    return 0;
}
"""

STUB_WITHOUT_SYMBOL = """\
int some_other_entry_point(void) { return 0; }
"""

STUB_UNRECOGNISED = """\
#include <stdint.h>
#include <stdlib.h>

int kvlangXvalueHeadNewSlack(const uint8_t *data, uint64_t data_length,
                              const char *langtype,
                              uint8_t **output_wire,
                              uint64_t *output_wire_length) {
    (void)data; (void)data_length; (void)langtype;
    uint8_t *wire = calloc(1, 32);
    if (wire == NULL) {
        return -1;
    }
    *output_wire = wire;
    *output_wire_length = 32;
    return 0;
}
"""


class SelftestFailure(Exception):
    pass


def compile_stub(directory: str, name: str, source: str,
                 definitions: tuple[str, ...] = ()) -> str:
    source_path = os.path.join(directory, name + ".c")
    library_path = os.path.join(directory, name + ".so")
    with open(source_path, "w", encoding="utf-8") as stream:
        stream.write(source)
    compiler = shutil.which("cc") or shutil.which("gcc")
    if compiler is None:
        raise SelftestFailure(
            "cannot run the self-test: a C compiler (cc or gcc) is required to build "
            "the stub libraries")
    command = [compiler, "-shared", "-fPIC"]
    command.extend(f"-D{definition}" for definition in definitions)
    command.extend(["-o", library_path, source_path])
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    if completed.returncode != 0 or not os.path.exists(library_path):
        raise SelftestFailure(
            f"cannot run the self-test: building {name} failed:\n{completed.stdout}")
    return library_path


def expect(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def run_selftest() -> int:
    failures: list[str] = []
    try:
        with tempfile.TemporaryDirectory(prefix="kvspace-head-selftest-") as directory:
            frontend = compile_stub(directory, "frontend", STUB_FRONTEND)
            for length in (16, 8):
                compile_stub(directory, f"backend{length}",
                             STUB_BACKEND.replace("__HEAD_LEN__", str(length)))
            legacy_eight = compile_stub(
                directory, "libeight", STUB_SOURCE, ("LEGACY_EIGHT_BYTE_HEAD",))
            legacy_constructor = compile_stub(
                directory, "liblegacy", STUB_SOURCE, ("LEGACY_LAYOUT_BYTE",))
            capacity_constructor = compile_stub(
                directory, "libcapacity", STUB_SOURCE, ("LEGACY_CAPACITY_FORM",))
            legacy_backend = compile_stub(
                directory, "backend16legacy",
                STUB_BACKEND.replace("__HEAD_LEN__", "16"),
                ("LEGACY_LAYOUT_BYTE",))
            capacity_backend = compile_stub(
                directory, "backend16capacity",
                STUB_BACKEND.replace("__HEAD_LEN__", "16"),
                ("LEGACY_CAPACITY_FORM",))
            symbol_missing = compile_stub(directory, "libnosym", STUB_WITHOUT_SYMBOL)
            unrecognised = compile_stub(directory, "libodd", STUB_UNRECOGNISED)

            # 1. 纯函数级：probe_library 必须把两版布局区分开。
            #    探的是「自己造 wire 的库」，所以用前端 stub（它导出构造函数）。
            expect(probe_library(frontend) == RESULT_SIXTEEN,
                   "probe_library(16-byte constructor) must be 16-byte", failures)
            expect(probe_library(unrecognised) == RESULT_UNKNOWN,
                   "probe_library(unrecognised wire) must be unknown", failures)
            expect(probe_library(symbol_missing) == RESULT_UNKNOWN,
                   "probe_library(library without the symbol) must be unknown", failures)
            expect(probe_library(legacy_eight) == RESULT_EIGHT,
                   "probe_library(8-byte constructor) must be 8-byte", failures)
            # 两处「上一代痕迹」必须各自能判出来——判不出「旧」就是假绿。
            expect(probe_library(legacy_constructor) == RESULT_SIXTEEN_LAYOUT_BYTE,
                   "probe_library(a head still carrying the `layout` byte) must "
                   "report the removed byte", failures)
            expect(probe_library(capacity_constructor) == RESULT_SIXTEEN_CAPACITY_FORM,
                   "probe_library(a head whose bodylen is a capacity) must report "
                   "the capacity form", failures)

            # 2. 判据核心：写值 + 读 headlen，按后端指认。
            verdict, detail = probe_backend(
                frontend, "shm", "shm://{store}/store",
                os.path.join(directory, "backend16.so"))
            expect(verdict == RESULT_SIXTEEN,
                   f"a 16-byte backend must be judged 16-byte, got {verdict!r}", failures)
            verdict, detail = probe_backend(
                frontend, "fs", "fs://{store}/store",
                os.path.join(directory, "backend8.so"))
            expect(verdict == RESULT_EIGHT,
                   f"an 8-byte backend must be judged 8-byte, got {verdict!r}", failures)
            verdict, detail = probe_backend(
                frontend, "shm", "shm://{store}/store", legacy_backend)
            expect(verdict == RESULT_SIXTEEN_LAYOUT_BYTE,
                   "a backend still writing the `layout` byte must be judged stale, "
                   f"got {verdict!r}", failures)
            verdict, detail = probe_backend(
                frontend, "shm", "shm://{store}/store", capacity_backend)
            expect(verdict == RESULT_SIXTEEN_CAPACITY_FORM,
                   "a backend still writing bodylen=capacity must be judged stale, "
                   f"got {verdict!r}", failures)
            verdict, detail = probe_backend(
                frontend, "fs", "fs://{store}/store", None)
            expect(verdict == RESULT_SKIPPED,
                   f"an unbuilt backend must be skipped, got {verdict!r}", failures)

            # 3. 编排层：main 必须遍历**所有**后端，不是只探第一个。
            #    只在 probe_backend 层测，把 BACKEND_CASES 截成一条也不会红。
            cases_seen: list[str] = []
            original = probe_backend

            def recording(frontend_path, scheme, dsn_template, backend_path):
                cases_seen.append(scheme)
                return RESULT_SIXTEEN, "recorded"

            globals()["probe_backend"] = recording
            try:
                status, _, _ = run_main(["--frontend", frontend], capture_output=True)
            finally:
                globals()["probe_backend"] = original
            expected_schemes = [scheme for scheme, _ in BACKEND_CASES]
            expect(cases_seen == expected_schemes,
                   f"main must probe every backend: expected {expected_schemes}, "
                   f"probed {cases_seen}", failures)
            expect(status == 0,
                   f"all-16-byte backends must exit 0, got {status}", failures)

            # 4. 红时必须**指认是哪个后端**：只红不指认等于没定位根因。
            def stale(frontend_path, scheme, dsn_template, backend_path):
                return RESULT_EIGHT, "headlen=8"

            globals()["probe_backend"] = stale
            try:
                status, output, _ = run_main(["--frontend", frontend],
                                             capture_output=True)
            finally:
                globals()["probe_backend"] = original
            expect(status == 1, f"a stale backend must exit 1, got {status}", failures)
            for scheme, _ in BACKEND_CASES:
                expect(f"backend {scheme!r}" in output,
                       f"the verdict must name the stale backend {scheme!r}; "
                       f"output was: {output.strip()}", failures)
    except SelftestFailure as failure:
        print(f"[selftest] {failure}")
        print("check_kvspace_head_layout selftest: FAIL (cannot run)")
        return 1

    for failure in failures:
        print(f"[selftest] {failure}")
    if failures:
        print(f"check_kvspace_head_layout selftest: FAIL ({len(failures)} failure(s))")
        return 1
    print("check_kvspace_head_layout selftest: OK "
          "(both layouts, per-backend verdicts, skip path)")
    return 0


BACKEND_SONAMES = {
    "shm": "libkvspace-c.dylib" if sys.platform == "darwin" else "libkvspace-c.so.1",
    "fs": "libkvspace_durable.dylib" if sys.platform == "darwin"
    else "libkvspace_durable.so.1",
}


def backend_library(frontend_path: str, scheme: str) -> str | None:
    """Locate a backend the same way the frontend would: four candidates, first hit."""
    soname = BACKEND_SONAMES[scheme]
    return next((candidate for candidate in backend_candidates(frontend_path, soname)
                 if os.path.exists(candidate)), None)


def main(arguments: list[str]) -> int:
    frontend_path = os.path.expanduser(
        "~/github.com/array2d/kvlang/lib/libkvspace.so")
    if "--selftest" in arguments:
        return run_selftest()
    index = 1
    while index < len(arguments):
        if arguments[index] == "--frontend" and index + 1 < len(arguments):
            frontend_path = arguments[index + 1]
            index += 2
        else:
            frontend_path = arguments[index]
            index += 1

    if not os.path.exists(frontend_path):
        print(f"missing frontend: {frontend_path}")
        return 2

    print(f"frontend: {frontend_path}")
    # 前端自己也是个 .so，但它只是转发壳：真正造 wire 的是后端。前端的结果照样打印，
    # 但不作为判据——判据是每个后端的 head 大小。
    print(f"  frontend itself: {probe_library(frontend_path)}")

    verdicts: list[tuple[str, str, str, str | None]] = []
    for scheme, dsn_template in BACKEND_CASES:
        library = backend_library(frontend_path, scheme)
        verdict, detail = probe_backend(frontend_path, scheme, dsn_template, library)
        verdicts.append((scheme, verdict, detail, library))

    stale = [entry for entry in verdicts
             if entry[1] in (RESULT_EIGHT, RESULT_SIXTEEN_LAYOUT_BYTE,
                             RESULT_SIXTEEN_CAPACITY_FORM)]
    unjudged = [entry for entry in verdicts if entry[1] == RESULT_UNKNOWN]
    judged = [entry for entry in verdicts if entry[1] == RESULT_SIXTEEN]
    skipped = [entry for entry in verdicts if entry[1] == RESULT_SKIPPED]

    for scheme, verdict, detail, library in verdicts:
        shown = os.path.realpath(library) if library else "(not built)"
        print(f"backend {scheme:4s}: {verdict:22s} {detail}")
        print(f"          library: {shown}")

    if stale:
        for scheme, _, detail, _ in stale:
            print(f"stale   : backend {scheme!r} uses a legacy head ({detail}); "
                  f"runtimec is correct to fail against it")
        return 1
    if unjudged:
        for scheme, _, detail, _ in unjudged:
            print(f"unjudged: backend {scheme!r} could not be judged ({detail})")
        return 2
    if skipped:
        for scheme, _, _, library in skipped:
            print(f"skipped : backend {scheme!r} is not built; expected "
                  f"{BACKEND_SONAMES[scheme]} next to the frontend")
    print(f"verdict : {len(judged)} backend(s) use the 16-byte head")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
