#!/usr/bin/env python3
"""error_test — 错误用例验证：每个 .kv 文件头注释标注期望的诊断信息，
脚本运行/检查该文件，收集实际错误输出并与期望逐条比对。

用法: python3 tutorial/error_test.py
"""
from __future__ import annotations
import os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LAYOUT_BIN = os.environ.get("KVLANG_LAYOUT_BIN", str(ROOT / "bin" / "layout_file"))
TERM_BIN = os.environ.get("KVLANG_TERM_BIN", str(ROOT / "bin" / "kvlang"))
_C_DSN = os.environ.get("KVSPACE", "redis://127.0.0.1:6379")
ERROR_CASES = ROOT / "tutorial" / "error_cases"
RED, GREEN, NC = "\033[0;31m", "\033[0;32m", "\033[0m"


def discover(root: Path) -> list[Path]:
    return sorted(root.rglob("*.kv"))


def parse_expects(f: Path) -> list[str]:
    """从 .kv 文件头提取 # 期望诊断 行，每行为一个子串（须匹配 stderr 中的同一行）。"""
    pats = []
    in_block = False
    with open(f) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if line.startswith("# expected"):
                in_block = True
                continue
            if in_block:
                if line.startswith("#   ") or line.startswith("# \t"):
                    p = line[2:].strip()
                    if p:
                        pats.append(p)
                else:
                    break
    return pats


def _unescape_logx(stderr: str) -> str:
    """logx msg= 域为 JSON-style 引号串，\" → " 以利子串匹配。"""
    return stderr.replace('\\"', '"')

def _detect_entry(out: str) -> str:
    m = re.search(r"ENTRY=(\S+)", out)
    return m.group(1) if m else "init"


def collect_errors(rel: str) -> str:
    """layout（编译期诊断）+ run（运行时诊断），收集 stderr 合并。"""
    # 用 kvspace clear 隔离各 case
    subprocess.run(["kvspace", "clear"], capture_output=True, timeout=10)
    stderr = ""
    layout = subprocess.run([LAYOUT_BIN, rel, _C_DSN], capture_output=True,
                            text=True, timeout=60, cwd=str(ROOT))
    stderr += _unescape_logx(layout.stderr)
    if layout.returncode == 0:
        entry = _detect_entry(layout.stdout)
        crun = subprocess.run([TERM_BIN, entry], capture_output=True,
                              text=True, timeout=120, cwd=str(ROOT),
                              env={**os.environ, "KVSPACE": _C_DSN})
        stderr += "\n" + _unescape_logx(crun.stderr)
    return stderr


def check(pat: str, errors: str) -> bool:
    """子串匹配：期望诊断串出现在 stderr 同一行的任意位置。"""
    for ln in errors.splitlines():
        if pat in ln:
            return True
    return False


def main() -> int:
    files = [f for f in discover(ERROR_CASES) if f.suffix == ".kv"]
    if not files:
        print(f"{RED}未找到错误用例文件{RED}")
        return 1

    passed, total = 0, 0
    for f in files:
        rel = str(f.relative_to(ROOT))
        expects = parse_expects(f)
        if not expects:
            print(f"{RED}❌ {rel}: 缺少 # 期望诊断 注释{RED}")
            continue

        try:
            errors = collect_errors(rel)
        except subprocess.TimeoutExpired:
            print(f"{RED}❌ {rel}: 运行超时{RED}")
            continue
        except Exception as e:  # noqa: BLE001
            print(f"{RED}❌ {rel}: 运行异常 {e}{RED}")
            continue

        all_ok = True
        for pat in expects:
            total += 1
            if check(pat, errors):
                passed += 1
            else:
                all_ok = False
                print(f"{RED}❌ {rel}: 未匹配 {pat!r}{RED}")
                # 输出实际 stderr 供诊断
                for ln in errors.splitlines()[-10:]:
                    print(f"    实际: {ln}")

        if all_ok:
            print(f"{GREEN}✅ {rel}: ({passed}/{total}){NC}")

    print(f"\n{GREEN}══ 错误诊断用例: {passed}/{total} PASSED, {total-passed} FAILED ══{NC}")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
