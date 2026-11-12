#!/usr/bin/env python3
"""tutorial-c test — 多语言直调 kvspace 存储 ABI + kvlang 语言层。

路径约定（与 kvlang/xvalue/CMakeLists.txt 一致，不另造一套）：
  KVLANG_KVSPACE_INCLUDE_DIRECTORY  kvspace 仓的 include/（<kvspace/kvspace.h> 等）
  KVLANG_KVSPACE_LIB_DIRECTORY      hold libkvspace 的目录
  KVLANG_XVALUE_LIBRARY_PATH        libkvlang_xvalue.so

两个库缺一不可：存储 ABI 在 libkvspace，wire 构造器与 langtype 访问器在
libkvlang_xvalue。只链一个会在链接期报 undefined symbol。
"""

from __future__ import annotations
import os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT.parent / "build" / "tutorial-c"
KVLANG_ROOT = ROOT.parent


# 路径一律走**命令行参数**，不读环境变量：全生态只允许 `KVSPACE` 一个环境变量。
# 环境变量是隐式输入——不进命令行、不进日志，两个人跑同一条命令可能结果不同。
INCS = [str(KVLANG_ROOT / "include")]
KVSPACE_INCLUDE = ""
LIB = ""
XVALUE_LIB = str(KVLANG_ROOT / "lib" / "libkvlang_xvalue.so")

passed = 0
failed = 0


def run(label: str, cmd: list[str], env: dict = None) -> bool:
    global passed, failed
    r = subprocess.run(cmd, capture_output=True, timeout=600, env={**os.environ, **(env or {})})
    if r.returncode == 0:
        print(f"PASS  {label}")
        passed += 1
        return True
    print(f"FAIL  {label}")
    for stream in ("stdout", "stderr"):
        data = getattr(r, stream)
        if data:
            try:
                s = data.decode()[:400]
            except Exception:
                s = repr(data[:100])
            print(f"  {stream}: {s}")
    failed += 1
    return False


def include_flags() -> list[str]:
    flags: list[str] = []
    for path in INCS:
        flags += ["-I", path]
    return flags


def link_flags() -> list[str]:
    # 静态库写全路径：-lkvlang_xvalue 会在 /usr/lib 里找错对象。
    return ["-L", LIB, "-lkvspace", XVALUE_LIB]


def cc(src: Path, bin: Path, cpp: bool = False) -> bool:
    compiler = "g++" if cpp else "gcc"
    args = [compiler, str(src), *include_flags(), *link_flags(), "-o", str(bin)]
    if cpp:
        args = ["g++", "-std=c++17", str(src), *include_flags(), *link_flags(),
                "-o", str(bin)]
    return run(f"{compiler}: compile {src.name}", args)


def test_c():
    src, bin = ROOT / "01_basic.c", BUILD / "01_basic"
    if not cc(src, bin):
        return
    run("C: run", [str(bin)], {"LD_LIBRARY_PATH": LIB})


def test_cpp():
    src, bin = ROOT / "02_cpp.cpp", BUILD / "02_cpp"
    if not cc(src, bin, cpp=True):
        return
    run("C++: run", [str(bin)], {"LD_LIBRARY_PATH": LIB})


def test_python():
    run("Python: test", [sys.executable, str(ROOT / "03_python.py"),
                         f"{LIB}/libkvspace.so"])


def test_integrity():
    src, bin = ROOT / "05_integrity.c", BUILD / "05_integrity"
    if not cc(src, bin):
        return
    run("C: integrity run", [str(bin)], {"LD_LIBRARY_PATH": LIB})


def test_multiprocess():
    src, bin = ROOT / "06_multiprocess.c", BUILD / "06_multiprocess"
    if not cc(src, bin):
        return
    run("C: multiprocess run", [str(bin), "/tmp/kv_mp.shm"], {"LD_LIBRARY_PATH": LIB})


def test_rust():
    src, bin = ROOT / "04_rust.rs", BUILD / "04_rust"
    if not run("Rust: compile", ["rustc", str(src), "-L", LIB, "-l", "kvspace",
                                 "-o", str(bin)]):
        return
    run("Rust: run", [str(bin)], {"LD_LIBRARY_PATH": LIB})


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="tutorial-c：存储 ABI + 语言层的多语言直调")
    ap.add_argument("--kvspace-include", required=True,
                    help="kvspace 仓的 include/（提供 <kvspace/*.h>）")
    ap.add_argument("--kvspace-lib", required=True,
                    help="内含 libkvspace 的目录")
    ap.add_argument("--xvalue-lib", default=XVALUE_LIB,
                    help="libkvlang_xvalue.so 路径（默认本仓 lib/）")
    args = ap.parse_args()
    KVSPACE_INCLUDE = args.kvspace_include
    LIB = args.kvspace_lib
    XVALUE_LIB = args.xvalue_lib
    INCS = [str(KVLANG_ROOT / "include"), KVSPACE_INCLUDE]

    BUILD.mkdir(parents=True, exist_ok=True)

    test_c()
    test_cpp()
    test_python()
    test_integrity()
    test_multiprocess()
    if shutil.which("rustc"):
        test_rust()
    else:
        print("SKIP  Rust (rustc not found)")

    total = passed + failed
    print(f"\n{passed}/{total} passed")
    sys.exit(0 if failed == 0 else 1)
