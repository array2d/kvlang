#!/usr/bin/env python3
"""CI 断言：layout 产出的 rwir 集合 ⊆ runtimec2 的 myrwircap 表。

myrwircap（runtimec2/src/rwir_gen.cpp）是 runtimec 唯一能力表，表即代码；
layout 不持有任何清单副本。本测试取 layout 回归语料（benchmark 十个 case，
规模取 SWEEP 最小值，与 real_segment_corpus 同源）实际布局出的 opcode，
减去运行时注册的扩展 rwir（`/lib/<op>` kind=def rwir，契约域外），
差集非空即失败——说明 layout 会产出本 runtime 无法执行的算子。
"""

from __future__ import annotations

import argparse
import ctypes
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

CASE_NAMES = (
    "binary_search",
    "binary_trees",
    "fib",
    "hash_table",
    "iops",
    "k_nucleotide",
    "matmul",
    "nqueens",
    "prime_sieve",
    "quicksort",
)
SLOT_RE = re.compile(r"\[(-?\d+),(-?\d+)\]$")


def min_scales(run_path: Path) -> dict[str, int]:
    import ast

    tree = ast.parse(run_path.read_text(encoding="utf-8"), filename=str(run_path))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id == "SWEEP" for t in node.targets
        ):
            sweep = ast.literal_eval(node.value)
            return {case: min(sweep[case]) for case in CASE_NAMES}
    raise SystemExit(f"FAIL: no SWEEP literal in {run_path}")


class Store:
    def __init__(self, library: Path, dsn: str):
        self.lib = ctypes.CDLL(str(library))
        self.lib.kvspaceConnect.argtypes = [ctypes.c_char_p]
        self.lib.kvspaceConnect.restype = ctypes.c_void_p
        self.lib.kvspaceClose.argtypes = [ctypes.c_void_p]
        self.lib.kvspaceGet.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.lib.kvspaceGet.restype = ctypes.c_int
        self.lib.kvspaceListLen.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int32),
        ]
        self.lib.kvspaceListLen.restype = ctypes.c_int
        self.lib.kvspaceListAt.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int32,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.lib.kvspaceListAt.restype = ctypes.c_int
        self.handle = self.lib.kvspaceConnect(dsn.encode())
        if not self.handle:
            raise SystemExit(f"FAIL: kvspaceConnect {dsn}")

    def close(self):
        self.lib.kvspaceClose(self.handle)

    def children(self, prefix: str) -> list[str]:
        count = ctypes.c_int32()
        if self.lib.kvspaceListLen(
            self.handle, prefix.encode(), 0, 0, ctypes.byref(count)
        ) != 0:
            return []
        out = []
        for index in range(count.value):
            buf = (ctypes.c_uint8 * 1024)()
            length = ctypes.c_uint32()
            if self.lib.kvspaceListAt(
                self.handle,
                prefix.encode(),
                0,
                0,
                index,
                buf,
                len(buf),
                ctypes.byref(length),
            ) == 0:
                out.append(bytes(buf[: length.value]).decode())
        return out

    def raw(self, key: str) -> bytes | None:
        data = ctypes.POINTER(ctypes.c_uint8)()
        length = ctypes.c_uint32()
        if self.lib.kvspaceGet(
            self.handle, key.encode(), 0, ctypes.byref(data), ctypes.byref(length)
        ) != 0:
            return None
        return bytes(data[: length.value])

    @staticmethod
    def langtype(value: bytes) -> str:
        body_len = int.from_bytes(value[:6], "little")
        offset = 8 + body_len
        attr_len = int.from_bytes(value[offset : offset + 2], "little")
        pos = offset + 2
        end = offset + attr_len
        while pos + 3 <= end:
            tag = value[pos]
            size = int.from_bytes(value[pos + 1 : pos + 3], "little")
            pos += 3
            if pos + size > end:
                break
            if tag == 1:
                return value[pos : pos + size].decode()
            pos += size
        return ""

    def body(self, key: str) -> tuple[bytes, str] | None:
        value = self.raw(key)
        if value is None:
            return None
        body_len = int.from_bytes(value[:6], "little")
        return value[8 : 8 + body_len], self.langtype(value)


def walk(store: Store, prefix: str):
    for child in store.children(prefix):
        if child.endswith("/"):
            yield from walk(store, prefix + child)
        else:
            yield prefix + child


def layout_ops(store: Store) -> set[str]:
    declared = set()
    for child in store.children("/lib/"):
        name = child.rstrip("/")
        got = store.body("/lib/" + name)
        if got and got[1] == "def rwir":
            declared.add(name)

    ops = set()
    for key in walk(store, "/lib/"):
        slot = SLOT_RE.search(key)
        if slot is None or slot.group(2) != "0":
            continue
        got = store.body(key)
        if got is None or got[1] != "rwir":
            continue
        body = got[0]
        if len(body) >= 6:
            ops.add(body[5:].decode())
    return ops, declared


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", required=True, type=Path)
    parser.add_argument("--byteseek", required=True, type=Path)
    parser.add_argument("--myrwircap-dump", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    root = args.source_root.resolve()
    library = root / "lib" / "libkvspace.so.1"
    for path in (args.layout, args.byteseek, args.myrwircap_dump, library):
        if not path.exists():
            raise SystemExit(f"FAIL: missing test dependency: {path}")

    scales = min_scales(root / "benchmark" / "run.py")
    env = {
        **os.environ,
        "LD_LIBRARY_PATH": f"{root / 'lib'}:{root / 'lib' / 'kvspace'}",
    }

    with tempfile.TemporaryDirectory(prefix="kvlang-layout-ops-") as tmp:
        dsn = f"fs://{tmp}/store"
        env["KVSPACE"] = dsn
        publish = subprocess.run(
            [str(args.byteseek), "isa", "publish"],
            env=env,
            capture_output=True,
            text=True,
        )
        if publish.returncode != 0:
            raise SystemExit(
                f"FAIL: extension registry publish: {publish.stderr.strip()[:300]}"
            )

        for case in CASE_NAMES:
            source = root / "benchmark" / "cases" / case / f"{case}.kv"
            rendered = Path(tmp) / f"{case}.kv"
            rendered.write_text(
                source.read_text(encoding="utf-8").replace("__SCALE__", str(scales[case])),
                encoding="utf-8",
            )
            result = subprocess.run(
                [str(args.layout), str(rendered), dsn],
                env=env,
                capture_output=True,
                text=True,
            )
            if result.returncode != 0:
                raise SystemExit(
                    f"FAIL: layout {case}: {result.stderr.strip()[:300]}"
                )

        store = Store(library, dsn)
        try:
            ops, declared = layout_ops(store)
        finally:
            store.close()

    if not ops:
        raise SystemExit("FAIL: layout produced no rwir opcodes")

    table = set(
        subprocess.run(
            [str(args.myrwircap_dump)], capture_output=True, text=True, check=True
        ).stdout.split()
    )
    if not table:
        raise SystemExit("FAIL: myrwircap table is empty")

    diff = sorted(ops - table - declared)
    if diff:
        print(f"FAIL: layout opcodes outside myrwircap ({len(diff)}):")
        for op in diff:
            print(f"  {op}")
        return 1
    print(
        f"OK layout ops ⊆ myrwircap (layout={len(ops)}, "
        f"declared-ext={len(declared & ops)}, table={len(table)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
