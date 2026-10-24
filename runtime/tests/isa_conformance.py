#!/usr/bin/env python3
"""P1b3 逐 entry conformance：156 个 typed core（`<kind>·<op>`）。

对每个 entry 生成一个最小 kv 程序，layout + 运行，断言结果 kind/值正确。
使用 ctest 环境变量 KVLANG_TERM_BIN / KVLANG_LAYOUT_BIN；shm 后端。
"""

import os
import shutil
import subprocess
import sys
import tempfile

TERM = os.environ["KVLANG_TERM_BIN"]
LAYOUT = os.environ["KVLANG_LAYOUT_BIN"]

SIGNED = {"int8", "int16", "int32", "int64"}
UNSIGNED = {"uint8", "uint16", "uint32", "uint64"}
BITS = {"int64", "uint64"}
FLOATS = {"float32", "float64"}

# op -> (operands, expected)；operand/expected 均按 kind 值（未 cast）。
BIN = {
    "add": ((2, 3), 5),
    "sub": ((7, 3), 4),
    "mul": ((2, 3), 6),
    "div": ((6, 2), 3),
    "mod": ((7, 3), 1),
    "max": ((2, 3), 3),
    "min": ((2, 3), 2),
}
BIT = {"bitand": ((6, 3), 2), "bitor": ((6, 3), 7), "bitxor": ((6, 3), 5),
       "shl": ((3, 1), 6), "shr": ((6, 1), 3)}
CMP = {"eq": ((2, 3), False), "neq": ((2, 3), True), "lt": ((2, 3), True),
       "le": ((2, 3), True), "gt": ((2, 3), False), "ge": ((2, 3), False)}
UN = {"neg": (-2,), "abs": (-2,)}
FBIN = {"add": ((2.5, 1.5), 4.0), "sub": ((2.5, 1.5), 1.0),
        "mul": ((2.5, 2.0), 5.0), "div": ((5.0, 2.0), 2.5),
        "max": ((2.5, 3.5), 3.5), "min": ((2.5, 3.5), 2.5)}
FUN = {"neg": (-2.5, -2.5), "abs": (-2.5, 2.5), "sqrt": (4.0, 2.0),
       "exp": (0.0, 1.0), "log": (1.0, 0.0), "sign": (-2.5, -1)}


def lit(kind, v):
    if kind in FLOATS:
        return f"{kind}({v!r})"
    return f"{kind}({v})"


def fmt(kind, v):
    if kind in FLOATS or isinstance(v, float):
        return repr(float(v))
    return str(int(v))


def entries():
    """(kind, op, arity, operands, expected)"""
    for kind in ["int8", "int16", "int32", "int64"]:
        for op, (a, e) in BIN.items():
            yield kind, op, 2, a, e
        for op, (a, e) in CMP.items():
            yield kind, op, 2, a, e
        yield kind, "neg", 1, (2,), -2
        yield kind, "abs", 1, (2,), 2
    for kind in ["uint8", "uint16", "uint32", "uint64"]:
        for op, (a, e) in BIN.items():
            yield kind, op, 2, a, e
        for op, (a, e) in CMP.items():
            yield kind, op, 2, a, e
    for kind in BITS:
        for op, (a, e) in BIT.items():
            yield kind, op, 2, a, e
    yield "int64", "sign", 1, (2,), 1
    for kind in ["float32", "float64"]:
        for op, (a, e) in FBIN.items():
            yield kind, op, 2, a, e
        for op, (a, e) in CMP.items():
            yield kind, op, 2, a, e
        yield kind, "neg", 1, (2.5,), -2.5
        yield kind, "abs", 1, (2.5,), 2.5
    for op, (a, e) in [("sqrt", (4.0, 2.0)), ("exp", (0.0, 1.0)),
                       ("log", (1.0, 0.0)), ("pow", ((2.0, 3.0), 8.0))]:
        args = a if isinstance(a, tuple) else (a,)
        yield "float64", op, 2 if op == "pow" else 1, args, e
    yield "float64", "sign", 1, (2.5,), 1


def src(kind, op, arity, args, want):
    call_args = ", ".join(lit(kind, a) for a in args)
    call = f"{kind}·{op}({call_args})"
    if arity == 1 and op in ("sign",):
        want_txt = fmt("int64", want)
        cond = f"r == int64({want_txt})"
    elif isinstance(want, bool):
        cond = "r == true" if want else "r == false"
    elif kind in FLOATS and float(want) < 0:
        # 负浮点字面量会被解析成字符串；用 0 - x 表达。
        cond = f"r == {kind}(0.0) - {kind}({abs(float(want))!r})"
    else:
        cond = f"r == {lit(kind, fmt(kind, want))}"
    return (
        "rwfunc test() -> () {\n"
        f"\t{call} -> r\n"
        f"\tif ({cond}) {{\n\t\tprintln(\"OK\")\n\t}} else {{\n"
        "\t\tprintln(\"BAD\")\n\t}\n}\n"
    )


def main():
    base = tempfile.mkdtemp(prefix="isa-conf-")
    fails = []
    total = 0
    try:
        for kind, op, arity, args, want in entries():
            total += 1
            prog = os.path.join(base, "case.kv")
            with open(prog, "w") as f:
                f.write(src(kind, op, arity, args, want))
            dsn = f"shm://{base}/s"
            shutil.rmtree(f"{base}/s", ignore_errors=True)
            for suf in ("", ".sbo.data", ".sbo.head", ".isa",
                        ".isa.sbo.data", ".isa.sbo.head"):
                try:
                    os.unlink(f"{base}/s{suf}")
                except OSError:
                    pass
            env = {**os.environ, "KVSPACE": dsn}
            boot = subprocess.run([TERM, "isa", "publish"], env=env,
                                  capture_output=True, text=True)
            if boot.returncode != 0:
                fails.append((kind, op, "publish", boot.stderr[:120]))
                continue
            lay = subprocess.run([LAYOUT, prog, dsn], env=env,
                                 capture_output=True, text=True)
            if lay.returncode != 0:
                fails.append((kind, op, "layout", lay.stderr[:160]))
                continue
            run = subprocess.run([TERM, "test"], env=env,
                                 capture_output=True, text=True)
            out = run.stdout.strip().splitlines()
            if run.returncode != 0 or not out or out[0] != "OK":
                fails.append((kind, op, "run", (run.stderr or run.stdout)[:160]))
    finally:
        shutil.rmtree(base, ignore_errors=True)
    if fails:
        for f in fails[:10]:
            print("FAIL", f, file=sys.stderr)
        print(f"{len(fails)}/{total} conformance failures", file=sys.stderr)
        return 1
    print(f"ok: {total} typed-core conformance entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
