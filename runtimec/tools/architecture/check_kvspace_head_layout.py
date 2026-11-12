
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
PROBE_CAPACITY = 80
PROBE_LANGTYPE = "int64"
LANGTYPE_RECORD_BYTES = 3 + len(PROBE_LANGTYPE)
REALLEN_RECORD_BYTES = 11
ZERO_PADDING = b"\x00" * 6

RESULT_SIXTEEN = "16-byte"
RESULT_SIXTEEN_LAYOUT_BYTE = "16-byte-layout-byte"
RESULT_SIXTEEN_CAPACITY_FORM = "16-byte-capacity-form"
RESULT_EIGHT = "8-byte"
RESULT_UNKNOWN = "unknown"
RESULT_SKIPPED = "skipped"

EXPECTED_HEAD_LEN = 16
LEGACY_HEAD_LEN = 8

def classify_wire(data: bytes) -> tuple[str, str]:
    if len(data) < EXPECTED_HEAD_LEN:
        return RESULT_UNKNOWN, f"wire too short: {len(data)} bytes"
    bodylen = int.from_bytes(data[0:8], "little")
    attrslen = int.from_bytes(data[8:10], "little")

    if (int.from_bytes(data[0:6], "little") == EXPECTED_BODY and
            len(data) == LEGACY_HEAD_LEN + EXPECTED_BODY + 2 +
            LANGTYPE_RECORD_BYTES):
        return RESULT_EIGHT, (f"u48 len={EXPECTED_BODY} storetype={data[6]} "
                              f"pointer={data[7]} total={len(data)}")

    if len(data) != EXPECTED_HEAD_LEN + bodylen + attrslen:
        return RESULT_UNKNOWN, (
            f"bodylen={bodylen} attrslen={attrslen} total={len(data)} "
            "matches no known layout")

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
    buffer = io.StringIO()
    error = io.StringIO()
    argv = ["check_kvspace_head_layout.py"] + arguments
    if capture_output:
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(error):
            status = main(argv)
    else:
        status = main(argv)
    return status, buffer.getvalue(), error.getvalue()

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

            expect(probe_library(frontend) == RESULT_SIXTEEN,
                   "probe_library(16-byte constructor) must be 16-byte", failures)
            expect(probe_library(unrecognised) == RESULT_UNKNOWN,
                   "probe_library(unrecognised wire) must be unknown", failures)
            expect(probe_library(symbol_missing) == RESULT_UNKNOWN,
                   "probe_library(library without the symbol) must be unknown", failures)
            expect(probe_library(legacy_eight) == RESULT_EIGHT,
                   "probe_library(8-byte constructor) must be 8-byte", failures)
            expect(probe_library(legacy_constructor) == RESULT_SIXTEEN_LAYOUT_BYTE,
                   "probe_library(a head still carrying the `layout` byte) must "
                   "report the removed byte", failures)
            expect(probe_library(capacity_constructor) == RESULT_SIXTEEN_CAPACITY_FORM,
                   "probe_library(a head whose bodylen is a capacity) must report "
                   "the capacity form", failures)

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
    soname = BACKEND_SONAMES[scheme]
    return next((candidate for candidate in backend_candidates(frontend_path, soname)
                 if os.path.exists(candidate)), None)

def main(arguments: list[str]) -> int:
    frontend_path = os.path.expanduser(
        "<repo>/lib/libkvspace.so")
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
