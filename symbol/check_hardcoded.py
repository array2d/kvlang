#!/usr/bin/env python3
"""检查 symbol.Table 之外的算子 hardcode。语法 token（arrow、成员访问）除外。"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SYMBOLS = {s.strip("\"'") for s in '''
"+" "-" "*" "×" "/" "÷" "%" "="
"==" "!=" "<" ">" "<=" ">=" "≠" "≤" "≥"
"&&" "||" "!"
"√" "⊗"
"&" "|" "^" "<<" ">>"
'''.split()}

ALLOW = ["symbol/symbol.go", "symbol/check_hardcoded.py"]

# 语法 token 上下文 — 非算子，不检查
SYNTAX_PATTERNS = [
    r'arrowVal\s*==',           # arrow 语法
    r'Arrow.*"=', r'Arrow.*"<-', r'Arrow.*"->',
    r'Token\{Kind: Arrow',       # scanner 输出 arrow token
    r'\.Value\s*==\s*"\*"',     # 成员访问 base.*key
    r'dict literal.*"="',        # dict 字面量 {k=v}
    r'\.Value\s*==\s*"="',      # dict 中的 =
]

def has_violation(line, sym):
    q = re.escape(sym)
    # 1) map 中 hardcode 属性: "*": 60, "==": true
    if re.search(r'"' + q + r'"\s*[:=]\s*(\d+|true|false)', line):
        return "map attr"
    # 2) 字符串比较: opcode == "!"
    if re.search(r'==\s*"' + q + r'"', line):
        if any(re.search(p, line) for p in SYNTAX_PATTERNS):
            return ""
        return "compare"
    # 3) switch case: case "!", "-", ...
    if re.search(r'case\s+.*"' + q + r'"', line):
        return "switch case"
    return ""

def main():
    violations = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'vendor', 'deepx-design')]
        for fn in filenames:
            if not fn.endswith('.go') or fn.endswith('_test.go'):
                continue
            fpath = os.path.join(dirpath, fn)
            rel = os.path.relpath(fpath, ROOT)
            if any(rel == a for a in ALLOW):
                continue
            with open(fpath) as f:
                for lineno, line in enumerate(f, 1):
                    s = line.strip()
                    if s.startswith('//') or s.startswith('*') or s.startswith('import'):
                        continue
                    for sym in SYMBOLS:
                        t = has_violation(line, sym)
                        if t:
                            violations.append(f"{rel}:{lineno}: {sym!r} {t}")
                            break

    if violations:
        print(f"{len(violations)} hardcoded:")
        for v in violations: print(f"  {v}")
        sys.exit(1)
    print("0 hardcoded — clean.")
    sys.exit(0)

if __name__ == '__main__': main()
