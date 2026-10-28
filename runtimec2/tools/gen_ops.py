#!/usr/bin/env python3
"""从仓库 native 作者清单生成算子表（src/ops_gen.cpp）。

清单是唯一事实：layout/tests/fixtures/isa_native.txt
（后续挪到 stdlib/kvlang/spec/isa/native.txt）。

两个正交维度：
- policy：no-malloc 口径，base 174 / complex 126；
- impl：执行方式，Typed 166 / Control 7 / Dynamic 52 / Handoff 75。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# 清单位置（#507 后迁到 stdlib）；两级都缺即失败，不静默跳过。
FIXTURE_CANDIDATES = [
    os.path.join(ROOT, "stdlib/kvlang/spec/isa/native.txt"),
    os.path.join(ROOT, "layout/tests/fixtures/isa_native.txt"),
]

KIND = {
    "int8": ("Int", 1), "int16": ("Int", 2), "int32": ("Int", 4), "int64": ("Int", 8),
    "uint8": ("Uint", 1), "uint16": ("Uint", 2), "uint32": ("Uint", 4),
    "uint64": ("Uint", 8), "float32": ("Float", 4), "float64": ("Float", 8),
}
SUF = {
    "add": "Add", "sub": "Sub", "mul": "Mul", "div": "Div", "mod": "Mod", "abs": "Abs",
    "neg": "Neg", "sign": "Sign", "max": "Max", "min": "Min", "bitand": "BitAnd",
    "bitor": "BitOr", "bitxor": "BitXor", "shl": "Shl", "shr": "Shr", "eq": "Eq",
    "neq": "Neq", "lt": "Lt", "le": "Le", "gt": "Gt", "ge": "Ge", "sqrt": "Sqrt",
    "exp": "Exp", "log": "Log", "pow": "Pow",
}
NATIVE_FAMILIES = {"time", "time/duration", "random", "kvspace", "vthread"}
SPECIAL = {"=": "Copy", "bool": "Bool", "br": "Br", "call": "Call", "goto": "Goto",
           "return": "Return"}
GENERIC = {
    "add": "Add", "+": "Add", "sub": "Sub", "-": "Sub", "mul": "Mul", "×": "Mul",
    "div": "Div", "÷": "Div", "mod": "Mod", "%": "Mod", "eq": "Eq", "==": "Eq",
    "neq": "Neq", "!=": "Neq", "≠": "Neq", "lt": "Lt", "<": "Lt", "gt": "Gt",
    ">": "Gt", "le": "Le", "<=": "Le", "≤": "Le", "ge": "Ge", ">=": "Ge", "≥": "Ge",
    "and": "And", "&&": "And", "or": "Or", "||": "Or", "not": "Not", "!": "Not",
    "bitand": "BitAnd", "&": "BitAnd", "bitor": "BitOr", "|": "BitOr",
    "bitxor": "BitXor", "^": "BitXor", "shl": "Shl", "<<": "Shl", "shr": "Shr",
    ">>": "Shr", "pow": "Pow", "sqrt": "Sqrt", "√": "Sqrt", "exp": "Exp",
    "log": "Log", "neg": "Neg", "abs": "Abs", "sign": "Sign", "max": "Max",
    "min": "Min",
}


def impl_of(name):
    if name in SPECIAL:
        return "Impl::Control"
    if "·" in name:
        fam = name.split("·", 1)[0]
        if fam in NATIVE_FAMILIES or fam == "kvlang":
            # kvlang·abs = 取地址（&x），与 C runtime 同责 → Native。
            return "Impl::Native"
        if fam in KIND:
            return "Impl::Typed"
        # 用户裁定：容器/string/ndarray 是 runtimec2 核心职责（Native）。
        return "Impl::Native"
    if name in KIND:
        return "Impl::Typed"  # bare kind = cast
    if name in GENERIC or name.startswith("char/"):
        return "Impl::Dynamic"  # 泛型数值/比较 + char cast：运行动态分派
    return "Impl::Native"


def op_of(name):
    impl = impl_of(name)
    if impl == "Impl::Control":
        return f"Op::{SPECIAL[name]}"
    if impl in ("Impl::Handoff", "Impl::Native"):
        return "Op::None"  # 按名字分派
    if name in GENERIC:
        return f"Op::{GENERIC[name]}"
    if "·" in name:
        return f"Op::{SUF[name.split('·', 1)[1]]}"
    return "Op::Cast"  # 裸 kind 与 char/* 都是 cast


def kind_of(name):
    impl = impl_of(name)
    if impl in ("Impl::Control", "Impl::Handoff", "Impl::Native"):
        return "Kind::None", 0
    if impl == "Impl::Dynamic":
        return "Kind::Any", 0
    if "·" in name:
        fam = name.split("·", 1)[0]
        if fam == "kvlang":
            return "Kind::Any", 0  # kvlang·abs：类型由操作数决定
        kind, w = KIND[fam]
        return f"Kind::{kind}", w
    kind, w = KIND[name]
    return f"Kind::{kind}", w


def policy_of(name):
    # no-malloc 口径：Typed/Control = base 174；Dynamic/Handoff = complex 126。
    return ("Class::Base"
            if impl_of(name) in ("Impl::Typed", "Impl::Control")
            else "Class::Complex")


def classify(name):
    kind, w = kind_of(name)
    return (policy_of(name), op_of(name), kind, w, impl_of(name))


def main():
    if len(sys.argv) > 1:
        fixture = sys.argv[1]
    else:
        fixture = next((p for p in FIXTURE_CANDIDATES if os.path.isfile(p)), None)
        if not fixture:
            sys.exit("native list not found: " + ", ".join(FIXTURE_CANDIDATES))
    names = []
    expect_n = None
    with open(fixture, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            first = line.split("\t", 1)[0].split(" ", 1)[0]
            if first == "count":
                expect_n = int(line.split("\t", 1)[1].strip())
                continue
            if first in ("ver", "hash"):
                continue
            names.append(first)
    if expect_n is None:
        sys.exit("fixture missing count header")
    if len(names) != expect_n:
        sys.exit(f"fixture: expected {expect_n} names, got {len(names)}")
    out = ["/* 生成：native 作者清单 → 算子表。源 layout/tests/fixtures/isa_native.txt。勿手改。 */",
           '#include "rwir.hpp"', "", "namespace kv::ops {", "",
           "const Entry myrwircaps[] = {"]
    counts = {"Typed": 0, "Control": 0, "Dynamic": 0, "Native": 0, "Handoff": 0}
    for n in names:
        cls, op, kind, w, impl = classify(n)
        counts[impl.split("::", 1)[1]] += 1
        out.append(f'    {{"{n}", {op}, {kind}, {w}, {cls}, {impl}}},')
    out += ["};", "", f"const int kMyrwircapsN = {expect_n};", "",
            "}  // namespace kv::ops", ""]
    sys.stderr.write(
        f"impl counts: Typed={counts['Typed']} Control={counts['Control']} "
        f"Dynamic={counts['Dynamic']} Native={counts['Native']} "
        f"Handoff={counts['Handoff']}\n")
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
