#!/usr/bin/env python3
"""Check the runtimec2 myrwircap table is internally well-formed.

`runtimec2/src/rwir_gen.cpp` is the single source of truth (table is code);
there is no external manifest and no generator. 每条 rwir 的实现载体只有两种：
引擎内联（Impl 为 Typed/Control/Dynamic/Handoff）与 rwir handler 表项
（Impl::None + handler != None，按 enum id 直接索引，无字符串查找）。
"""

import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TABLE = ROOT / "runtimec2/src/rwir_gen.cpp"

KNOWN_HANDOFF_BLOCKERS = {
    "kvspace·watch": (
        "runtimec2 yields the exact PC, but the default byteseek driver has no "
        "in-process handler or registered external claimant"
    ),
}

CONTROL_OPS = {
    "Op::Bool",
    "Op::Br",
    "Op::Call",
    "Op::Copy",
    "Op::Goto",
    "Op::Return",
}
DYNAMIC_OPS = {
    "Op::Abs",
    "Op::Add",
    "Op::And",
    "Op::BitAnd",
    "Op::BitOr",
    "Op::BitXor",
    "Op::Cast",
    "Op::Div",
    "Op::Eq",
    "Op::Exp",
    "Op::Ge",
    "Op::Gt",
    "Op::Le",
    "Op::Log",
    "Op::Lt",
    "Op::Max",
    "Op::Min",
    "Op::Mod",
    "Op::Mul",
    "Op::Neg",
    "Op::Neq",
    "Op::Not",
    "Op::Or",
    "Op::Pow",
    "Op::Shl",
    "Op::Shr",
    "Op::Sign",
    "Op::Sqrt",
    "Op::Sub",
}

INT_BASE_OPS = {
    "Op::Abs",
    "Op::Add",
    "Op::Cast",
    "Op::Div",
    "Op::Eq",
    "Op::Ge",
    "Op::Gt",
    "Op::Le",
    "Op::Lt",
    "Op::Max",
    "Op::Min",
    "Op::Mod",
    "Op::Mul",
    "Op::Neg",
    "Op::Neq",
    "Op::Sub",
}
UINT_BASE_OPS = {
    "Op::Add",
    "Op::Cast",
    "Op::Div",
    "Op::Eq",
    "Op::Ge",
    "Op::Gt",
    "Op::Le",
    "Op::Lt",
    "Op::Max",
    "Op::Min",
    "Op::Mod",
    "Op::Mul",
    "Op::Neq",
    "Op::Sub",
}
FLOAT32_OPS = {
    "Op::Abs",
    "Op::Add",
    "Op::Cast",
    "Op::Div",
    "Op::Eq",
    "Op::Ge",
    "Op::Gt",
    "Op::Le",
    "Op::Lt",
    "Op::Max",
    "Op::Min",
    "Op::Mul",
    "Op::Neg",
    "Op::Neq",
    "Op::Sub",
}
BIT_OPS = {"Op::BitAnd", "Op::BitOr", "Op::BitXor", "Op::Shl", "Op::Shr"}
TYPED_OPS = {
    ("Kind::Int", 1): INT_BASE_OPS,
    ("Kind::Int", 2): INT_BASE_OPS,
    ("Kind::Int", 4): INT_BASE_OPS,
    ("Kind::Int", 8): INT_BASE_OPS | BIT_OPS | {"Op::Sign"},
    ("Kind::Uint", 1): UINT_BASE_OPS,
    ("Kind::Uint", 2): UINT_BASE_OPS,
    ("Kind::Uint", 4): UINT_BASE_OPS,
    ("Kind::Uint", 8): UINT_BASE_OPS | BIT_OPS,
    ("Kind::Float", 4): FLOAT32_OPS,
    ("Kind::Float", 8): FLOAT32_OPS
    | {"Op::Exp", "Op::Log", "Op::Pow", "Op::Sign", "Op::Sqrt"},
}

TABLE_ENTRY_RE = re.compile(
    r'^\s*\{"(?P<name>[^"]+)",\s+(?P<op>Op::\w+),\s+'
    r'(?P<kind>Kind::\w+),\s+(?P<width>\d+),\s+'
    r'(?P<policy>Class::\w+),\s+(?P<impl>Impl::\w+),\s+'
    r'(?P<handler>RwirHandlerId::\w+)\},\s*$',
    re.MULTILINE,
)


@dataclass(frozen=True)
class Entry:
    name: str
    op: str
    kind: str
    width: int
    policy: str
    impl: str
    handler: str


def load_table(errors):
    source = TABLE.read_text(encoding="utf-8")
    entries = [
        Entry(
            match.group("name"),
            match.group("op"),
            match.group("kind"),
            int(match.group("width")),
            match.group("policy"),
            match.group("impl"),
            match.group("handler"),
        )
        for match in TABLE_ENTRY_RE.finditer(source)
    ]
    declared = re.search(r"const\s+int\s+kMyrwircapsN\s*=\s*(\d+)\s*;", source)
    if declared is None:
        errors.append(f"{TABLE}: missing kMyrwircapsN")
    elif int(declared.group(1)) != len(entries):
        errors.append(
            f"{TABLE}: kMyrwircapsN={declared.group(1)}, parsed entries={len(entries)}"
        )
    duplicates = sorted(
        name for name, count in Counter(e.name for e in entries).items() if count > 1
    )
    if duplicates:
        errors.append("table has duplicate entries: " + ", ".join(duplicates))
    return entries


def check_contract(entries, errors):
    counts = Counter()
    by_name = {entry.name: entry for entry in entries}
    folded_handler = {}

    for entry in entries:
        impl = entry.impl.removeprefix("Impl::")
        counts[impl if impl != "None" else "handler"] += 1
        if impl == "Typed":
            if entry.op not in TYPED_OPS.get((entry.kind, entry.width), set()):
                errors.append(
                    f"typed opcode lacks dispatch contract: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            if entry.handler != "RwirHandlerId::None":
                errors.append(f"inline opcode must not carry a handler: {entry.name}")
        elif impl == "Dynamic":
            if entry.kind != "Kind::Any" or entry.width != 0 or entry.op not in DYNAMIC_OPS:
                errors.append(
                    f"dynamic opcode lacks dispatch contract: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            if entry.handler != "RwirHandlerId::None":
                errors.append(f"inline opcode must not carry a handler: {entry.name}")
        elif impl == "Control":
            if entry.kind != "Kind::None" or entry.width != 0 or entry.op not in CONTROL_OPS:
                errors.append(
                    f"control opcode lacks dispatch contract: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            if entry.handler != "RwirHandlerId::None":
                errors.append(f"inline opcode must not carry a handler: {entry.name}")
        elif impl == "Handoff":
            if entry.kind != "Kind::None" or entry.width != 0 or entry.op != "Op::None":
                errors.append(
                    f"handoff opcode has executable-table fields: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            if entry.handler != "RwirHandlerId::None":
                errors.append(f"handoff opcode must not carry a handler: {entry.name}")
            if entry.name in KNOWN_HANDOFF_BLOCKERS:
                counts["handoff-blocker"] += 1
        elif impl == "None":
            if entry.handler == "RwirHandlerId::None":
                errors.append(f"opcode has no implementation carrier: {entry.name}")
                continue
            if entry.kind != "Kind::None" or entry.width != 0 or entry.op != "Op::None":
                errors.append(
                    f"handler opcode has executable-table fields: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            folded = entry.name.replace("·", "/")
            if folded in folded_handler:
                errors.append(
                    f"handler folded-name collision: {folded_handler[folded]} and "
                    f"{entry.name} -> {folded}"
                )
            folded_handler[folded] = entry.name
        else:
            errors.append(f"opcode has unsupported implementation class: {entry.name} ({entry.impl})")

    for name, reason in sorted(KNOWN_HANDOFF_BLOCKERS.items()):
        entry = by_name.get(name)
        if entry is None:
            errors.append(f"stale handoff blocker: {name} is absent from the table")
        elif entry.impl != "Impl::Handoff":
            errors.append(f"stale handoff blocker: {name} is now {entry.impl}")
        else:
            print(f"BLOCKER {name}: {reason}")

    return counts


def report_failure(errors):
    print(f"FAIL myrwircap table consistency ({len(errors)} errors)")
    for error in errors:
        print(f"  {error}")


def main():
    if len(sys.argv) > 1:
        print(f"usage: {Path(sys.argv[0]).name}", file=sys.stderr)
        return 2

    errors = []
    try:
        entries = load_table(errors)
        counts = check_contract(entries, errors)
    except OSError as exc:
        errors.append(str(exc))
        entries = []
        counts = Counter()

    if errors:
        report_failure(errors)
        return 1

    print(
        "OK myrwircap guard "
        f"(ops={len(entries)}; typed={counts['Typed']} control={counts['Control']} "
        f"dynamic={counts['Dynamic']} handoff={counts['Handoff']} "
        f"handler={counts['handler']}; "
        f"blocked-handoff={counts['handoff-blocker']})"
    )
    print(
        "LIMITED guarantee: validates table shape and implementation carriers; "
        "it does not prove handler bodies or runtime value semantics"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
