from __future__ import annotations
import argparse, itertools, os, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYOUT_BIN = str(ROOT / "bin" / "pathlanglayout")
TERM_BIN = str(ROOT / "bin" / "pathseek")
_C_DSN = os.environ.get("PATHSPACE", "shm://")
ERROR_CASES = Path(__file__).resolve().parent
RED, GREEN, NC = "\033[0;31m", "\033[0;32m", "\033[0m"

_BACKEND_TMP: tempfile.TemporaryDirectory[str] | None = None
_CASE_SEQ = itertools.count()
_LAST_BACKEND: str | None = None

def _drop_backend(dsn: str) -> None:
    path = dsn.split("://", 1)[1]
    if dsn.startswith("shm://"):
        for base in (path, path + ".isa"):
            for suf in ("", ".sbo.data", ".sbo.head", ".garb", ".gen"):
                try:
                    os.unlink(base + suf)
                except OSError:
                    pass
    elif dsn.startswith("fs://"):
        shutil.rmtree(path, ignore_errors=True)
        shutil.rmtree(path + ".isa", ignore_errors=True)

def _case_dsn() -> str:
    global _LAST_BACKEND
    if _BACKEND_TMP is None:
        return _C_DSN
    if _LAST_BACKEND:
        _drop_backend(_LAST_BACKEND)
        _LAST_BACKEND = None
    n = next(_CASE_SEQ)
    dsn = (f"shm://{_BACKEND_TMP.name}/{n}.shm"
           if _C_DSN.startswith("shm://") else f"fs://{_BACKEND_TMP.name}/{n}.fs")
    _LAST_BACKEND = dsn
    return dsn

def discover(root: Path) -> list[Path]:
    return sorted(root.rglob("*.kv"))

def parse_expects(f: Path) -> list[str]:
    pats = []
    in_block = False
    with open(f) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("// expected"):
                in_block = True
                continue
            if in_block:
                if line.startswith("//   ") or line.startswith("// \t"):
                    p = line[3:].strip()
                    if p:
                        pats.append(p)
                else:
                    break
    return pats

def _unescape_logx(stderr: str) -> str:
    return stderr.replace('\\"', '"')

def collect_errors(rel: str) -> str:
    dsn = _case_dsn()
    env = {**os.environ, "PATHSPACE": dsn}
    boot = subprocess.run([TERM_BIN, "-c", 'print("boot")'], capture_output=True,
                          text=True, timeout=60, cwd=str(ROOT), env=env)
    stderr = _unescape_logx(boot.stderr)
    layout = subprocess.run([LAYOUT_BIN, rel, dsn], capture_output=True,
                            text=True, timeout=60, cwd=str(ROOT), env=env)
    stderr += ("\n" if stderr else "") + _unescape_logx(layout.stderr)
    if layout.returncode == 0:
        crun = subprocess.run([TERM_BIN, "test"], capture_output=True,
                              text=True, timeout=120, cwd=str(ROOT), env=env)
        stderr += "\n" + _unescape_logx(crun.stderr)
    return stderr

def check(pat: str, errors: str) -> bool:
    for ln in errors.splitlines():
        if pat in ln:
            return True
    return False

def main() -> int:
    global _BACKEND_TMP, _C_DSN, _LAST_BACKEND, LAYOUT_BIN, TERM_BIN
    ap = argparse.ArgumentParser(description="pathlang error_cases verification")
    ap.add_argument("--pathspace", default=None,
                    help="Backend DSN prefix (e.g. shm:// or fs://)")
    ap.add_argument("--layout-bin", default=LAYOUT_BIN, help="pathlanglayout 路径")
    ap.add_argument("--term-bin", default=TERM_BIN, help="pathseek 路径")
    args = ap.parse_args()
    LAYOUT_BIN, TERM_BIN = args.layout_bin, args.term_bin
    if args.pathspace:
        _C_DSN = args.pathspace
    if _C_DSN.startswith(("shm://", "fs://")):
        _BACKEND_TMP = tempfile.TemporaryDirectory(prefix="pathlang-err-")
    try:
        files = [f for f in discover(ERROR_CASES) if f.suffix == ".kv"]
        if not files:
            print(f"{RED}No error case files found{NC}")
            return 1

        passed, total = 0, 0
        for f in files:
            rel = str(f.relative_to(ROOT))
            expects = parse_expects(f)
            if not expects:
                print(f"{RED}FAIL {rel}: missing // expected header{NC}")
                continue

            try:
                errors = collect_errors(rel)
            except subprocess.TimeoutExpired:
                print(f"{RED}FAIL {rel}: timed out{NC}")
                continue
            except Exception as e:
                print(f"{RED}FAIL {rel}: exception {e}{NC}")
                continue

            all_ok = True
            for pat in expects:
                total += 1
                if check(pat, errors):
                    passed += 1
                else:
                    all_ok = False
                    print(f"{RED}FAIL {rel}: unmatched {pat!r}{NC}")
                    for ln in errors.splitlines()[-10:]:
                        print(f"    actual: {ln}")

            if all_ok:
                print(f"{GREEN}PASS {rel}: ({passed}/{total}){NC}")

        print(f"\n{GREEN}== error_cases: {passed}/{total} PASSED, {total-passed} FAILED =={NC}")
        return 0 if passed == total else 1
    finally:
        if _LAST_BACKEND:
            _drop_backend(_LAST_BACKEND)
            _LAST_BACKEND = None
        if _BACKEND_TMP is not None:
            _BACKEND_TMP.cleanup()
            _BACKEND_TMP = None

if __name__ == "__main__":
    sys.exit(main())
