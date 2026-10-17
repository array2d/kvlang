#!/usr/bin/env python3
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PC_RE = re.compile(r"^/vthread/[0-9]+/\[[0-9]+\]/\[[0-9]+,0\]$")
PC_LINE = re.compile(r"PC=(\S+)")
SCOPE_BIT = re.compile(r"_else_|_if_|_while_|_then_|_merge_|_do_|_exit_")
NESTED_INST = re.compile(r"/\[\d+,0\]/\[")


def run(cmd: list[str], env: dict | None = None, timeout: int = 60) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=str(ROOT), env=env)


def main() -> int:
    env = {**os.environ, "LOG_LEVEL": "debug"}
    kv = env.get("KVLANG_TERM_BIN", str(ROOT / "bin" / "kvlang"))
    layout = env.get("KVLANG_LAYOUT_BIN", str(ROOT / "bin" / "layout_file"))
    src = ROOT / "tutorial" / "02-func" / "tco_depth.kv"
    dsn = env.get("KVSPACE", "shm:///tmp/kvlang_frame_pc")
    if dsn.startswith("shm://"):
        p = Path(dsn[len("shm://") :])
        try:
            p.unlink()
        except OSError:
            pass

    lr = run([layout, str(src), dsn], env)
    if lr.returncode != 0:
        print(f"FAIL layout: {lr.stderr[:400]}", file=sys.stderr)
        return 1

    rr = run([kv, "init"], env={**env, "KVSPACE": dsn}, timeout=120)
    text = rr.stdout + "\n" + rr.stderr
    pcs = PC_LINE.findall(text)
    if not pcs:
        print("FAIL no PC= lines (need LOG_LEVEL=debug and a working run)", file=sys.stderr)
        print(text[-800:], file=sys.stderr)
        return 1

    bad = [p for p in pcs if not PC_RE.match(p)]
    if bad:
        print(f"FAIL E1 grammar: {bad[:5]}", file=sys.stderr)
        return 1
    lens = [len(p) for p in pcs]
    if max(lens) - min(lens) >= 8:
        print(f"FAIL E2 O(1) length: min={min(lens)} max={max(lens)}", file=sys.stderr)
        return 1
    depths = sorted({int(re.search(r"/\[(\d+)\]/", p).group(1)) for p in pcs})
    if depths != list(range(1, depths[-1] + 1)):
        print(f"FAIL E3 depths not 1..=max: {depths}", file=sys.stderr)
        return 1
    if any(SCOPE_BIT.search(p) for p in pcs):
        print("FAIL E4 scope name in PC", file=sys.stderr)
        return 1
    if any(NESTED_INST.search(p) for p in pcs):
        print("FAIL E7 nested instruction-as-dir", file=sys.stderr)
        return 1
    print(f"ok n={len(pcs)} max_d={depths[-1]} len={min(lens)}..{max(lens)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
