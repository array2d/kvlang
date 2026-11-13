
from __future__ import annotations

import argparse
import csv
import itertools
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
KV = str(ROOT / "pathlang")
LAYOUT_BIN = str(ROOT / "bin" / "pathlanglayout")
TERM_BIN = str(ROOT / "bin" / "pathseek")
CASE_TIMEOUT = 120
_C_DSN = ""
FAIL_CSV = (ROOT / "tutorial" / "test_failures.csv").resolve()
BENCH_CSV = (ROOT / "tutorial" / "benchmark.csv").resolve()
MODULE = sys.modules[__name__]

_KV_ENV = {**os.environ}

_BACKEND_TMP = None
_CASE_SEQ = itertools.count()
_LAST_BACKEND = None

def _drop_backend(dsn: str) -> None:
    path = dsn.split("://", 1)[1]
    if dsn.startswith("shm://"):
        for base in (path, path + ".isa"):
            for suf in ("", ".sbo.data", ".sbo.head"):
                try:
                    os.unlink(base + suf)
                except OSError:
                    pass
    else:
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
    dsn = f"shm://{_BACKEND_TMP.name}/{n}.shm"
    _LAST_BACKEND = dsn
    return dsn

def discover(root: Path) -> list[Path]:
    return sorted(root.rglob("*.kv"))

def parse_expects(f: Path) -> list[str]:
    tail_note = re.compile(r"(?<=\S)\s+\([^()]*\)\s*$")
    pats = []
    in_block = False
    with open(f) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("// 期望输出") or line.startswith("// Expected output"):
                prefix = "// 期望输出" if line.startswith("// 期望输出") else "// Expected output"
                inline = line[len(prefix) :].lstrip()
                if inline.startswith(":"):
                    inline = inline[1:].strip()
                if inline:
                    for part in inline.split(","):
                        p = tail_note.sub("", part.strip()).strip()
                        if p:
                            pats.append(p)
                    continue
                in_block = True
                continue
            if in_block:
                if line.startswith("//   ") or line.startswith("// \t"):
                    p = tail_note.sub("", line[3:].strip()).strip()
                    if p:
                        pats.append(p)
                elif not line.startswith("//"):
                    break
    return pats

def _needs_skip(f: Path) -> bool:
    with open(f) as fh:
        for line in fh:
            if line.startswith("// extern") or line.startswith("// wip"):
                return True
            if line and not line.startswith("//"):
                return False
    return False

def _flush_backend() -> None:
    try:
        r = subprocess.run(
            ["pathspace", "clear"],
            capture_output=True, text=True, timeout=60,
            env={**os.environ, "PATHSPACE": _C_DSN},
        )
    except FileNotFoundError:
        raise SystemExit(
            "pathspace CLI not found in PATH; set PATH to the pathspace prefix bin"
        )
    if r.returncode != 0:
        raise SystemExit(
            f"pathspace clear failed ({r.returncode}): {r.stderr.strip()[:200]}"
        )

def _extract_bench_us(stdout: str) -> tuple[str, float]:
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
    return result, us / 1000.0

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
        "pathlang_ms": "invalid",
        "python_ms": "invalid",
        "c_ms": "invalid",
    }

def _benchmark_file(f: Path, expects: list[str]) -> tuple[dict[str, str], str]:
    rel = str(f.relative_to(ROOT))
    invalid = _invalid_row(f)
    with tempfile.TemporaryDirectory(prefix="pathlang-bench-") as tmp:
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
            _flush_backend()
            kv_result, kv_ms = _timed_run([KV, rel], env=_KV_ENV)
            py_result, py_ms = _timed_run([sys.executable, str(f.with_suffix(".py"))])
            c_result, c_ms = _timed_run([str(executable)])
        except FileNotFoundError as exc:
            return invalid, f"command not found: {exc.filename}"
        except subprocess.TimeoutExpired:
            return invalid, "program timed out"

    error = _benchmark_error(
        {"pathlang": kv_result, "python": py_result, "c": c_result}, expects,
    )
    if error:
        return invalid, error
    return {
        "file": rel,
        "pathlang_ms": f"{kv_ms:.3f}",
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

def _run_test_file(f: Path, expects: list[str], env: dict) -> tuple[bool, str]:
    rel = str(f.relative_to(ROOT))
    dsn = _case_dsn()
    if dsn == _C_DSN:
        _flush_backend()
    boot = subprocess.run([TERM_BIN, "-c", 'print("boot")'], capture_output=True,
                          text=True, timeout=60, cwd=str(ROOT),
                          env={**env, "PATHSPACE": dsn})
    if boot.returncode != 0:
        return False, f"isa boot failed: {boot.stderr.strip()[:100]}"
    layout = subprocess.run([LAYOUT_BIN, rel, dsn], capture_output=True, text=True,
                            timeout=60, cwd=str(ROOT), env=env)
    if layout.returncode != 0:
        return False, f"layout failed: {layout.stderr.strip()[:100]}"
    entry = "test"
    try:
        crun = subprocess.run([TERM_BIN, entry], capture_output=True, text=True,
                              timeout=CASE_TIMEOUT, cwd=str(ROOT), env={**env, "PATHSPACE": dsn})
    except subprocess.TimeoutExpired:
        return False, "timeout"
    if crun.returncode != 0:
        return False, f"term exit {crun.returncode}: {crun.stderr.strip()[:100]}"
    ok, detail = _compare_lines(expects, crun.stdout)
    if not ok:
        return False, detail
    return True, crun.stdout[:200]

def _normalize_output_lines(text: str) -> list[str]:
    if not text:
        return []
    return [line.rstrip() for line in text.splitlines()]

def _compare_lines(expects: list[str], actual_stdout: str) -> tuple[bool, str]:
    expected_lines = [line.rstrip() for line in expects]
    actual_lines = _normalize_output_lines(actual_stdout)
    if len(expected_lines) != len(actual_lines):
        return False, (f"line count {len(actual_lines)} != expected "
                       f"{len(expected_lines)}")
    for ordinal, (want, got) in enumerate(zip(expected_lines, actual_lines), 1):
        if want != got:
            return False, f"line {ordinal}: want {want!r} got {got!r}"
    return True, ""

def main():
    global _C_DSN, _BACKEND_TMP, LAYOUT_BIN, TERM_BIN, CASE_TIMEOUT
    ap = argparse.ArgumentParser(description="tutorial test")
    ap.add_argument("--filter", default="", help="按路径子串过滤（如 11-string/01 / 08-leetcode/01）")
    ap.add_argument("--no-build", action="store_true", help="skip make build")
    ap.add_argument("--errorexit", action="store_true", help="exit on first error")
    ap.add_argument("--bench", action="store_true", help="benchmark matching .kv/.py/.c files")
    ap.add_argument("--pathspace", default="",
                    help="DSN，必填且只接受 shm://（如 shm:///tmp/kv.shm）；无默认值")
    ap.add_argument("--layout-bin", default=LAYOUT_BIN, help="pathlanglayout 路径")
    ap.add_argument("--term-bin", default=TERM_BIN, help="pathseek 路径")
    ap.add_argument("--case-timeout", type=int, default=CASE_TIMEOUT,
                    help=f"单用例运行超时（秒），默认 {CASE_TIMEOUT}")
    args = ap.parse_args()
    LAYOUT_BIN, TERM_BIN = args.layout_bin, args.term_bin
    CASE_TIMEOUT = args.case_timeout
    _C_DSN = args.pathspace
    if not _C_DSN:
        ap.error("缺少 --pathspace=：DSN 必填且只接受 shm://，例如 "
                 "--pathspace=shm:///tmp/kv.shm（无默认值）")
    if not _C_DSN.startswith("shm://"):
        ap.error(f"--pathspace 只接受 shm://，收到 {_C_DSN!r}")
    _BACKEND_TMP = tempfile.TemporaryDirectory(prefix="pathlang-tut-")

    files = [f for f in discover(ROOT / "tutorial")
             if args.filter in str(f)]

    print(f"pathlang: {os.path.abspath(KV)}")
    print(f"PATHSPACE: {_C_DSN}")
    prefix = "pathlang"

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
            ok, detail = _run_test_file(f, expects, _KV_ENV)
            if ok:
                print(f"{GREEN}✅ {prefix} {rel}{NC}")
                passed += 1
            else:
                print(f"{RED}❌ {prefix} {rel}: {detail}{NC}")
                failures.append({"file": rel, "reason": detail, "expected": "", "stdout": ""})
                failed += 1
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
            fieldnames=["file", "pathlang_ms", "python_ms", "c_ms"],
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
        self.pathlang = self.root / "pathlang"
        self.pathlang.write_text("#!/bin/sh\nprintf 'answer 42\\n'\n", encoding="utf-8")
        self.pathlang.chmod(0o755)
        self.patchers = [
            mock.patch.object(MODULE, "ROOT", self.root),
            mock.patch.object(MODULE, "KV", str(self.pathlang)),
            mock.patch.object(MODULE, "FAIL_CSV", self.tutorial / "test_failures.csv"),
            mock.patch.object(MODULE, "BENCH_CSV", self.tutorial / "benchmark.csv"),
            mock.patch.object(MODULE, "_flush_backend"),
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
        source.write_text("// 期望输出:\n//   answer 42\n", encoding="utf-8")
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
            list(rows[0]), ["file", "pathlang_ms", "python_ms", "c_ms"],
        )
        self.assertEqual(rows[0]["file"], "tutorial/case.kv")
        self.assertNotIn(b"\r\n", BENCH_CSV.read_bytes())
        for field in ("pathlang_ms", "python_ms", "c_ms"):
            self.assertGreaterEqual(float(rows[0][field]), 0)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is required")
    def test_marks_output_mismatch_invalid(self):
        source = self.write_fixture(python_output="wrong")

        self.assertEqual(run_benchmarks([source]), 1)

        row = self.read_rows()[0]
        self.assertEqual(
            [row["pathlang_ms"], row["python_ms"], row["c_ms"]],
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
        self.assertEqual(self.read_rows()[0]["pathlang_ms"], "invalid")

    def test_marks_nonzero_exit_invalid(self):
        failed = subprocess.CompletedProcess(
            [], 1, stdout="answer 42\n", stderr="warning\n",
        )

        self.assertIn(
            "exited with status 1",
            _benchmark_error(
                {"pathlang": failed, "python": failed, "c": failed}, ["answer 42"],
            ),
        )

    def test_stderr_does_not_invalidate_successful_run(self):
        result = subprocess.CompletedProcess(
            [], 0, stdout="answer 42\n", stderr="warning\n",
        )

        self.assertEqual(
            _benchmark_error(
                {"pathlang": result, "python": result, "c": result}, ["answer 42"],
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
