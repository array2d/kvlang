#!/usr/bin/env python3
"""从仓库 native 作者清单生成算子表（src/ops_gen.cpp）。

清单是唯一事实：layout/tests/fixtures/isa_native.txt
（后续挪到 stdlib/kvlang/spec/isa/native.txt）。

两个正交维度：
- policy：no-malloc 口径，base 174 / complex 126；
- impl：执行方式，Typed 166 / Control 7 / Dynamic 52 / Handoff 75。
"""
import sys

FIXTURE_DEFAULT = "../layout/tests/fixtures/isa_native.txt"

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
SPECIAL = {"=": "Copy", "bool": "Bool", "br": "Br", "call": "Call", "goto": "Goto",
           "debugger": "Debugger", "return": "Return"}
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
        if fam in KIND or fam == "kvlang":
            return "Impl::Typed"
        return "Impl::Handoff"
    if name in KIND:
        return "Impl::Typed"  # bare kind = cast
    if name in GENERIC or name.startswith("char/"):
        return "Impl::Dynamic"  # 泛型数值/比较 + char cast：运行动态分派
    return "Impl::Handoff"      # 容器（array/obj/map）等


def op_of(name):
    impl = impl_of(name)
    if impl == "Impl::Control":
        return f"Op::{SPECIAL[name]}"
    if impl == "Impl::Handoff":
        return "Op::None"
    if name in GENERIC:
        return f"Op::{GENERIC[name]}"
    if "·" in name:
        return f"Op::{SUF[name.split('·', 1)[1]]}"
    return "Op::Cast"  # 裸 kind 与 char/* 都是 cast


def kind_of(name):
    impl = impl_of(name)
    if impl in ("Impl::Control", "Impl::Handoff"):
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
    return "Class::Base" if impl_of(name) in ("Impl::Typed", "Impl::Control") else "Class::Complex"


def classify(name):
    kind, w = kind_of(name)
    return (policy_of(name), op_of(name), kind, w, impl_of(name))


def main():
    fixture = sys.argv[1] if len(sys.argv) > 1 else FIXTURE_DEFAULT
    names = []
    with open(fixture, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            first = line.split("\t", 1)[0].split(" ", 1)[0]
            if first in ("ver", "count", "hash"):
                continue
            names.append(first)
    if len(names) != 300:
        sys.exit(f"fixture: expected 300 names, got {len(names)}")
    out = ["/* 生成：native 作者清单 → 算子表。源 layout/tests/fixtures/isa_native.txt。勿手改。 */",
           '#include "ops.hpp"', "", "namespace kv::ops {", "",
           "const Entry kBaseOps[] = {"]
    counts = {"Typed": 0, "Control": 0, "Dynamic": 0, "Handoff": 0}
    for n in names:
        cls, op, kind, w, impl = classify(n)
        counts[impl.split("::", 1)[1]] += 1
        out.append(f'    {{"{n}", {op}, {kind}, {w}, {cls}, {impl}}},')
    out += ["};", "", "const int kBaseOpsN = 300;", "", "}  // namespace kv::ops", ""]
    sys.stderr.write(
        f"impl counts: Typed={counts['Typed']} Control={counts['Control']} "
        f"Dynamic={counts['Dynamic']} Handoff={counts['Handoff']}\n")
    sys.stdout.write("\n".join(out))


if __name__ == "__main__":
    main()
