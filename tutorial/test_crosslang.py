#!/usr/bin/env python3
"""跨语言验证：Rust layout → redis → Go runtime，对比 .kv 头注释期望输出。

用法：python3 tutorial/test_crosslang.py [filter]
前置：redis 运行中；layout 示例已构建（kvlang/layout/target/debug/examples/layout_file）。
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KV = str(ROOT / "kvlang")
RUST_LAYOUT = str(ROOT / "layout" / "target" / "debug" / "examples" / "layout_file")


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


def detect_entry(layout_stdout: str) -> str:
    # layout_file 输出 ENTRY=...（复刻 Go findEntry）
    m = re.search(r"ENTRY=(\S+)", layout_stdout)
    return m.group(1) if m else "init"


def flush() -> None:
    subprocess.run(["redis-cli", "-p", "6379", "FLUSHALL"], capture_output=True, timeout=5)


def rust_layout(f: Path):
    return subprocess.run([RUST_LAYOUT, str(f)], capture_output=True, text=True, timeout=60)


def go_run(entry: str):
    # 裸 "init" 入口：run 无参数 → runLib("","init")；lib 块入口：run X.init
    cmd = f"{KV} run" if entry == "init" else f"{KV} run {entry}"
    return subprocess.run(
        ["script", "-qec", cmd, "/dev/null"], capture_output=True, text=True, timeout=20
    )


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
        lr = rust_layout(f)
        if lr.returncode != 0:
            failed += 1
            fails.append((f.name, f"[layout] {lr.stderr.strip()[-160:]}"))
            continue
        entry = detect_entry(lr.stdout)
        try:
            gr = go_run(entry)
        except subprocess.TimeoutExpired:
            failed += 1
            fails.append((f.name, "[timeout]"))
            continue
        if gr.returncode != 0 and not gr.stdout:
            failed += 1
            fails.append((f.name, f"[run rc={gr.returncode}] {gr.stderr.strip()[-160:]}"))
            continue
        ok = all(e in gr.stdout for e in expects)
        if ok:
            passed += 1
        else:
            failed += 1
            fails.append((f.name, f"[entry={entry}] expect={expects} got={gr.stdout.strip()[-160:]!r}"))

    print(f"\nPASS {passed}  FAIL {failed}  SKIP {skipped}")
    for name, why in fails:
        print(f"  FAIL {name}: {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
