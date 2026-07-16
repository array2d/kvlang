#!/usr/bin/env python3
"""kvlang tutorial test — 从 .kv 文件 # 期望输出 头注释自动生成测试。"""

from __future__ import annotations
import argparse, re, subprocess, sys
from pathlib import Path

RED, GREEN, YELLOW, NC = "\033[0;31m", "\033[0;32m", "\033[1;33m", "\033[0m"
ROOT = Path(__file__).resolve().parent.parent
KV = str(ROOT / "kvlang")


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
                    # 去掉行尾注释 "(i=3)" 之类
                    p = re.sub(r"\s*\(.*\)\s*$", "", p)
                    if p:
                        pats.append(p)
                elif not line.startswith("#"):
                    break  # 空行或非注释行结束块
    return pats


def main():
    ap = argparse.ArgumentParser(description="tutorial test")
    ap.add_argument("--filter", default="", help="filter by name")
    args = ap.parse_args()

    passed = failed = 0
    files = [f for f in discover(ROOT / "tutorial")
             if args.filter in str(f)]

    if not files:
        print(f"{YELLOW}no .kv files found{NC}")
        sys.exit(0)

    for f in files:
        expects = parse_expects(f)
        if not expects:
            continue
        rel = str(f.relative_to(ROOT))
        try:
            subprocess.run([KV, "kvspace", "clear"], capture_output=True, timeout=5)
            r = subprocess.run([KV, rel], capture_output=True, text=True,
                               timeout=60, cwd=str(ROOT))
            all_ok = True
            for pat in expects:
                if pat not in r.stdout:
                    all_ok = False
                    print(f"{RED}❌ {rel}: want {pat!r}{NC}")
                    print(f"   stdout: {r.stdout[:200]}")
            if all_ok:
                print(f"{GREEN}✅ {rel}{NC}")
                passed += 1
            else:
                failed += 1
        except subprocess.TimeoutExpired:
            print(f"{RED}❌ {rel}: timeout{NC}")
            failed += 1

    print(f"\n{YELLOW}══ {GREEN}PASS:{passed}{YELLOW}  {RED}FAIL:{failed}{YELLOW} ══{NC}")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
