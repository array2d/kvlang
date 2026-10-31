#!/usr/bin/env python3
"""Check repository ISA generation and runtimec2 implementation consistency.

The repository-native manifest, generator rules, generated opcode table, and
runtime dispatch registrations are always checked.  A workspace CSV remains an
optional analysis view and can add checks, but can never disable the repository
checks.
"""

import csv
import importlib.util
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
GENERATED_TABLE = ROOT / "runtimec2/src/rwir_gen.cpp"
RWIR_HEADER = ROOT / "runtimec2/include/rwir.hpp"
VM_SOURCE = ROOT / "runtimec2/src/vmkv.cpp"
NATIVE_SOURCE = ROOT / "runtimec2/src/vmkv_native.cpp"

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
    r'(?P<policy>Class::\w+),\s+(?P<impl>Impl::\w+)\},\s*$',
    re.MULTILINE,
)
CAP_ARRAY_RE = re.compile(
    r"constexpr\s+NativeCap\s+(?P<array>\w+)\[\]\s*=\s*\{(?P<body>.*?)\n\};",
    re.DOTALL,
)
CAP_RE = re.compile(r'NCAP\("([^"]+)",\s*(\w+)\)')
FAMILY_TABLE_RE = re.compile(
    r"constexpr\s+NativeFamily\s+kFamilies\[\]\s*=\s*\{(?P<body>.*?)\n\};",
    re.DOTALL,
)
FAMILY_RE = re.compile(r'\{"([^"]+)",\s*(\w+),\s*NCAPS\(\2\)\}')


@dataclass(frozen=True)
class Entry:
    name: str
    op: str
    kind: str
    width: int
    policy: str
    impl: str


def load_generator():
    spec = importlib.util.spec_from_file_location("gen_ops", HERE / "gen_ops.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load tools/gen_ops.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_manifest(generator, errors):
    manifest = next((Path(path) for path in generator.FIXTURE_CANDIDATES if Path(path).is_file()), None)
    if manifest is None:
        errors.append("native manifest is missing")
        return None, []

    names = []
    expected_count = None
    for line_number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if fields[0] == "count":
            try:
                expected_count = int(fields[1])
            except (IndexError, ValueError):
                errors.append(f"{manifest}:{line_number}: invalid count header")
            continue
        if fields[0] in {"ver", "hash"}:
            continue
        names.append(fields[0].split(" ", 1)[0])

    if expected_count is None:
        errors.append(f"{manifest}: missing count header")
    elif expected_count != len(names):
        errors.append(f"{manifest}: count={expected_count}, records={len(names)}")
    duplicates = sorted(name for name, count in Counter(names).items() if count > 1)
    if duplicates:
        errors.append("native manifest has duplicate opcodes: " + ", ".join(duplicates))
    return manifest, names


def expected_entries(generator, names):
    entries = []
    for name in names:
        policy, op, kind, width, impl = generator.classify(name)
        entries.append(Entry(name, op, kind, int(width), policy, impl))
    return entries


def load_generated_table(errors):
    source = GENERATED_TABLE.read_text(encoding="utf-8")
    entries = [
        Entry(
            match.group("name"),
            match.group("op"),
            match.group("kind"),
            int(match.group("width")),
            match.group("policy"),
            match.group("impl"),
        )
        for match in TABLE_ENTRY_RE.finditer(source)
    ]
    declared = re.search(r"const\s+int\s+kMyrwircapsN\s*=\s*(\d+)\s*;", source)
    if declared is None:
        errors.append(f"{GENERATED_TABLE}: missing kMyrwircapsN")
    elif int(declared.group(1)) != len(entries):
        errors.append(
            f"{GENERATED_TABLE}: kMyrwircapsN={declared.group(1)}, parsed entries={len(entries)}"
        )
    duplicates = sorted(entry.name for entry, count in Counter(entries).items() if count > 1)
    if duplicates:
        errors.append("generated table has duplicate entries: " + ", ".join(duplicates))
    return entries


def compare_generated(expected, actual, errors):
    if len(expected) != len(actual):
        errors.append(f"generated table size mismatch: rules={len(expected)}, table={len(actual)}")
    for index, (want, got) in enumerate(zip(expected, actual), 1):
        if want != got:
            errors.append(f"generated table entry {index} mismatch: rules={want}, table={got}")


def load_native_handlers(errors):
    source = NATIVE_SOURCE.read_text(encoding="utf-8")
    arrays = {}
    for match in CAP_ARRAY_RE.finditer(source):
        array_name = match.group("array")
        caps = CAP_RE.findall(match.group("body"))
        if not caps:
            errors.append(f"native cap array {array_name} is empty or unparsable")
        arrays[array_name] = caps

    family_match = FAMILY_TABLE_RE.search(source)
    if family_match is None:
        errors.append(f"{NATIVE_SOURCE}: kFamilies is missing or unparsable")
        return {}
    families = FAMILY_RE.findall(family_match.group("body"))
    if not families:
        errors.append(f"{NATIVE_SOURCE}: kFamilies has no parsable routes")
        return {}

    routes = {}
    for prefix, array_name in families:
        if array_name not in arrays:
            errors.append(f"native family {prefix} references unknown array {array_name}")
            continue
        routes.setdefault(array_name, []).append(prefix)

    handlers = {}
    for array_name, caps in arrays.items():
        prefixes = routes.get(array_name, [])
        if not prefixes:
            errors.append(f"native cap array {array_name} is not reachable from kFamilies")
        for name, function in caps:
            if name in handlers:
                errors.append(f"duplicate native handler registration: {name}")
            handlers[name] = function
            if not any(name.startswith(prefix) for prefix in prefixes):
                errors.append(
                    f"native handler {name} in {array_name} is unreachable through prefixes {prefixes}"
                )

    required_lookup_fragments = (
        "const NativeFn* find_native(std::string_view fn)",
        "fn.starts_with(fam.prefix)",
        "fam.caps[i].name == fn",
    )
    for fragment in required_lookup_fragments:
        if fragment not in source:
            errors.append(f"native lookup contract missing source anchor: {fragment}")
    return handlers


def check_dispatch_contract(entries, handlers, errors):
    counts = Counter()
    availability = Counter()
    by_name = {entry.name: entry for entry in entries}
    folded_native = {}

    for entry in entries:
        impl = entry.impl.removeprefix("Impl::")
        counts[impl] += 1
        if impl == "Typed":
            if entry.op not in TYPED_OPS.get((entry.kind, entry.width), set()):
                errors.append(
                    f"typed opcode lacks dispatch contract: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            else:
                availability["typed-contract"] += 1
        elif impl == "Dynamic":
            if entry.kind != "Kind::Any" or entry.width != 0 or entry.op not in DYNAMIC_OPS:
                errors.append(
                    f"dynamic opcode lacks dispatch contract: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            else:
                availability["dynamic-contract"] += 1
        elif impl == "Control":
            if entry.kind != "Kind::None" or entry.width != 0 or entry.op not in CONTROL_OPS:
                errors.append(
                    f"control opcode lacks dispatch contract: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            else:
                availability["control-contract"] += 1
        elif impl == "Native":
            if entry.kind != "Kind::None" or entry.width != 0 or entry.op != "Op::None":
                errors.append(
                    f"native opcode has executable-table fields: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            folded = entry.name.replace("·", "/")
            if folded in folded_native:
                errors.append(
                    f"native folded-name collision: {folded_native[folded]} and {entry.name} -> {folded}"
                )
            folded_native[folded] = entry.name
            if folded in handlers:
                availability["native-handler"] += 1
            else:
                errors.append(f"native opcode has no registered handler: {entry.name} -> {folded}")
        elif impl == "Handoff":
            if entry.kind != "Kind::None" or entry.width != 0 or entry.op != "Op::None":
                errors.append(
                    f"handoff opcode has executable-table fields: {entry.name} "
                    f"({entry.op}, {entry.kind}, width={entry.width})"
                )
            availability["handoff-routed"] += 1
            if entry.name in KNOWN_HANDOFF_BLOCKERS:
                availability["handoff-blocker"] += 1
        else:
            errors.append(f"opcode has unsupported implementation class: {entry.name} ({entry.impl})")

    for name in sorted(KNOWN_HANDOFF_BLOCKERS):
        entry = by_name.get(name)
        if entry is None:
            errors.append(f"stale handoff blocker: {name} is absent from generated table")
        elif entry.impl != "Impl::Handoff":
            errors.append(f"stale handoff blocker: {name} is now {entry.impl}")

    for folded_name in sorted(handlers):
        if folded_name not in folded_native:
            errors.append(f"registered native handler has no Native opcode: {folded_name}")

    header = RWIR_HEADER.read_text(encoding="utf-8")
    vm_source = VM_SOURCE.read_text(encoding="utf-8")
    source_anchors = {
        "Impl enum": "enum class Impl : std::uint8_t { None = 0, Typed, Control, Dynamic, Native, Handoff }",
        "handoff dispatch": "ins.impl == rwir::Impl::Handoff || ins.impl == rwir::Impl::None",
        "native dispatch": "ins.impl == rwir::Impl::Native",
        "native lookup": "find_native(fo)",
    }
    for label, fragment in source_anchors.items():
        source = header if label == "Impl enum" else vm_source
        if fragment not in source:
            errors.append(f"runtime dispatch contract missing {label} source anchor")

    return counts, availability


def check_optional_csv(csv_path, expected_by_name):
    if not csv_path.is_file():
        print(f"SKIP optional CSV: no DB export at {csv_path} (repository guard already ran)")
        return []

    errors = []
    with csv_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None or "name" not in reader.fieldnames:
            return [f"optional CSV {csv_path} has no name column"]
        if "impl" not in reader.fieldnames:
            print(f"SKIP optional CSV: {csv_path} has no impl column (repository guard already ran)")
            return []
        rows = list(reader)

    seen = set()
    checked = 0
    for line_number, row in enumerate(rows, 2):
        name = (row.get("name") or "").strip()
        if not name:
            errors.append(f"optional CSV {csv_path}:{line_number}: empty name")
            continue
        if name in seen:
            errors.append(f"optional CSV {csv_path}:{line_number}: duplicate opcode {name}")
            continue
        seen.add(name)
        expected = expected_by_name.get(name)
        if expected is None:
            errors.append(f"optional CSV {csv_path}:{line_number}: unknown opcode {name}")
            continue
        got = (row.get("impl") or "").strip()
        if not got:
            continue
        got = got.removeprefix("Impl::")
        want = expected.impl.removeprefix("Impl::")
        checked += 1
        if got != want:
            errors.append(
                f"optional CSV {csv_path}:{line_number}: {name}: csv={got}, rules={want}"
            )

    if not errors:
        print(f"OK optional CSV impl consistency (rows={len(rows)}, checked={checked})")
    return errors


def report_failure(errors):
    print(f"FAIL ISA implementation consistency ({len(errors)} errors)")
    for error in errors:
        print(f"  {error}")


def main():
    if len(sys.argv) > 2:
        print(f"usage: {Path(sys.argv[0]).name} [optional-opcode.csv]", file=sys.stderr)
        return 2

    errors = []
    try:
        generator = load_generator()
        manifest, names = load_manifest(generator, errors)
        expected = expected_entries(generator, names)
        generated = load_generated_table(errors)
        compare_generated(expected, generated, errors)
        handlers = load_native_handlers(errors)
        counts, availability = check_dispatch_contract(generated, handlers, errors)
    except (OSError, RuntimeError) as exc:
        errors.append(str(exc))
        manifest = None
        expected = []
        counts = Counter()
        availability = Counter()

    if errors:
        report_failure(errors)
        return 1

    print(
        "OK repository ISA guard "
        f"(manifest={manifest}, ops={len(generated)}; "
        f"generated typed={counts['Typed']} control={counts['Control']} "
        f"dynamic={counts['Dynamic']} native={counts['Native']} handoff={counts['Handoff']}; "
        f"available native-handler={availability['native-handler']} "
        f"routed-handoff={availability['handoff-routed']} "
        f"blocked-handoff={availability['handoff-blocker']} "
        f"unsupported={availability['unsupported']})"
    )
    print(
        "LIMITED guarantee: validates generation, dispatch shapes, lookup reachability, and "
        "Native registration; it does not prove handler bodies or runtime value semantics"
    )
    for name, reason in sorted(KNOWN_HANDOFF_BLOCKERS.items()):
        print(f"BLOCKER {name}: {reason}")

    if len(sys.argv) == 2:
        csv_path = Path(sys.argv[1]).resolve()
        csv_errors = check_optional_csv(csv_path, {entry.name: entry for entry in expected})
        if csv_errors:
            report_failure(csv_errors)
            return 1
    else:
        print("SKIP optional CSV: not requested (repository guard already ran)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
