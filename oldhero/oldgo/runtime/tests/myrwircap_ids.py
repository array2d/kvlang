#!/usr/bin/env python3
"""能力表编号契约：dump / compare / selftest。

以 runtime/src/myrwircaps.c 预处理后的表序为准（id = 表下标）。
fixture 见 tests/fixtures/myrwircap_ids.txt，逐字节比对；错位时打印
第一个错位的 index 与两边的名字。
"""

import argparse
import codecs
import contextlib
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(ROOT, "runtime", "src", "myrwircaps.c")
FIXTURE = os.path.join(HERE, "fixtures", "myrwircap_ids.txt")
NONE_KINDS = {"((void *)0)", "NULL", "0", ""}


def split_fields(entry):
    fields, cur, quoted, i = [], [], False, 0
    while i < len(entry):
        c = entry[i]
        if quoted:
            cur.append(c)
            if c == "\\" and i + 1 < len(entry):
                cur.append(entry[i + 1])
                i += 2
                continue
            if c == '"':
                quoted = False
            i += 1
            continue
        if c == '"':
            quoted = True
            cur.append(c)
        elif c == ",":
            fields.append("".join(cur))
            cur = []
        else:
            cur.append(c)
        i += 1
    fields.append("".join(cur))
    return [f.strip() for f in fields]


def decode_literal(field):
    raw = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', field))
    return codecs.decode(raw, "unicode_escape").encode("latin-1").decode("utf-8")


def preprocess(path=SRC):
    proc = subprocess.run(
        ["gcc", "-E", "-P", f"-I{ROOT}/runtime/include",
         f"-I{ROOT}/runtime/src", path],
        capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"myrwircap_ids: preprocess failed: {proc.stderr.strip()}")
    return proc.stdout


def parse_table(text):
    try:
        begin = text.index("myrwircaps[] = {")
        body = text[begin:text.index("\n};", begin)].split("{", 1)[1]
    except ValueError:
        sys.exit("myrwircaps: table not found in preprocessed source")
    entries, depth, start = [], 0, 0
    for i, c in enumerate(body):
        if c == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                entries.append(body[start:i])
    if not entries:
        sys.exit("myrwircaps: empty table")
    rows = []
    for index, entry in enumerate(entries):
        fields = split_fields(entry)
        name = decode_literal(fields[0])
        if len(fields) >= 4 and fields[3] not in NONE_KINDS:
            kind = decode_literal(fields[3])
        else:
            kind = "-"
        width = fields[4] if len(fields) >= 5 else "0"
        rows.append((index, name, kind, width))
    return rows


def format_rows(rows):
    return "".join(f"{i}\t{n}\t{k}\t{w}\n" for i, n, k, w in rows)


def describe(row):
    index, name, kind, width = row
    label = name if name else "<route>"
    return f'name="{label}"  result_kind="{kind}"  width={width}'


def compare(actual, expected):
    limit = min(len(actual), len(expected))
    for i in range(limit):
        if actual[i] != expected[i]:
            print(f"first mismatch at index {i}:")
            print(f"  myrwircaps  {describe(actual[i])}")
            print(f"  fixture     {describe(expected[i])}")
            return 1
    if len(actual) != len(expected):
        print(f"row count mismatch: myrwircaps {len(actual)} != fixture "
              f"{len(expected)}; first extra at index {limit}")
        extra = actual[limit] if len(actual) > limit else expected[limit]
        print(f"  {'myrwircaps' if len(actual) > limit else 'fixture'}"
              f"  {describe(extra)}")
        return 1
    return 0


def contract(rows):
    if rows[-1][1]:
        print(f"route tail missing: last row at index {len(rows) - 1} is "
              f"{describe(rows[-1])}")
        return 1
    typed = [n for _, n, k, _ in rows if k != "-"]
    try:
        sys.path.insert(0, HERE)
        import isa_conformance
        covered = {f"{kind}·{op}" for kind, op, *_ in isa_conformance.entries()}
    except Exception as exc:  # noqa: BLE001
        sys.exit(f"myrwircap_ids: cannot load isa_conformance: {exc}")
    missing = set(typed) - covered
    extra_ops = covered - set(typed)
    if len(typed) != 156 or missing or extra_ops:
        print(f"typed set mismatch: {len(typed)} rows, {len(missing)} "
              f"not covered, {len(extra_ops)} extra")
        if missing:
            print(f"  not covered: {sorted(missing)[:8]}")
        if extra_ops:
            print(f"  extra: {sorted(extra_ops)[:8]}")
        return 1
    print(f"ok: {len(rows)} rows, {len(typed)} typed, route at "
          f"{len(rows) - 1}")
    return 0


def selftest():
    base = [(0, "call", "-", "0"), (1, "return", "-", "0")]
    cases = [
        ("name", base, [(0, "call", "-", "0"), (1, "goto", "-", "0")],
         ['index 1', "return", "goto"]),
        ("kind", base, [(0, "call", "-", "0"), (1, "return", "bool", "0")],
         ['index 1', 'result_kind="bool"']),
        ("width", base, [(0, "call", "-", "0"), (1, "return", "-", "8")],
         ['index 1', "width=8"]),
        ("count", base, [(0, "call", "-", "0")],
         ['index 1', "row count"]),
    ]
    failures = []
    for label, left, right, expect in cases:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = compare(left, right)
        out = buf.getvalue()
        if code == 0 or not all(e in out for e in expect):
            failures.append((label, out))
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        code = compare(base, list(base))
    if code != 0:
        failures.append(("equal", buf.getvalue()))
    if failures:
        for label, out in failures:
            print(f"selftest failed: {label}\n{out}", file=sys.stderr)
        return 1
    print(f"ok: selftest {len(cases)} mismatches + 1 equal")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dump", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    actual = parse_table(preprocess())
    if args.dump:
        sys.stdout.write(format_rows(actual))
        return 0
    with open(FIXTURE, encoding="utf-8") as f:
        expected = []
        for line in f:
            index, name, kind, width = line.rstrip("\n").split("\t")
            expected.append((int(index), name, kind, width))
    rc = compare(actual, expected)
    return contract(actual) if rc == 0 else rc


if __name__ == "__main__":
    sys.exit(main())
