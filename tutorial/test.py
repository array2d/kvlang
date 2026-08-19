#!/usr/bin/env python3
"""kvlang tutorial test — 从 .kv 文件 # 期望输出 头注释自动生成测试。"""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

RED, GREEN, YELLOW, NC = "\033[0;31m", "\033[0;32m", "\033[1;33m", "\033[0m"
ROOT = Path(__file__).resolve().parent.parent
KV = str(ROOT / "kvlang")
RUST_BIN = str(ROOT / "target" / "debug" / "kvlang")
LAYOUT_BIN = os.environ.get("KVLANG_LAYOUT_BIN", str(ROOT / "bin" / "layout_file"))
TERM_BIN = os.environ.get("KVLANG_TERM_BIN", str(ROOT / "bin" / "kvlang"))
_C_DSN = os.environ.get("KVSPACE", "redis://127.0.0.1:6379")
SHM_PATH = "/tmp/kvlang_rust_test"
FAIL_CSV = (ROOT / "tutorial" / "test_failures.csv").resolve()
BENCH_CSV = (ROOT / "tutorial" / "benchmark.csv").resolve()
MODULE = sys.modules[__name__]

_KV_ENV = {**os.environ, "KVSPACE": os.environ.get("KVSPACE", "goheap://")}
_SHM_ENV = {**os.environ, "KVSPACE": f"shm://{SHM_PATH}",
            "LD_LIBRARY_PATH": str(ROOT.parent / "kvspace-c" / "build"),
            "KVSPACE_SHM": SHM_PATH}


def discover(root: Path) -> list[Path]:
    return sorted(root.rglob("*.kv"))


def parse_expects(f: Path) -> list[str]:
    """从 .kv 文件头提取 # 期望输出 行，去掉注释前缀和尾部说明。"""
    pats = []
    in_block = False
    with open(f) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("# 期望输出"):
                in_block = True
                continue
            if in_block:
                if line.startswith("#   ") or line.startswith("# \t"):
                    p = line[2:].strip()
                    p = re.sub(r"\s*\(.*\)\s*$", "", p)
                    if p:
                        pats.append(p)
                elif not line.startswith("#"):
                    break
    return pats


def _needs_skip(f: Path) -> bool:
    """文件头含 # extern 或 # wip 标记 = 不参与自动测试（外部 rwirext / 待实现特性）。"""
    with open(f) as fh:
        for line in fh:
            if line.startswith("# extern") or line.startswith("# wip"):
                return True
            if line and not line.startswith("#"):
                return False
    return False


def _flush_redis() -> None:
    try:
        subprocess.run(
            ["redis-cli", "-p", "6379", "FLUSHALL"],
            capture_output=True, timeout=5,
        )
    except FileNotFoundError:
        pass


def _extract_bench_us(stdout: str) -> tuple[str, float]:
    """从 stdout 提取 __bench_ns: 或 __bench_us: 行，返回 (清理后stdout, us)。"""
    bench_us = 0.0
    lines = stdout.split("\n")
    clean = []
    for line in lines:
        if line.startswith("__bench_ns:"):
            bench_us = float(line.split(":")[1].strip()) / 1000.0
        elif line.startswith("__bench_us:"):
            bench_us = float(line.split(":")[1].strip())
        else:
            clean.append(line)
    return "\n".join(clean), bench_us


def _timed_run(command: list[str], env: dict | None = None) -> tuple[subprocess.CompletedProcess[str], float]:
    result = subprocess.run(command, capture_output=True, text=True,
                            timeout=60, cwd=str(ROOT), env=env)
    _, us = _extract_bench_us(result.stdout)
    return result, us / 1000.0  # 返回 ms，兼容旧格式


def _benchmark_error(results: dict[str, subprocess.CompletedProcess[str]],
                     expects: list[str]) -> str:
    for name, result in results.items():
        if result.returncode != 0:
            return f"{name} exited with status {result.returncode}"
    outputs = {}
    for name, result in results.items():
        clean, _ = _extract_bench_us(result.stdout)
        outputs[name] = clean
    for name, output in outputs.items():
        if any(expected not in output for expected in expects):
            return f"{name} output does not match expected output"
    if len(set(outputs.values())) != 1:
        return "program outputs differ"
    return ""


def _invalid_row(f: Path) -> dict[str, str]:
    return {
        "file": str(f.relative_to(ROOT)),
        "kvlang_ms": "invalid",
        "python_ms": "invalid",
        "c_ms": "invalid",
    }


def _benchmark_file(f: Path, expects: list[str]) -> tuple[dict[str, str], str]:
    rel = str(f.relative_to(ROOT))
    invalid = _invalid_row(f)
    with tempfile.TemporaryDirectory(prefix="kvlang-bench-") as tmp:
        executable = Path(tmp) / "program"
        try:
            compiled = subprocess.run(
                ["gcc", "-O3", str(f.with_suffix(".c")), "-o", str(executable), "-lm"],
                capture_output=True, text=True, timeout=60, cwd=str(ROOT),
            )
        except FileNotFoundError:
            return invalid, "gcc not found"
        except subprocess.TimeoutExpired:
            return invalid, "C compilation timed out"
        if compiled.returncode != 0:
            return invalid, "C compilation failed"

        try:
            _flush_redis()
            kv_result, kv_ms = _timed_run([KV, rel], env=_KV_ENV)
            py_result, py_ms = _timed_run([sys.executable, str(f.with_suffix(".py"))])
            c_result, c_ms = _timed_run([str(executable)])
        except FileNotFoundError as exc:
            return invalid, f"command not found: {exc.filename}"
        except subprocess.TimeoutExpired:
            return invalid, "program timed out"

    error = _benchmark_error(
        {"kvlang": kv_result, "python": py_result, "c": c_result}, expects,
    )
    if error:
        return invalid, error
    return {
        "file": rel,
        "kvlang_ms": f"{kv_ms:.3f}",
        "python_ms": f"{py_ms:.3f}",
        "c_ms": f"{c_ms:.3f}",
    }, ""


def run_benchmarks(files: list[Path], errorexit: bool = False) -> int:
    rows = []
    skipped = invalid = 0
    for f in files:
        if not f.with_suffix(".py").is_file() or not f.with_suffix(".c").is_file():
            skipped += 1
            continue
        expects = parse_expects(f)
        if expects:
            row, error = _benchmark_file(f, expects)
        else:
            row, error = _invalid_row(f), "missing # 期望输出"
        rows.append(row)
        rel = str(f.relative_to(ROOT))
        if error:
            invalid += 1
            print(f"{RED}❌ bench {rel}: invalid — {error}{NC}")
            if errorexit:
                break
        else:
            print(f"{GREEN}✅ bench {rel}{NC}")

    _write_benchmark_csv(rows)
    print(f"{YELLOW}══ VALID:{len(rows) - invalid}  INVALID:{invalid}  SKIP:{skipped} ══{NC}")
    print(f"report: {BENCH_CSV}")
    return invalid


def _rust_test_file(f: Path, expects: list[str], env: dict) -> tuple[bool, str]:
    """Layout .kv to SHM, run Rust runtime, check output against expects."""
    rel = str(f.relative_to(ROOT))
    # Clean old SHM
    try: os.unlink(SHM_PATH)
    except OSError: pass

    # Step 1: Go layout → SHM
    layout = subprocess.run([KV, "layout", rel], capture_output=True, text=True,
                            timeout=30, cwd=str(ROOT), env=env)
    if layout.returncode != 0:
        return False, f"layout failed: {layout.stderr.strip()[:100]}"

    # Step 2: List available functions in /lib/, try each
    from pathlib import Path as _Path
    func_names = [_Path(f).stem, "main", "init"]
    try:
        ls = subprocess.run(
            [os.path.expanduser("~/.local/bin/kvspace"), "--kvspace", f"shm://{SHM_PATH}",
             "list", "/lib/"], capture_output=True, text=True, timeout=5, env=env)
        for line in ls.stdout.strip().split('\n'):
            name = line.strip().split()[0].rstrip('/')
            if name and name not in func_names:
                func_names.append(name)
    except Exception: pass

    rust = None
    for fn in func_names:
        try:
            rust = subprocess.run([RUST_BIN, fn], capture_output=True, text=True,
                                  timeout=30, cwd=str(ROOT), env=env)
        except FileNotFoundError:
            return False, f"rust binary not found at {RUST_BIN}"
        if rust.returncode == 0: break
    if rust is None or rust.returncode != 0:
        return False, f"rust exit {rust.returncode}: {rust.stderr.strip()[:100] if rust else 'no attempt'}"

    # Step 3: Check output
    for pat in expects:
        if pat not in rust.stdout:
            return False, f"want {pat!r}"
    return True, rust.stdout[:200]


def _detect_entry(out: str) -> str:
    m = re.search(r"ENTRY=(\S+)", out)
    return m.group(1) if m else "init"


def _c_test_file(f: Path, expects: list[str], env: dict) -> tuple[bool, str]:
    """Rust layout → kvspace(redis/shm) → C runtime，检查输出。"""
    rel = str(f.relative_to(ROOT))
    if _C_DSN.startswith("shm://"):
        try:
            os.unlink(_C_DSN[len("shm://"):])
        except OSError:
            pass
    elif _C_DSN.startswith("fs://"):
        shutil.rmtree(_C_DSN[len("fs://"):], ignore_errors=True)
    else:
        _flush_redis()
    layout = subprocess.run([LAYOUT_BIN, rel, _C_DSN], capture_output=True, text=True,
                            timeout=60, cwd=str(ROOT), env=env)
    if layout.returncode != 0:
        return False, f"layout failed: {layout.stderr.strip()[:100]}"
    entry = _detect_entry(layout.stdout)
    try:
        crun = subprocess.run([TERM_BIN, entry], capture_output=True, text=True,
                              timeout=120, cwd=str(ROOT), env={**env, "KVSPACE": _C_DSN})
    except subprocess.TimeoutExpired:
        return False, "timeout"
    if crun.returncode != 0:
        return False, f"term exit {crun.returncode}: {crun.stderr.strip()[:100]}"
    for pat in expects:
        if pat not in crun.stdout:
            return False, f"want {pat!r}"
    return True, crun.stdout[:200]


def main():
    ap = argparse.ArgumentParser(description="tutorial test")
    ap.add_argument("--filter", default="", help="按路径子串过滤（如 14-numpy / 11-string/01）")
    ap.add_argument("--no-build", action="store_true", help="skip make build")
    ap.add_argument("--errorexit", action="store_true", help="exit on first error")
    ap.add_argument("--bench", action="store_true", help="benchmark matching .kv/.py/.c files")
    ap.add_argument("--runtime", default="c", choices=("go", "rust", "c"),
                    help="runtime to test (default: c; env KVLANG_RUNTIME=c 等价 --runtime c)")
    args = ap.parse_args()
    if os.environ.get("KVLANG_RUNTIME") == "c":
        args.runtime = "c"

    if not args.no_build and args.runtime == "go":
        r = subprocess.run(["make", "build"], capture_output=True, text=True,
                           timeout=120, cwd=str(ROOT))
        if r.returncode != 0:
            print(f"{RED}❌ make build failed:{NC}\n{r.stderr}")
            sys.exit(1)
        print(f"{GREEN}✅ build ok{NC}")

    files = [f for f in discover(ROOT / "tutorial")
             if args.filter in str(f)]

    print(f"kvlang: {os.path.abspath(KV)}")
    prefix = "🔧 rust" if args.runtime == "rust" else "kvlang"

    if args.bench:
        sys.exit(1 if run_benchmarks(files, args.errorexit) else 0)

    if not files:
        print(f"{YELLOW}no .kv files found{NC}")
        sys.exit(0)

    passed = failed = skipped = 0
    failures: list[dict] = []
    for f in files:
        rel = str(f.relative_to(ROOT))
        if _needs_skip(f):
            print(f"{YELLOW}⏭ {prefix} {rel}: extern/wip{NC}")
            skipped += 1
            continue
        expects = parse_expects(f)
        if not expects:
            continue
        try:
            if args.runtime == "rust":
                ok, detail = _rust_test_file(f, expects, _SHM_ENV)
                if ok:
                    print(f"{GREEN}✅ {prefix} {rel}{NC}")
                    passed += 1
                else:
                    print(f"{RED}❌ {prefix} {rel}: {detail}{NC}")
                    failures.append({"file": rel, "reason": detail, "expected": "", "stdout": ""})
                    failed += 1
            elif args.runtime == "c":
                ok, detail = _c_test_file(f, expects, _KV_ENV)
                if ok:
                    print(f"{GREEN}✅ {prefix} {rel}{NC}")
                    passed += 1
                else:
                    print(f"{RED}❌ {prefix} {rel}: {detail}{NC}")
                    failures.append({"file": rel, "reason": detail, "expected": "", "stdout": ""})
                    failed += 1
            else:
                _flush_redis()
                r = subprocess.run([KV, rel], capture_output=True, text=True,
                                   timeout=60, cwd=str(ROOT), env=_KV_ENV)
                all_ok = True
                if r.returncode != 0:
                    all_ok = False
                    print(f"{RED}❌ {prefix} {rel}: exit code {r.returncode}{NC}")
                    failures.append({"file": rel, "reason": f"exit code {r.returncode}",
                                     "expected": "", "stdout": r.stdout[:500]})
                if r.stderr.strip():
                    all_ok = False
                    print(f"{RED}❌ {prefix} {rel}: stderr — {r.stderr.strip()[:200]}{NC}")
                    found = [x for x in failures if x["file"] == rel and x["reason"].startswith("exit code")]
                    if not found:
                        failures.append({"file": rel, "reason": f"stderr: {r.stderr.strip()[:200]}",
                                         "expected": "", "stdout": r.stdout[:500]})
                for pat in expects:
                    if pat not in r.stdout:
                        all_ok = False
                        print(f"{RED}❌ {prefix} {rel}: want {pat!r}{NC}")
                        print(f"   stdout: {r.stdout[:200]}")
                        failures.append({"file": rel, "reason": "output mismatch",
                                         "expected": pat, "stdout": r.stdout[:500]})
                if all_ok:
                    print(f"{GREEN}✅ {prefix} {rel}{NC}")
                    passed += 1
                else:
                    failed += 1
                    if args.errorexit:
                        _write_csv(failures)
                        print(f"\n{YELLOW}errorexit: stopping at first failure{NC}")
                        sys.exit(1)
            if args.errorexit and failed:
                _write_csv(failures)
                print(f"\n{YELLOW}errorexit: stopping at first failure{NC}")
                sys.exit(1)
        except subprocess.TimeoutExpired:
            print(f"{RED}❌ {prefix} {rel}: timeout{NC}")
            failed += 1
            failures.append({"file": rel, "reason": "timeout", "expected": "", "stdout": ""})
            if args.errorexit:
                _write_csv(failures)
                print(f"\n{YELLOW}errorexit: stopping at first failure{NC}")
                sys.exit(1)

    _write_csv(failures)
    print(f"{YELLOW}══ {GREEN}PASS:{passed}{YELLOW}  {RED}FAIL:{failed}{YELLOW}  SKIP:{skipped} ══{NC}")
    print(f"report: {FAIL_CSV}")
    sys.exit(0 if failed == 0 else 1)


def _write_csv(failures: list[dict]) -> None:
    if not failures:
        FAIL_CSV.write_text("file,reason,expected,stdout\n", encoding="utf-8")
        return
    with open(FAIL_CSV, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["file", "reason", "expected", "stdout"])
        w.writeheader()
        for row in failures:
            w.writerow(row)


def _write_benchmark_csv(rows: list[dict[str, str]]) -> None:
    with open(BENCH_CSV, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(
            fh,
            fieldnames=["file", "kvlang_ms", "python_ms", "c_ms"],
            lineterminator="\n",
        )
        w.writeheader()
        w.writerows(rows)


class BenchmarkTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.tutorial = self.root / "tutorial"
        self.tutorial.mkdir()
        self.kvlang = self.root / "kvlang"
        self.kvlang.write_text("#!/bin/sh\nprintf 'answer 42\\n'\n", encoding="utf-8")
        self.kvlang.chmod(0o755)
        self.patchers = [
            mock.patch.object(MODULE, "ROOT", self.root),
            mock.patch.object(MODULE, "KV", str(self.kvlang)),
            mock.patch.object(MODULE, "FAIL_CSV", self.tutorial / "test_failures.csv"),
            mock.patch.object(MODULE, "BENCH_CSV", self.tutorial / "benchmark.csv"),
            mock.patch.object(MODULE, "_flush_redis"),
            mock.patch("builtins.print"),
        ]
        for patcher in self.patchers:
            patcher.start()

    def tearDown(self):
        for patcher in reversed(self.patchers):
            patcher.stop()
        self.tempdir.cleanup()

    def write_fixture(self, python_output="answer 42", include_c=True):
        source = self.tutorial / "case.kv"
        source.write_text("# 期望输出:\n#   answer 42\n", encoding="utf-8")
        source.with_suffix(".py").write_text(
            f"print({python_output!r})\n", encoding="utf-8",
        )
        if include_c:
            source.with_suffix(".c").write_text(
                '#include <stdio.h>\nint main(void) { puts("answer 42"); return 0; }\n',
                encoding="utf-8",
            )
        return source

    def read_rows(self):
        with open(BENCH_CSV, newline="", encoding="utf-8") as fh:
            return list(csv.DictReader(fh))

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_writes_timings_for_matching_outputs(self):
        source = self.write_fixture()

        self.assertEqual(run_benchmarks([source]), 0)

        rows = self.read_rows()
        self.assertEqual(len(rows), 1)
        self.assertEqual(
            list(rows[0]), ["file", "kvlang_ms", "python_ms", "c_ms"],
        )
        self.assertEqual(rows[0]["file"], "tutorial/case.kv")
        self.assertNotIn(b"\r\n", BENCH_CSV.read_bytes())
        for field in ("kvlang_ms", "python_ms", "c_ms"):
            self.assertGreaterEqual(float(rows[0][field]), 0)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_marks_output_mismatch_invalid(self):
        source = self.write_fixture(python_output="wrong")

        self.assertEqual(run_benchmarks([source]), 1)

        row = self.read_rows()[0]
        self.assertEqual(
            [row["kvlang_ms"], row["python_ms"], row["c_ms"]],
            ["invalid", "invalid", "invalid"],
        )

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_marks_different_outputs_invalid(self):
        source = self.write_fixture(python_output="answer 42 ")

        self.assertEqual(run_benchmarks([source]), 1)

        row = self.read_rows()[0]
        self.assertEqual(row["python_ms"], "invalid")

    def test_skips_missing_counterpart(self):
        source = self.write_fixture(include_c=False)

        self.assertEqual(run_benchmarks([source]), 0)
        self.assertEqual(self.read_rows(), [])

    def test_marks_missing_expected_output_invalid(self):
        source = self.write_fixture()
        source.write_text('println("answer 42")\n', encoding="utf-8")

        self.assertEqual(run_benchmarks([source]), 1)
        self.assertEqual(self.read_rows()[0]["kvlang_ms"], "invalid")

    def test_marks_nonzero_exit_invalid(self):
        failed = subprocess.CompletedProcess(
            [], 1, stdout="answer 42\n", stderr="warning\n",
        )

        self.assertIn(
            "exited with status 1",
            _benchmark_error(
                {"kvlang": failed, "python": failed, "c": failed}, ["answer 42"],
            ),
        )

    def test_stderr_does_not_invalidate_successful_run(self):
        result = subprocess.CompletedProcess(
            [], 0, stdout="answer 42\n", stderr="warning\n",
        )

        self.assertEqual(
            _benchmark_error(
                {"kvlang": result, "python": result, "c": result}, ["answer 42"],
            ),
            "",
        )

    def test_bench_honors_filter_and_no_build(self):
        keep = self.tutorial / "keep.kv"
        drop = self.tutorial / "drop.kv"
        with (
            mock.patch.object(MODULE, "discover", return_value=[drop, keep]),
            mock.patch.object(MODULE, "run_benchmarks", return_value=0) as bench,
            mock.patch.object(subprocess, "run") as run,
            mock.patch.object(
                sys, "argv", ["test.py", "--bench", "--no-build", "--filter", "keep"],
            ),
            self.assertRaises(SystemExit) as exit_context,
        ):
            main()

        self.assertEqual(exit_context.exception.code, 0)
        bench.assert_called_once_with([keep], False)
        run.assert_not_called()

if __name__ == "__main__":
    main()
