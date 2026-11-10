#!/usr/bin/env python3
"""检查 symbol.Table 之外的算子 hardcode。语法 token（arrow、成员访问）除外。"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.abspath(__file__))

SYMBOLS = {s.strip("\"'") for s in '''
"+" "-" "*" "×" "/" "÷" "%" "="
"==" "!=" "<" ">" "<=" ">=" "≠" "≤" "≥"
"&&" "||" "!"
"√" "⊗"
"&" "|" "^" "<<" ">>"
'''.split()}

ALLOW = ["symbol/symbol.go", "symbol/check_hardcoded.py"]

# ── Go patterns ────────────────────────────────────────────
GO_SYNTAX_PATTERNS = [
    r'arrowVal\s*==', r'Arrow.*"="', r'Arrow.*"<-', r'Arrow.*"->"',
    r'Token\{Kind: Arrow', r'\.Value\s*==\s*"\*"',
    r'dict literal.*"="', r'\.Value\s*==\s*"="',
]

# ── Rust patterns ──────────────────────────────────────────
RUST_SYNTAX_PATTERNS = [
    # match arms: "add" | "+" => ...,  "print" | "println" => { ... }
    # Allow any symbol used in a match arm
    r'match\b',  # entire match block is allowed
]

def has_go_violation(line, sym):
    q = re.escape(sym)
    if re.search(r'"' + q + r'"\s*[:=]\s*(\d+|true|false)', line):
        return "map attr"
    if re.search(r'==\s*"' + q + r'"', line):
        if any(re.search(p, line) for p in GO_SYNTAX_PATTERNS):
            return ""
        return "compare"
    if re.search(r'case\s+.*"' + q + r'"', line):
        return "switch case"
    return ""

def has_rs_violation(line, sym):
    q = re.escape(sym)
    # match arm: '"+" | "-" =>' or '"add" => {'
    if re.search(r'"' + q + r'"\s*(?:\|[^"]*"[^"]*")?\s*=>', line):
        return ""
    # string compare: opcode == "!"
    if re.search(r'==\s*"' + q + r'"', line):
        return "compare"
    return ""

def check_file(fpath, violations, lang):
    rel = os.path.relpath(fpath, ROOT)
    if any(rel == a for a in ALLOW):
        return
    with open(fpath) as f:
        for lineno, line in enumerate(f, 1):
            s = line.strip()
            if s.startswith('//') or s.startswith('*') or s.startswith('import') or s.startswith('use '):
                continue
            if s.startswith('#'):  # Rust attributes (#[derive], #![...])
                continue
            if s.startswith("//!") or s.startswith("///"):  # Rust doc comments
                continue
            for sym in SYMBOLS:
                has_fn = has_rs_violation if lang == 'rs' else has_go_violation
                t = has_fn(line, sym)
                if t:
                    violations.append(f"{rel}:{lineno}: {sym!r} {t}")
                    break

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", default=".go", choices=(".go", ".rs"),
                    help="language to check (default: .go)")
    args = ap.parse_args()
    ext = args.lang

    violations = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'vendor', 'deepx-design', 'target')]
        for fn in filenames:
            if not fn.endswith(ext) or fn.endswith('_test' + ext):
                continue
            check_file(os.path.join(dirpath, fn), violations, ext.lstrip('.'))

    if violations:
        print(f"{len(violations)} hardcoded:")
        for v in violations: print(f"  {v}")
        sys.exit(1)
    print("0 hardcoded — clean.")
    sys.exit(0)

if __name__ == '__main__': main()
