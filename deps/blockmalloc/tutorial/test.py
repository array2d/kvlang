
from __future__ import annotations
import argparse, csv, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

RED, GREEN, YELLOW, NC = "\033[0;31m", "\033[0;32m", "\033[1;33m", "\033[0m"
ROOT = Path(__file__).resolve().parent.parent
FAIL_CSV = (ROOT / "tutorial" / "test_failures.csv").resolve()
TIMEOUT = 30

def discover(build_dir: Path) -> list[Path]:
    cases = sorted(build_dir.rglob("[0-9][0-9]_*"))
    return [c for c in cases if c.is_file() and os.access(c, os.X_OK)]

def build() -> bool:
    out = ROOT / "build"
    out.mkdir(exist_ok=True)
    r = subprocess.run(
        ["cmake", "-DCMAKE_BUILD_TYPE=Release", ".."],
        capture_output=True, text=True, timeout=60, cwd=str(out))
    if r.returncode != 0:
        print(f"{RED}❌ cmake failed:{NC}\n{r.stderr}")
        return False
    r = subprocess.run(
        ["make", f"-j{os.cpu_count() or 1}"],
        capture_output=True, text=True, timeout=120, cwd=str(out))
    if r.returncode != 0:
        print(f"{RED}❌ make failed:{NC}\n{r.stderr}")
        return False
    return True

def _run_one(case: Path, label: str, timeout: int) -> dict:
    try:
        t0 = time.monotonic()
        r = subprocess.run([str(case)], capture_output=True, text=True, timeout=timeout)
        elapsed = time.monotonic() - t0
        return {
            "label": label, "exit": r.returncode, "time": elapsed,
            "stdout": r.stdout.strip()[:500], "stderr": r.stderr.strip()[:300],
        }
    except subprocess.TimeoutExpired:
        return {
            "label": label, "exit": "timeout", "time": timeout,
            "stdout": "", "stderr": "",
        }

def main():
    ap = argparse.ArgumentParser(description="blockmalloc tutorial test")
    ap.add_argument("--filter", default="", help="filter by case name")
    ap.add_argument("--no-build", action="store_true", help="skip cmake + make")
    ap.add_argument("--errorexit", action="store_true", help="exit on first error")
    ap.add_argument("--repeat", type=int, default=1, help="repeat each case N times")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                    help=f"parallel jobs (default: cpu count)")
    args = ap.parse_args()

    build_dir = ROOT / "build" / "tutorial"

    if not args.no_build:
        if not build():
            sys.exit(1)
        print(f"{GREEN}✅ build ok{NC}")

    cases = [c for c in discover(build_dir) if args.filter in c.name]
    if not cases:
        print(f"{YELLOW}no case binaries found in {build_dir}{NC}")
        sys.exit(0)

    tasks = []
    for case in cases:
        for run_i in range(args.repeat):
            label = f"{case.name} [{run_i+1}/{args.repeat}]" if args.repeat > 1 else case.name
            tasks.append((case, label))

    passed, failed = 0, 0
    failures: list[dict] = []
    print_lock = __import__('threading').Lock() if args.jobs > 1 else None

    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futures = {ex.submit(_run_one, c, label, TIMEOUT): (c.name, label) for c, label in tasks}

        for fut in as_completed(futures):
            r = fut.result()
            label = r["label"]
            if args.jobs > 1:
                with print_lock:
                    _report(r, label)
            else:
                _report(r, label)

            if r["exit"] != 0:
                failed += 1
                failures.append({
                    "case": futures[fut][0], "run": label,
                    "exit": r["exit"], "time": f"{r['time']:.3f}",
                    "stdout": r["stdout"],
                })
                if args.errorexit:
                    _write_csv(failures)
                    sys.exit(1)
            else:
                passed += 1

    _write_csv(failures)
    total = passed + failed
    print(f"{YELLOW}══ {GREEN}PASS:{passed}{YELLOW}  {RED}FAIL:{failed}{YELLOW}  "
          f"TOTAL:{total} ══{NC}")
    print(f"report: {FAIL_CSV}")
    sys.exit(0 if failed == 0 else 1)

def _report(r: dict, label: str) -> None:
    if r["exit"] == "timeout":
        print(f"{RED}❌ {label}: timeout >{r['time']}s{NC}")
    elif r["exit"] != 0:
        print(f"{RED}❌ {label}: exit={r['exit']} ⏱ {r['time']:.3f}s{NC}")
        if r["stdout"]:
            print(f"   {r['stdout'][:200]}")
        if r["stderr"]:
            print(f"   stderr: {r['stderr'][:200]}")
    else:
        print(f"{GREEN}✅ {label}: exit=0 ⏱ {r['time']:.3f}s{NC}")
        if r["stdout"]:
            print(f"   {r['stdout'][:200]}")

def _write_csv(failures: list[dict]) -> None:
    FAIL_CSV.write_text("case,run,exit,time,stdout\n", encoding="utf-8")
    if not failures:
        return
    with open(FAIL_CSV, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["case", "run", "exit", "time", "stdout"])
        w.writeheader()
        for row in failures:
            w.writerow(row)

if __name__ == "__main__":
    main()
