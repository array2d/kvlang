#!/usr/bin/env python3
"""C runtime 验证：Rust layout → redis → C runtime，对比 .kv 头注释期望输出。

用法：python3 tutorial/test_c_runtime.py [filter]
前置：redis 运行中；layout 示例 + C runtime 已构建。
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYOUT = str(ROOT / "layout" / "target" / "debug" / "examples" / "layout_file")
CRUN = str(ROOT / "runtime" / "test" / "run")


def parse_expects(f: Path) -> list[str]:
    pats = []
    in_block = False
    for line in f.read_text().splitlines():
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


def needs_skip(f: Path) -> bool:
    for line in f.read_text().splitlines():
        if line.startswith("# extern") or line.startswith("# wip"):
            return True
        if line and not line.startswith("#"):
            return False
    return False


def detect_entry(out: str) -> str:
    m = re.search(r"ENTRY=(\S+)", out)
    return m.group(1) if m else "init"


def flush():
    subprocess.run(["redis-cli", "-p", "6379", "FLUSHALL"], capture_output=True, timeout=5)


def layout(f: Path):
    return subprocess.run([LAYOUT, str(f)], capture_output=True, text=True, timeout=60)


def crun(entry: str):
    cmd = [CRUN, entry]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30)


def main() -> int:
    filt = sys.argv[1] if len(sys.argv) > 1 else ""
    files = sorted(ROOT.glob("tutorial/**/*.kv"))
    passed = failed = skipped = 0
    fails = []
    for f in files:
        if filt and filt not in str(f):
            continue
        if needs_skip(f):
            skipped += 1
            continue
        expects = parse_expects(f)
        if not expects:
            continue
        print(f"  {f.name}", file=sys.stderr, flush=True)

        flush()
        lr = layout(f)
        if lr.returncode != 0:
            failed += 1
            fails.append((f.name, f"[layout] {lr.stderr.strip()[-160:]}"))
            continue
        entry = detect_entry(lr.stdout)
        try:
            cr = crun(entry)
        except subprocess.TimeoutExpired:
            failed += 1
            fails.append((f.name, "[timeout]"))
            continue
        if cr.returncode != 0:
            failed += 1
            fails.append((f.name, f"[run rc={cr.returncode}] {cr.stderr.strip()[-160:]}"))
            continue
        ok = all(e in cr.stdout for e in expects)
        if ok:
            passed += 1
        else:
            failed += 1
            fails.append((f.name, f"[entry={entry}] expect={expects} got={cr.stdout.strip()[-160:]!r}"))

    print(f"\nPASS {passed}  FAIL {failed}  SKIP {skipped}")
    for name, why in fails:
        print(f"  FAIL {name}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
