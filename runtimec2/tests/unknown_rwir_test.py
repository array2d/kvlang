#!/usr/bin/env python3
"""建段期硬错：未知 rwir（不在 myrwircap 且 `/lib/<op>` 无 def rwir）必须失败。

把 layout 产物里一条 `print` 指令改名为未声明的 `bogus·rwir`，再用 m1 驱动执行：
期望非零退出，且 stderr 明确点名该 opcode；不允许静默走 handoff。
"""

from __future__ import annotations

import argparse
import ctypes
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = """rwfunc test() -> () {
    x = 1
    print(x)
}
"""


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
        self.lib.kvspaceSetValue.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint32,
            ctypes.c_char_p,
            ctypes.c_uint32,
        ]
        self.lib.kvspaceSetValue.restype = ctypes.c_int
        self.lib.kvspaceXvalueHeadNewCode.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_uint64,
            ctypes.c_uint64,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_uint64),
        ]
        self.lib.kvspaceXvalueHeadNewCode.restype = ctypes.c_int
        self.libc = ctypes.CDLL(None)
        self.libc.free.argtypes = [ctypes.c_void_p]
        self.handle = self.lib.kvspaceConnect(dsn.encode())
        if not self.handle:
            raise SystemExit(f"FAIL: kvspaceConnect {dsn}")

    def close(self):
        self.lib.kvspaceClose(self.handle)

    def body(self, key: str) -> bytes | None:
        data = ctypes.POINTER(ctypes.c_uint8)()
        length = ctypes.c_uint32()
        if self.lib.kvspaceGet(
            self.handle, key.encode(), 0, ctypes.byref(data), ctypes.byref(length)
        ) != 0:
            return None
        value = bytes(data[: length.value])
        body_len = int.from_bytes(value[:6], "little")
        return value[8 : 8 + body_len]

    def set_code(self, key: str, body: bytes):
        source = (ctypes.c_uint8 * len(body)).from_buffer_copy(body)
        wire = ctypes.POINTER(ctypes.c_uint8)()
        wire_len = ctypes.c_uint64()
        if (
            self.lib.kvspaceXvalueHeadNewCode(
                source, len(body), len(body), b"rwir", ctypes.byref(wire),
                ctypes.byref(wire_len),
            )
            != 0
        ):
            raise SystemExit(f"FAIL: encode {key}")
        try:
            error = ctypes.create_string_buffer(256)
            if (
                self.lib.kvspaceSetValue(
                    self.handle, key.encode(), wire, wire_len.value,
                    error, len(error),
                )
                != 0
            ):
                raise SystemExit(f"FAIL: set {key}: {error.value.decode()}")
        finally:
            self.libc.free(wire)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", required=True, type=Path)
    parser.add_argument("--m1", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    args = parser.parse_args()

    root = args.source_root.resolve()
    library = root / "lib" / "libkvspace.so.1"
    for path in (args.layout, args.m1, library):
        if not path.exists():
            raise SystemExit(f"FAIL: missing test dependency: {path}")

    env = {
        **os.environ,
        "LD_LIBRARY_PATH": f"{root / 'lib'}:{root / 'lib' / 'kvspace'}",
    }
    unknown = "bogus·rwir"

    with tempfile.TemporaryDirectory(prefix="kvlang-unknown-rwir-") as tmp:
        dsn = f"shm://{tmp}/store.shm"
        source = Path(tmp) / "case.kv"
        source.write_text(SOURCE, encoding="utf-8")
        layout = subprocess.run(
            [str(args.layout), str(source), dsn], env=env,
            capture_output=True, text=True,
        )
        if layout.returncode != 0:
            raise SystemExit(f"FAIL: layout: {layout.stderr.strip()[:300]}")

        store = Store(library, dsn)
        try:
            # 扩展 rwir 必须先注册（否则 print 本身就该硬错）。
            publish = subprocess.run(
                [
                    str(root / "bin" / "byteseek"), "isa", "publish",
                ],
                env={**env, "KVSPACE": dsn},
                capture_output=True,
                text=True,
            )
            if publish.returncode != 0:
                raise SystemExit(
                    f"FAIL: extension publish: {publish.stderr.strip()[:300]}"
                )
            patched = False
            for s0 in range(1, 64):
                key = f"/lib/test/[{s0},0]"
                body = store.body(key)
                if body is None or len(body) <= 5:
                    continue
                if body[5:].decode() != "print":
                    continue
                store.set_code(key, body[:5] + unknown.encode())
                patched = True
                break
        finally:
            store.close()

        if not patched:
            raise SystemExit("FAIL: no print instruction found in /lib/test")

        run = subprocess.run(
            [str(args.m1), dsn, "test"], env=env, capture_output=True, text=True
        )

    expected = f'unknown rwir "{unknown}"'
    if run.returncode == 0:
        raise SystemExit("FAIL: unknown rwir was silently accepted")
    if expected not in run.stderr:
        raise SystemExit(
            f"FAIL: expected {expected!r} in stderr, got: {run.stderr.strip()[:300]}"
        )
    print(f"OK unknown rwir rejected: {expected}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
