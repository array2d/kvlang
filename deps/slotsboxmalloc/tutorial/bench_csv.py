import argparse
import csv
import platform
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "tutorial"

BENCHES = [
    "08_bench_pure",
    "09_bench_sharded",
    "11_bench_shallow",
    "12_bench_lock",
    "14_bench_alloc_only",
    "15_bench_dedicated",
    "16_bench_malloc",
]

ROW = re.compile(
    r"^(?P<case>.*?)\s*(?:thr|threads)=(?P<thr>\d+)\s+"
    r"ops=(?P<ops>\d+)\s+time=(?P<time>[\d.]+)s\s+"
    r"(?:rate|throughput|total_throughput)=\s*(?P<rate>[\d,]+)\s*ops/s"
)

FIELDS = ["bench", "case", "threads", "ops", "time_s", "mops", "env"]

def main() -> int:
    ap = argparse.ArgumentParser(description="tutorial bench → CSV")
    ap.add_argument("-o", "--out", default=str(ROOT / "doc" / "bench-v3.csv"))
    args = ap.parse_args()

    env = f"{platform.system()} {platform.machine()}"
    rows = []
    for name in BENCHES:
        exe = BUILD / name
        if not exe.exists():
            print(f"skip (not built): {name}", file=__import__("sys").stderr)
            continue
        proc = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
        for line in (proc.stdout + proc.stderr).splitlines():
            m = ROW.match(line.strip())
            if not m:
                continue
            case = (m.group("case") or name).strip().replace(" ", "_") or name
            rows.append({
                "bench": name,
                "case": case,
                "threads": int(m.group("thr")),
                "ops": int(m.group("ops")),
                "time_s": f"{float(m.group('time')):.6f}",
                "mops": f"{int(m.group('rate').replace(',', '')) / 1e6:.3f}",
                "env": env,
            })

    with open(args.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    print(f"{len(rows)} rows -> {args.out}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
