#!/usr/bin/env python3
"""kvlang tutorial test — 从 .kv 文件 # 期望输出 头注释自动生成测试。"""

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

RED, GREEN, YELLOW, NC = "\033[0;31m", "\033[0;32m", "\033[1;33m", "\033[0m"
ROOT = Path(__file__).resolve().parent.parent
KV = str(ROOT / "kvlang")
FAIL_CSV = (ROOT / "tutorial" / "test_failures.csv").resolve()
BENCH_CSV = (ROOT / "tutorial" / "benchmark.csv").resolve()


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


def _flush_redis() -> None:
    try:
        subprocess.run(["redis-cli", "-p", "6379", "FLUSHALL"],
                       capture_output=True, timeout=5)
    except FileNotFoundError:
        pass


def _timed_run(command: list[str]) -> tuple[subprocess.CompletedProcess[str], float]:
    started = time.perf_counter()
    result = subprocess.run(command, capture_output=True, text=True,
                            timeout=60, cwd=str(ROOT))
    return result, (time.perf_counter() - started) * 1000


def _benchmark_error(results: dict[str, subprocess.CompletedProcess[str]],
                     expects: list[str]) -> str:
    outputs = {name: result.stdout for name, result in results.items()}
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
                ["gcc", "-O3", str(f.with_suffix(".c")), "-o", str(executable)],
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
            kv_result, kv_ms = _timed_run([KV, rel])
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


def main():
    ap = argparse.ArgumentParser(description="tutorial test")
    ap.add_argument("--filter", default="", help="filter by name")
    ap.add_argument("--no-build", action="store_true", help="skip make build")
    ap.add_argument("--errorexit", action="store_true", help="exit on first error")
    ap.add_argument("--bench", action="store_true", help="benchmark matching .kv/.py/.c files")
    args = ap.parse_args()

    if not args.no_build:
        r = subprocess.run(["make", "build"], capture_output=True, text=True,
                           timeout=120, cwd=str(ROOT))
        if r.returncode != 0:
            print(f"{RED}❌ make build failed:{NC}\n{r.stderr}")
            sys.exit(1)
        print(f"{GREEN}✅ build ok{NC}")

    files = [f for f in discover(ROOT / "tutorial")
             if args.filter in str(f)]

    print(f"kvlang: {os.path.abspath(KV)}")

    if args.bench:
        sys.exit(1 if run_benchmarks(files, args.errorexit) else 0)

    if not files:
        print(f"{YELLOW}no .kv files found{NC}")
        sys.exit(0)

    passed = failed = 0
    failures: list[dict] = []
    for f in files:
        expects = parse_expects(f)
        if not expects:
            continue
        rel = str(f.relative_to(ROOT))
        try:
            _flush_redis()
            r = subprocess.run([KV, rel], capture_output=True, text=True,
                               timeout=60, cwd=str(ROOT))
            all_ok = True
            if r.returncode != 0:
                all_ok = False
                print(f"{RED}❌ kvlang {rel}: exit code {r.returncode}{NC}")
                failures.append({"file": rel, "reason": f"exit code {r.returncode}",
                                 "expected": "", "stdout": r.stdout[:500]})
            if r.stderr.strip():
                all_ok = False
                print(f"{RED}❌ kvlang {rel}: stderr — {r.stderr.strip()[:200]}{NC}")
                found = [x for x in failures if x["file"] == rel and x["reason"].startswith("exit code")]
                if not found:
                    failures.append({"file": rel, "reason": f"stderr: {r.stderr.strip()[:200]}",
                                     "expected": "", "stdout": r.stdout[:500]})
            for pat in expects:
                if pat not in r.stdout:
                    all_ok = False
                    print(f"{RED}❌ kvlang {rel}: want {pat!r}{NC}")
                    print(f"   stdout: {r.stdout[:200]}")
                    failures.append({"file": rel, "reason": "output mismatch",
                                     "expected": pat, "stdout": r.stdout[:500]})
            if all_ok:
                print(f"{GREEN}✅ kvlang {rel}{NC}")
                passed += 1
            else:
                failed += 1
                if args.errorexit:
                    _write_csv(failures)
                    print(f"\n{YELLOW}errorexit: stopping at first failure{NC}")
                    sys.exit(1)
        except subprocess.TimeoutExpired:
            print(f"{RED}❌ kvlang {rel}: timeout{NC}")
            failed += 1
            failures.append({"file": rel, "reason": "timeout", "expected": "", "stdout": ""})
            if args.errorexit:
                _write_csv(failures)
                print(f"\n{YELLOW}errorexit: stopping at first failure{NC}")
                sys.exit(1)

    _write_csv(failures)
    print(f"{YELLOW}══ {GREEN}PASS:{passed}{YELLOW}  {RED}FAIL:{failed}{YELLOW} ══{NC}")
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
        w = csv.DictWriter(fh, fieldnames=["file", "kvlang_ms", "python_ms", "c_ms"])
        w.writeheader()
        w.writerows(rows)


if __name__ == "__main__":
    main()
