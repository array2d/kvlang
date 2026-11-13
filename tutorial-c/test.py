
from __future__ import annotations
import os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT.parent / "build" / "tutorial-c"
PATHLANG_ROOT = ROOT.parent

INCS = [str(PATHLANG_ROOT / "include")]
PATHSPACE_INCLUDE = ""
LIB = ""
XVALUE_LIB = str(PATHLANG_ROOT / "lib" / "libpathlang_xvalue.a")

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
    return ["-L", LIB, "-lpathspace", XVALUE_LIB]

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
                         f"{LIB}/libpathspace.so"])

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
    if not run("Rust: compile", ["rustc", str(src), "-L", LIB, "-l", "pathspace",
                                 "-o", str(bin)]):
        return
    run("Rust: run", [str(bin)], {"LD_LIBRARY_PATH": LIB})

if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="tutorial-c：存储 ABI + 语言层的多语言直调")
    ap.add_argument("--pathspace-include", required=True,
                    help="pathspace 仓的 include/（提供 <pathspace/*.h>）")
    ap.add_argument("--pathspace-lib", required=True,
                    help="内含 libpathspace 的目录")
    ap.add_argument("--xvalue-lib", default=XVALUE_LIB,
                    help="libpathlang_xvalue.a 路径（默认本仓 lib/）")
    args = ap.parse_args()
    PATHSPACE_INCLUDE = args.pathspace_include
    LIB = args.pathspace_lib
    XVALUE_LIB = args.xvalue_lib
    INCS = [str(PATHLANG_ROOT / "include"), PATHSPACE_INCLUDE]

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
