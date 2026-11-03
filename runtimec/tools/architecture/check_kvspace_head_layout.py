#!/usr/bin/env python3
"""判 kvspace 的 XValue head 是 8 字节还是 16 字节布局。

为什么需要它：`lib/` 里可能同时存在新旧两份产物，装错位置或加载到旧的那份时，
真后端用例会红，但**红的原因不是代码错**。先判库，再判代码。

为什么必须判两份：**布局由后端决定，不是前端**。`libkvspace.so` 只是个转发壳
（`kvspace/src/frontend.c`），真正造 wire / 读 head 的是 `libkvspace-c`（shm）
或 `libkvspace_durable`。前端重建、后端没重建时，探针只看前端会给出「16 字节，
没问题」的假结论——这条是用一次完整的红换来的。

判据不看符号是否存在（两版都有 `kvspaceXvalueHeadNewSlack`），而是直接造一个值
看字节：

    8 字节布局 : [len:u48][storetype][ptr]  body  [attr_len:u16][records]
    16 字节布局: [bodylen:u64][attrslen:u32][storetype][ptr][pad:2]  body  [records]

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
PROBE_LANGTYPE = "int64"
LANGTYPE_RECORD_BYTES = 3 + len(PROBE_LANGTYPE)  # tag + u16 len + data

RESULT_SIXTEEN = "16-byte"
RESULT_EIGHT = "8-byte"
RESULT_UNKNOWN = "unknown"


def probe_library(library_path: str) -> str:
    """返回 RESULT_SIXTEEN / RESULT_EIGHT / RESULT_UNKNOWN，并打印证据。"""
    try:
        library = ctypes.CDLL(library_path)
        new_slack = library.kvspaceXvalueHeadNewSlack
    except (OSError, AttributeError) as error:
        print(f"  cannot load or bind: {error}")
        return RESULT_UNKNOWN
    new_slack.restype = ctypes.c_int
    new_slack.argtypes = [
        ctypes.c_char_p, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.c_char_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
        ctypes.POINTER(ctypes.c_uint64),
    ]
    wire = ctypes.POINTER(ctypes.c_uint8)()
    wire_size = ctypes.c_uint64(0)
    status = new_slack(
        b"ab", EXPECTED_BODY, EXPECTED_BODY, PROBE_LANGTYPE.encode(),
        ctypes.byref(wire), ctypes.byref(wire_size))
    if status != 0 or wire_size.value == 0:
        print(f"  cannot construct a probe value (status={status})")
        return RESULT_UNKNOWN

    total = int(wire_size.value)
    data = bytes(wire[index] for index in range(total))
    print(f"  wire: {total} bytes: {data.hex(' ')}")

    bodylen = int.from_bytes(data[0:8], "little")
    attrslen = int.from_bytes(data[8:12], "little")
    if (bodylen == EXPECTED_BODY and
            attrslen == LANGTYPE_RECORD_BYTES and
            total == 16 + bodylen + attrslen):
        print(f"  layout: 16-byte head (bodylen={bodylen}, "
              f"attrslen={attrslen})")
        return RESULT_SIXTEEN

    u48_length = int.from_bytes(data[0:6], "little")
    if u48_length == EXPECTED_BODY:
        print(f"  layout: 8-byte head (u48 len={u48_length}, "
              f"storetype={data[6]}, pointer={data[7]})")
        return RESULT_EIGHT

    print(f"  layout: unrecognised (bodylen={bodylen}, attrslen={attrslen}, "
          f"u48={u48_length})")
    return RESULT_UNKNOWN


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
STUB_SOURCE = """\
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void write_record(uint8_t *attrs, const char *langtype) {
    const size_t length = strlen(langtype);
    attrs[0] = 1;
    attrs[1] = (uint8_t)(length & 0xFFu);
    attrs[2] = (uint8_t)((length >> 8) & 0xFFu);
    memcpy(attrs + 3, langtype, length);
}

int kvspaceXvalueHeadNewSlack(const uint8_t *data, uint64_t data_length,
                              uint64_t capacity, const char *langtype,
                              uint8_t **output_wire,
                              uint64_t *output_wire_length) {
    if (output_wire == NULL || output_wire_length == NULL || langtype == NULL) {
        return -1;
    }
    *output_wire = NULL;
    *output_wire_length = 0;
    if (capacity < data_length || (data_length != 0U && data == NULL)) {
        return -1;
    }
    const uint64_t record_length = 3U + (uint64_t)strlen(langtype);
#if HEAD_LEN == 16
    const uint64_t total = 16U + data_length + record_length;
    uint8_t *wire = calloc(1, (size_t)total);
    if (wire == NULL) {
        return -1;
    }
    memcpy(wire + 0, &data_length, 8);
    const uint32_t attrs_length = (uint32_t)record_length;
    memcpy(wire + 8, &attrs_length, 4);
    wire[12] = 1U;
    wire[13] = 0U;
    if (data_length != 0U) {
        memcpy(wire + 16, data, (size_t)data_length);
    }
    write_record(wire + 16 + data_length, langtype);
#else
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
    write_record(attrs + 2, langtype);
#endif
    *output_wire = wire;
    *output_wire_length = total;
    return 0;
}
"""

# 一个导出别的名字的库：文件在、符号不在，走 `cannot load or bind` 分支。
STUB_WITHOUT_SYMBOL = """\
int some_other_entry_point(void) { return 0; }
"""

# 一个返回无法识别字节的库：两版布局都不匹配，走 `unrecognised` 分支。
STUB_UNRECOGNISED = """\
#include <stdint.h>
#include <stdlib.h>

int kvspaceXvalueHeadNewSlack(const uint8_t *data, uint64_t data_length,
                              uint64_t capacity, const char *langtype,
                              uint8_t **output_wire,
                              uint64_t *output_wire_length) {
    (void)data; (void)data_length; (void)capacity; (void)langtype;
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
            sixteen = compile_stub(directory, "lib16", STUB_SOURCE, ("HEAD_LEN=16",))
            eight = compile_stub(directory, "lib8", STUB_SOURCE, ("HEAD_LEN=8",))
            symbol_missing = compile_stub(directory, "libnosym", STUB_WITHOUT_SYMBOL)
            unrecognised = compile_stub(directory, "libodd", STUB_UNRECOGNISED)

            # 1. 纯函数级：探针必须把两版布局区分开。
            expect(probe_library(sixteen) == RESULT_SIXTEEN,
                   f"probe_library(16-byte stub) returned {probe_library(sixteen)!r}",
                   failures)
            expect(probe_library(eight) == RESULT_EIGHT,
                   f"probe_library(8-byte stub) returned {probe_library(eight)!r}",
                   failures)

            # 2. 三种「判不出」各走一条不同分支。
            expect(probe_library(os.path.join(directory, "absent.so")) == RESULT_UNKNOWN,
                   "probe_library(missing file) must be unknown", failures)
            expect(probe_library(symbol_missing) == RESULT_UNKNOWN,
                   "probe_library(library without the symbol) must be unknown", failures)
            expect(probe_library(unrecognised) == RESULT_UNKNOWN,
                   "probe_library(unrecognised wire) must be unknown", failures)

            # 3. 后端候选顺序：让多条候选同时存在，断言选中的是序号最小的那条。
            #    只放一条时顺序对调也不会红——那样的用例测的是「能找到」，不是顺序。
            frontend = os.path.join(directory, "frontend", "libkvspace.so")
            os.makedirs(os.path.dirname(frontend))
            shutil.copy(sixteen, frontend)
            # 期望顺序独立写出来，依据是 routing/README.md §十 的规范条款，不取自
            # backend_candidates()，也不抄 frontend.c——用被测对象的输出当期望，它把顺序
            # 改错时用例会跟着一起错，永远自洽。
            order_soname = "liborder.so"
            directory_path = os.path.dirname(frontend)
            authoritative_order = [
                os.path.join(directory_path, order_soname),
                os.path.join(directory_path, "kvspace", order_soname),
                os.path.join(directory_path, "..", "kvspace", order_soname),
            ]
            actual_order = backend_candidates(frontend, order_soname)
            expect([os.path.normpath(path) for path in actual_order[:3]] ==
                   [os.path.normpath(path) for path in authoritative_order],
                   f"backend candidate order is {actual_order[:3]!r}, expected "
                   f"{authoritative_order!r} "
                   f"(routing/README.md §十 后端查找顺序)", failures)
            # 第四条是平台系统目录，照条款核**具体路径**（不是只核条数）：
            # 只核数量时把 /usr/lib 换成别的目录也不会红——那正是它存在的意义。
            platform_directory = "/usr/local/lib/kvspace" if sys.platform == "darwin" \
                else "/usr/lib/kvspace"
            expect(len(actual_order) == 4,
                   "four backend candidates are required", failures)
            expect(len(actual_order) == 4 and
                   actual_order[3] == os.path.join(platform_directory, order_soname),
                   f"the system candidate must be "
                   f"{os.path.join(platform_directory, order_soname)!r}, got "
                   f"{actual_order[3] if len(actual_order) == 4 else actual_order!r}",
                   failures)
            for length in range(1, len(authoritative_order) + 1):
                staged_paths = []
                for candidate in authoritative_order[:length]:
                    os.makedirs(os.path.dirname(os.path.abspath(candidate)),
                                exist_ok=True)
                    shutil.copy(sixteen, candidate)
                    staged_paths.append(candidate)
                chosen = next((path for path in backend_candidates(frontend, order_soname)
                               if os.path.exists(path)), None)
                expected = os.path.realpath(staged_paths[0])
                expect(os.path.realpath(chosen or "") == expected,
                       f"with candidates 0..{length - 1} staged, the probe picked "
                       f"{chosen!r} instead of the first {expected!r}", failures)
                for staged in staged_paths:
                    os.remove(staged)

            # 3b. 后端路径按 realpath 呈现：候选是符号链接时必须解析到真实文件。
            link_soname = "liblink.so"
            link_target = os.path.join(directory, "real", "liblink-real.so")
            os.makedirs(os.path.dirname(link_target), exist_ok=True)
            shutil.copy(sixteen, link_target)
            link_candidate = backend_candidates(frontend, link_soname)[0]
            os.symlink(link_target, link_candidate)
            link_status, link_stdout, _ = run_main(
                [frontend, "--soname", link_soname], capture_output=True)
            expect(f"backend : {os.path.realpath(link_target)}" in link_stdout,
                   "the backend path must be reported through realpath", failures)
            os.remove(link_candidate)

            # 4. 端到端：退出码三态。先放一个 16 字节后端，保证基线是 0。
            baseline_soname = "libbaseline.so"
            baseline = os.path.join(directory, "frontend", "kvspace", baseline_soname)
            os.makedirs(os.path.dirname(baseline), exist_ok=True)
            shutil.copy(sixteen, baseline)
            expect(run_main([frontend, "--soname", baseline_soname],
                            capture_output=True)[0] == 0,
                   "frontend and backend both 16-byte must exit 0", failures)

            # 每个用例用不同的 soname：同一个进程里 dlopen 同一路径会复用已加载的
            # handle，换文件内容也不会重新读，用例之间会互相污染。
            backend_directory = os.path.join(directory, "frontend", "kvspace")
            os.makedirs(backend_directory, exist_ok=True)
            for soname, library, expected, description in (
                    ("libstale.so", eight, 1, "an 8-byte backend must exit 1"),
                    ("libodd.so", unrecognised, 2,
                     "an unrecognisable backend must exit 2")):
                shutil.copy(library, os.path.join(backend_directory, soname))
                status, _, _ = run_main([frontend, "--soname", soname],
                                        capture_output=True)
                expect(status == expected, f"{description}, got {status}", failures)

            absent = os.path.join(directory, "absent-frontend", "libkvspace.so")
            status, _, _ = run_main([absent, "--soname", "libkvspace-c.so.1"],
                                    capture_output=True)
            expect(status == 2,
                   f"a missing frontend must exit 2, got {status}", failures)
    except SelftestFailure as failure:
        print(f"[selftest] {failure}")
        print("check_kvspace_head_layout selftest: FAIL (cannot run)")
        return 1

    for failure in failures:
        print(f"[selftest] {failure}")
    if failures:
        print(f"check_kvspace_head_layout selftest: FAIL ({len(failures)} failure(s))")
        return 1
    print("check_kvspace_head_layout selftest: OK (2 layouts, 3 unknown paths, "
          "candidate order, 3 exit codes)")
    return 0


def main(arguments: list[str]) -> int:
    frontend_path = os.path.expanduser(
        "~/github.com/array2d/kvlang/lib/libkvspace.so")
    soname = "libkvspace-c.so.1" if sys.platform != "darwin" \
        else "libkvspace-c.dylib"
    if "--selftest" in arguments:
        return run_selftest()
    index = 1
    while index < len(arguments):
        if arguments[index] == "--frontend" and index + 1 < len(arguments):
            frontend_path = arguments[index + 1]
            index += 2
        elif arguments[index] == "--soname" and index + 1 < len(arguments):
            soname = arguments[index + 1]
            index += 2
        else:
            frontend_path = arguments[index]
            index += 1

    if not os.path.exists(frontend_path):
        print(f"missing frontend: {frontend_path}")
        return 2

    print(f"frontend: {frontend_path}")
    frontend_result = probe_library(frontend_path)

    backend_path = next(
        (candidate for candidate in backend_candidates(frontend_path, soname)
         if os.path.exists(candidate)), None)
    if backend_path is None:
        print(f"backend : no candidate exists for {soname}; tried:")
        for candidate in backend_candidates(frontend_path, soname):
            print(f"  {candidate}")
        print("verdict : cannot judge -- the layout comes from the backend, "
              "not the frontend")
        return 2
    print(f"backend : {os.path.realpath(backend_path)}")
    backend_result = probe_library(backend_path)

    results = (frontend_result, backend_result)
    if all(result == RESULT_SIXTEEN for result in results):
        print("verdict : both frontend and backend use the 16-byte head")
        return 0
    if RESULT_EIGHT in results:
        print("verdict : at least one library predates the 16-byte head "
              "change; runtimec is correct to fail against it")
        return 1
    print("verdict : layout cannot be determined; fix the environment before "
          "judging runtimec")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
