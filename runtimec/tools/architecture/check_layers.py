
from __future__ import annotations

import argparse
import contextlib
import io
import re
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

SOURCE_SUFFIXES = {".c", ".h"}
PRODUCT_DIRECTORIES = (
    "include", "interface", "contract", "host", "binrwir", "vmkv",
    "binrwir_compiler", "vmbin", "engine", "generated",
)
KNOWN_LAYERS = {
    "public", "interface", "contract", "host", "binrwir", "vmkv",
    "binrwir_compiler", "vmbin", "engine",
}
ALLOWED_DEPENDENCIES = {
    "public": {"public"},
    "contract": {"public", "contract"},
    "host": {"contract", "host"},
    "binrwir": {"contract", "binrwir"},
    "vmkv": {"contract", "binrwir", "vmkv"},
    "binrwir_compiler": {"contract", "binrwir", "binrwir_compiler"},
    "vmbin": {"contract", "binrwir", "vmbin"},
    "engine": {
        "contract", "host", "binrwir", "vmkv", "binrwir_compiler",
        "vmbin", "engine",
    },
    "interface": {"public", "contract", "engine", "interface"},
}
EXPECTED_COMPONENT_ORDER = (
    ("runtimec_contract", "contract"),
    ("runtimec_binrwir", "binrwir"),
    ("runtimec_vmkv", "vmkv"),
    ("runtimec_binrwir_compiler", "binrwir_compiler"),
    ("runtimec_vmbin", "vmbin"),
    ("runtimec_engine", "engine"),
)
EXPECTED_TARGET_DEPENDENCIES = {
    "runtimec_contract": set(),
    "runtimec_binrwir": {"runtimec_contract"},
    "runtimec_vmkv": {"runtimec_contract", "runtimec_binrwir"},
    "runtimec_binrwir_compiler": {"runtimec_contract", "runtimec_binrwir"},
    "runtimec_vmbin": {"runtimec_contract", "runtimec_binrwir"},
    "runtimec_engine": {
        "runtimec_contract", "runtimec_binrwir",
        "runtimec_vmkv", "runtimec_binrwir_compiler", "runtimec_vmbin",
    },
    "runtimec_interface": {"runtimec_contract", "runtimec_engine"},
}
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
FORBIDDEN_TREE_PATTERN = re.compile(r"(?:^|/)(?:runtime|runtimec2)(?:/|$)")
COMPONENT_PATTERN = re.compile(
    r"runtimec_add_component\(\s*(runtimec_[a-z0-9_]+)\s+([a-z0-9_]+)\s*\)"
)
LINK_PATTERN = re.compile(
    r"target_link_libraries\(\s*(runtimec_[a-z0-9_]+)\s+(.*?)\)",
    re.DOTALL,
)
INTERNAL_PREFIXES = {
    "contract", "host", "binrwir", "vmkv", "binrwir_compiler",
    "vmbin", "engine", "interface",
}
POLICY_LAYERS = {"binrwir_compiler", "vmbin"}
EXECUTION_WINDOW_FILES = {"engine/execution_window.c", "engine/execution_window.h"}
ENGINE_OWNER_FILES = EXECUTION_WINDOW_FILES | {"engine/session.c", "engine/session.h"}
DISPATCH_OWNER_FILES = EXECUTION_WINDOW_FILES
EXECUTION_ENTRY_POINTS = {
    "vmbin_execute(": "vmbin/interpreter",
    "binrwir_jit_execute(": "binrwir_compiler/jit/executor",
}
EXECUTION_TARGET_TYPE = "binrwir_execution_target_t"
EXECUTION_TARGET_OWNER = "binrwir_compiler"
BINRWIR_NEUTRAL_FILE_NAMES = {
    "value.c", "value.h", "reference.c", "reference.h", "signature.c",
    "signature.h", "instruction.c", "instruction.h", "program.c",
    "program.h", "execution_plan.c", "execution_plan.h",
}
REMOVED_BINRWIR_MODEL_FILES = (
    "execution_binding.c", "execution_binding.h",
    "reference.c", "reference.h", "value.c", "value.h",
)
REQUIRED_JIT_STATE_FILES = (
    "register_allocation.c", "register_allocation.h",
    "safepoint.c", "safepoint.h",
)
REQUIRED_JIT_SAFEPOINT_TOKENS = (
    "BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION",
    "BINRWIR_JIT_SAFEPOINT_ACTION_COMMIT_COMPLETED_STATE",
    "BINRWIR_JIT_SAFEPOINT_ACTION_DISCARD_INCOMPLETE_STATE",
    "BINRWIR_JIT_MATERIALIZATION_FLAG_COMPLETED_DIRTY",
    "binrwir_jit_safepoint_commit_completed_state",
    "binrwir_jit_safepoint_discard_incomplete_state",
)
FORBIDDEN_JIT_SAFEPOINT_TOKENS = (
    "observe_every_instruction",
    "BINRWIR_JIT_MATERIALIZATION_FLAG_DIRTY",
    "binrwir_jit_safepoint_materialize(",
)
C_NON_CODE_PATTERN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
JIT_EXCLUSIVE_STORAGE_PATTERN = re.compile(
    r"(?:register[ _]?file|register[ _]?allocation|register[ _]?allocator|"
    r"register[ _]?desc(?:riptor)?|virtual[ _]?register|"
    r"spill[ _]?slot|spill[ _]?state|spilled?[ _]?register)",
    re.IGNORECASE,
)
CONTRACT_HEADER = ("contract", "kvspace_access.h")
CONTRACT_IMPLEMENTATION = ("contract", "kvspace_access.c")
BODY_OFFSET_DEFINITION = re.compile(
    r"#define\s+RUNTIMEC_XVALUE_BODY_OFFSET_BYTES\s+(\S+)")
CONTRACT_BODY_ADDRESS_FUNCTION = "runtimec_xvalue_body_address("
CONTRACT_BODY_ADDRESS_DERIVATION = "xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES"
REQUIRED_BODY_ADDRESS_DERIVATION_TOKENS = (
    "UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES",
    CONTRACT_BODY_ADDRESS_DERIVATION,
    "RUNTIMEC_XVALUE_ADDRESS_NONE",
)
REMOVED_VALUE_CACHE_PATTERN = re.compile(
    r"\b(?:binrwir_value_slot|binrwir_cell|value_slot|cell_cache|"
    r"value_cache|register_file|virtual_register|value_base|value_count|"
    r"payload_low|payload_high)\b",
    re.IGNORECASE,
)
PROCESS_ENTRY_PATTERN = re.compile(r"\bint\s+main\s*\(")
PROCESS_GLOBAL_HOOK_PATTERN = re.compile(
    r"\b(?:atexit|signal|sigaction|pthread_create)\s*\(")
LIBRARY_DECLARATION_PATTERN = re.compile(
    r"add_library\(\s*([A-Za-z0-9_]+)\s+(SHARED|STATIC|MODULE|OBJECT|INTERFACE)\b")
EXPORTED_PRODUCT_PATTERN = re.compile(
    r"(?:target_link_options|target_sources)\(\s*([A-Za-z0-9_]+)[^)]*exports"
    r"|set_property\(\s*TARGET\s+([A-Za-z0-9_]+)[^)]*exports",
    re.DOTALL)
HARDCODED_XVALUE_BODY_OFFSET_PATTERN = re.compile(
    r"\b(?:xvalue|resolved)[A-Za-z0-9_]*address\b\s*\+\s*"
    r"(?:8U?\b|UINT64_C\s*\(\s*8\s*\))",
    re.IGNORECASE,
)

AUDIT_RULES = (
    "addressed-binrwir-layout",
    "addressed-binrwir-offset",
    "addressed-binrwir-size",
    "binrwir-owned-by-vmbin",
    "body-address-derivation-outside-contract",
    "component-order",
    "contract-body-address-derivation",
    "execution-dispatch-outside-window",
    "forbidden-layer-edge",
    "forbidden-tree-include",
    "hardcoded-body-address-derivation-outside-contract",
    "contract-anchor-missing",
    "jit-storage-outside-jit",
    "missing-addressed-binrwir-file",
    "missing-build-file",
    "missing-interface-component",
    "missing-jit-commit-discard-contract",
    "missing-jit-state-contract",
    "missing-zero-address-rejection",
    "optional-instruction-boundary",
    "optional-or-ambiguous-jit-boundary",
    "process-entry-in-product-source",
    "process-global-hook-in-product-source",
    "product-library-not-shared",
    "read-error",
    "removed-binrwir-storage-model",
    "removed-static-address-resolver",
    "removed-value-cache-model",
    "policy-merge-outside-owner",
    "stale-kvspace-header-copy",
    "target-dependencies",
    "unresolved-private-include",
    "xvalue-body-address-contract",
)
PRECONDITION_RULES = ("missing-runtimec",)
RULES = AUDIT_RULES + PRECONDITION_RULES

@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str
    detail: str

    def __post_init__(self) -> None:
        if self.rule not in RULES:
            raise ValueError(f"unregistered rule {self.rule!r}")

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.detail}"

def parse_arguments(command_arguments: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root", type=Path, default=None,
        help="repository source root, or the runtimec directory itself; "
             "defaults to the tree this script lives in",
    )
    parser.add_argument("--quiet", action="store_true", help="suppress success output")
    parser.add_argument("--selftest", action="store_true",
                        help="prove every rule can fire, using synthetic fixtures")
    return parser.parse_args(command_arguments)

def locate_runtimec(source_root: Path) -> tuple[Path, Path]:
    source_root = source_root.resolve()
    return ((source_root.parent, source_root) if source_root.name == "runtimec"
            else (source_root, source_root / "runtimec"))

def relative_display(path: Path, repository_root: Path) -> str:
    try:
        return path.resolve().relative_to(repository_root).as_posix()
    except ValueError:
        return path.as_posix()

def layer_for_relative(relative: PurePosixPath) -> str | None:
    if not relative.parts:
        return None
    if relative.parts[0] == "include":
        return "public"
    if relative.parts[0] == "generated":
        return "contract"
    return relative.parts[0] if relative.parts[0] in KNOWN_LAYERS else None

def inferred_include_layer(token: str) -> str | None:
    parts = tuple(part for part in PurePosixPath(token).parts if part not in {".", ".."})
    if not parts:
        return None
    if parts[0] == "kvlang":
        return "public"
    if parts[0] == "runtimec" and len(parts) > 1:
        parts = parts[1:]
    if not parts:
        return None
    return parts[0] if parts[0] in INTERNAL_PREFIXES else None

def resolve_include(
    token: str, source: Path, runtimec_root: Path, repository_root: Path,
) -> Path | None:
    candidates = [
        source.parent / token,
        runtimec_root / token,
        runtimec_root / "include" / token,
        *(runtimec_root / directory / token for directory in INTERNAL_PREFIXES),
        repository_root / "include" / token,
        repository_root / token,
        repository_root.parent / "kvspace" / "include" / token,
    ]
    return next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)

def function_body(code: str, name: str) -> tuple[str, int] | None:
    code = c_code_without_comments_or_literals(code)
    start = code.find(name + "(")
    while start >= 0:
        opening = code.find("{", start)
        terminator = code.find(";", start)
        if opening >= 0 and (terminator < 0 or opening < terminator):
            break
        start = code.find(name + "(", start + 1)
    else:
        return None
    depth = 0
    for index in range(opening, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[opening:index + 1], opening
    return None

def iter_product_sources(runtimec_root: Path):
    for directory_name in PRODUCT_DIRECTORIES:
        directory = runtimec_root / directory_name
        if directory.is_dir():
            for path in sorted(directory.rglob("*")):
                if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                    yield path

def cmake_target_dependencies(cmake_text: str) -> dict[str, set[str]]:
    dependencies: dict[str, set[str]] = {}
    for match in LINK_PATTERN.finditer(cmake_text):
        target, body = match.groups()
        tokens = set(re.findall(r"\bruntimec_[a-z0-9_]+\b", body))
        dependencies.setdefault(target, set()).update(tokens)
    return dependencies

def c_code_without_comments_or_literals(text: str) -> str:
    return C_NON_CODE_PATTERN.sub(
        lambda match: re.sub(r"[^\n]", " ", match.group(0)), text,
    )

def struct_fields(code: str, tag: str, alias: str) -> tuple[str, ...] | None:
    match = re.search(
        rf"typedef\s+struct\s+{re.escape(tag)}\s*\{{(.*?)\}}\s*"
        rf"{re.escape(alias)}\s*;",
        code,
        re.DOTALL,
    )
    if match is None:
        return None
    return tuple(
        " ".join(declaration.split())
        for declaration in match.group(1).split(";")
        if declaration.strip()
    )

def audit_addressed_binrwir_model(
    runtimec_root: Path, repository_root: Path,
) -> list[Finding]:
    findings: list[Finding] = []
    binrwir_root = runtimec_root / "binrwir"
    jit_root = runtimec_root / "binrwir_compiler" / "jit"

    for file_name in REMOVED_BINRWIR_MODEL_FILES:
        path = binrwir_root / file_name
        if path.exists():
            findings.append(Finding(
                relative_display(path, repository_root), 1,
                "removed-binrwir-storage-model",
                "reference/value/execution binding files must not exist",
            ))
    for file_name in REQUIRED_JIT_STATE_FILES:
        path = jit_root / file_name
        if not path.is_file():
            findings.append(Finding(
                relative_display(path, repository_root), 1,
                "missing-jit-state-contract",
                "JIT register allocation and safepoint files are required",
            ))

    safepoint_header = jit_root / "safepoint.h"
    safepoint_source = jit_root / "safepoint.c"
    lowering_header = jit_root / "lowering_plan.h"
    jit_contract_text = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (safepoint_header, safepoint_source, lowering_header)
        if path.is_file()
    )
    for token in REQUIRED_JIT_SAFEPOINT_TOKENS:
        if token not in jit_contract_text:
            findings.append(Finding(
                relative_display(safepoint_header, repository_root), 1,
                "missing-jit-commit-discard-contract", token,
            ))
    for token in FORBIDDEN_JIT_SAFEPOINT_TOKENS:
        if token in jit_contract_text:
            findings.append(Finding(
                relative_display(jit_root, repository_root), 1,
                "optional-or-ambiguous-jit-boundary", token,
            ))

    lowering_code = c_code_without_comments_or_literals(
        lowering_header.read_text(encoding="utf-8")
        if lowering_header.is_file() else "",
    )
    lowering_fields = struct_fields(
        lowering_code,
        "binrwir_jit_lowering_plan_options_t",
        "binrwir_jit_lowering_plan_options_t",
    )
    expected_lowering_fields = (
        "size_t structure_size",
        "uint32_t code_generation_revision",
    )
    if lowering_fields != expected_lowering_fields:
        findings.append(Finding(
            relative_display(lowering_header, repository_root), 1,
            "optional-instruction-boundary",
            f"expected fields {expected_lowering_fields!r}, found {lowering_fields!r}",
        ))

    expected_structs = {
        binrwir_root / "argument.h": (
            "binrwir_argument", "binrwir_argument_t",
            ("uint32_t argument_index",
             "uint8_t reference_kind",
             "uint8_t dereference",
             "uint16_t slot",
             "uint64_t payload"),
            "sizeof(binrwir_argument_t) == 16U",
        ),
        binrwir_root / "instruction.h": (
            "binrwir_instruction", "binrwir_instruction_t",
            ("binrwir_operation_identifier_t operation_identifier",),
            "sizeof(binrwir_instruction_t) == 2U",
        ),
    }
    for path, (tag, alias, expected_fields, size_assertion) in expected_structs.items():
        display = relative_display(path, repository_root)
        if not path.is_file():
            findings.append(Finding(display, 1, "missing-addressed-binrwir-file",
                                    "fixed binary structure is required"))
            continue
        text = path.read_text(encoding="utf-8")
        code = c_code_without_comments_or_literals(text)
        actual_fields = struct_fields(code, tag, alias)
        if actual_fields != expected_fields:
            findings.append(Finding(
                display, 1, "addressed-binrwir-layout",
                f"expected fields {expected_fields!r}, found {actual_fields!r}",
            ))
        if size_assertion not in code:
            findings.append(Finding(display, 1, "addressed-binrwir-size",
                                    size_assertion))

    required_layout_assertions = {
        binrwir_root / "argument.h": (
            "offsetof(binrwir_argument_t, argument_index) == 0U",
            "offsetof(binrwir_argument_t, payload) == 8U",
        ),
        binrwir_root / "instruction.h": (
            "offsetof(binrwir_instruction_t, operation_identifier) == 0U",
        ),
    }
    for path, assertions in required_layout_assertions.items():
        display = relative_display(path, repository_root)
        code = c_code_without_comments_or_literals(
            path.read_text(encoding="utf-8") if path.is_file() else "",
        )
        for assertion in assertions:
            if assertion not in code:
                findings.append(Finding(
                    display, 1, "addressed-binrwir-offset", assertion,
                ))

    argument_source = binrwir_root / "argument.c"
    instruction_source = binrwir_root / "instruction.c"
    required_source_tokens = {
        argument_source: (
            "BINRWIR_REFERENCE_STATIC",
            "memset(output_argument, 0, sizeof(*output_argument))",
        ),
        instruction_source: (
            "binrwir_argument_validate",
        ),
    }
    for path, tokens in required_source_tokens.items():
        display = relative_display(path, repository_root)
        text = path.read_text(encoding="utf-8") if path.is_file() else ""
        text = c_code_without_comments_or_literals(text)
        for token in tokens:
            if token not in text:
                findings.append(Finding(
                    display, 1, "missing-zero-address-rejection", token,
                ))
    for path in (argument_source, instruction_source):
        display = relative_display(path, repository_root)
        text = path.read_text(encoding="utf-8") if path.is_file() else ""
        text = c_code_without_comments_or_literals(text)
        if "body_address" in text:
            findings.append(Finding(
                display, 1, "addressed-binrwir-layout",
                "argument items carry references, not scalar addresses; "
                "`body_address` must not appear",
            ))

    contract_header = runtimec_root.joinpath(*CONTRACT_HEADER)
    contract_source = runtimec_root.joinpath(*CONTRACT_IMPLEMENTATION)
    contract_anchor = contract_header
    host_text = contract_header.read_text(encoding="utf-8") \
        if contract_header.is_file() else ""
    if not host_text:
        findings.append(Finding(
            relative_display(contract_header, repository_root), 1,
            "contract-anchor-missing",
            "the contract header is the single anchor for body-offset "
            "arithmetic; it must exist",
        ))
    offset_definition = BODY_OFFSET_DEFINITION.search(host_text)
    if offset_definition is None or \
            offset_definition.group(1) != "KVSPACE_XVALUE_HEAD_LEN":
        findings.append(Finding(
            relative_display(contract_anchor, repository_root), 1,
            "xvalue-body-address-contract",
            "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES must be defined as "
            "KVSPACE_XVALUE_HEAD_LEN, never as a numeric literal",
        ))
    for token in ("resolve_static_xvalue_address",):
        if token not in host_text:
            findings.append(Finding(
                relative_display(contract_anchor, repository_root), 1,
                "xvalue-body-address-contract", token,
            ))

    code_by_path: dict[Path, str] = {}
    for path in iter_product_sources(runtimec_root):
        try:
            code_by_path[path] = c_code_without_comments_or_literals(
                path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            continue
    contract_implementation = contract_source
    contract_code = code_by_path.get(contract_implementation, "")
    if CONTRACT_BODY_ADDRESS_FUNCTION not in contract_code:
        findings.append(Finding(
            relative_display(contract_implementation, repository_root), 1,
            "contract-body-address-derivation",
            f"the contract must define {CONTRACT_BODY_ADDRESS_FUNCTION[:-1]}",
        ))
    if CONTRACT_BODY_ADDRESS_FUNCTION not in c_code_without_comments_or_literals(
            host_text):
        findings.append(Finding(
            relative_display(contract_anchor, repository_root), 1,
            "contract-body-address-derivation",
            f"the contract must declare {CONTRACT_BODY_ADDRESS_FUNCTION[:-1]}",
        ))
    definition = function_body(contract_code,
                               CONTRACT_BODY_ADDRESS_FUNCTION[:-1])
    if definition is None:
        findings.append(Finding(
            relative_display(contract_implementation, repository_root), 1,
            "contract-body-address-derivation",
            f"{CONTRACT_BODY_ADDRESS_FUNCTION[:-1]} must have a definition body",
        ))
    else:
        body, body_offset = definition
        body_line = contract_code.count("\n", 0, body_offset) + 1
        for token in REQUIRED_BODY_ADDRESS_DERIVATION_TOKENS:
            occurrences = body.count(token)
            if occurrences != 1:
                findings.append(Finding(
                    relative_display(contract_implementation, repository_root),
                    body_line, "contract-body-address-derivation",
                    f"expected exactly one {token!r} inside "
                    f"{CONTRACT_BODY_ADDRESS_FUNCTION[:-1]}, found {occurrences}",
                ))
    for path, code in code_by_path.items():
        if path in (contract_anchor, contract_implementation):
            continue
        if "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES" in code:
            findings.append(Finding(
                relative_display(path, repository_root),
                code.count("\n", 0, code.index(
                    "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES")) + 1,
                "body-address-derivation-outside-contract",
                "the XValue body offset arithmetic belongs to "
                f"{CONTRACT_BODY_ADDRESS_FUNCTION[:-1]} in " + "/".join(CONTRACT_IMPLEMENTATION),
            ))
        for match in HARDCODED_XVALUE_BODY_OFFSET_PATTERN.finditer(code):
            findings.append(Finding(
                relative_display(path, repository_root),
                code.count("\n", 0, match.start()) + 1,
                "hardcoded-body-address-derivation-outside-contract",
                match.group(0),
            ))

    removed_resolver_name = "resolve_static_" + "value_body_address"
    for path in code_by_path:
        text = path.read_text(encoding="utf-8")
        if removed_resolver_name in text:
            findings.append(Finding(
                relative_display(path, repository_root), 1,
                "removed-static-address-resolver", removed_resolver_name,
            ))

    for path, code in code_by_path.items():
        relative = path.relative_to(runtimec_root)
        if relative.parts[:2] == ("binrwir_compiler", "jit"):
            continue
        for rule, pattern in (
            ("jit-storage-outside-jit", JIT_EXCLUSIVE_STORAGE_PATTERN),
            ("removed-value-cache-model", REMOVED_VALUE_CACHE_PATTERN),
        ):
            for match in pattern.finditer(code):
                findings.append(Finding(
                    relative_display(path, repository_root),
                    code.count("\n", 0, match.start()) + 1,
                    rule, match.group(0),
                ))
    return findings

def audit_build_graph(
    runtimec_root: Path, repository_root: Path,
) -> list[Finding]:
    cmake_path = runtimec_root / "CMakeLists.txt"
    display = relative_display(cmake_path, repository_root)
    if not cmake_path.is_file():
        return [Finding(display, 1, "missing-build-file", "CMakeLists.txt is required")]
    text = cmake_path.read_text(encoding="utf-8")
    findings: list[Finding] = []
    actual_order = tuple(COMPONENT_PATTERN.findall(text))
    if actual_order != EXPECTED_COMPONENT_ORDER:
        findings.append(Finding(
            display, 1, "component-order",
            f"expected {EXPECTED_COMPONENT_ORDER!r}, found {actual_order!r}",
        ))
    if not re.search(r"add_library\(\s*runtimec_interface\s+STATIC\b", text):
        findings.append(Finding(
            display, 1, "missing-interface-component",
            "runtimec_interface static component is required",
        ))
    actual_dependencies = cmake_target_dependencies(text)
    for target, expected in EXPECTED_TARGET_DEPENDENCIES.items():
        actual = actual_dependencies.get(target, set())
        if actual != expected:
            findings.append(Finding(
                display, 1, "target-dependencies",
                f"{target} expected {sorted(expected)!r}, found {sorted(actual)!r}",
            ))

    library_kinds = {name: kind
                     for name, kind in LIBRARY_DECLARATION_PATTERN.findall(text)}
    exported_targets = {first or second
                        for first, second in EXPORTED_PRODUCT_PATTERN.findall(text)}
    if not exported_targets:
        findings.append(Finding(
            display, 1, "product-library-not-shared",
            "no target references the exported symbol map; the product must be a "
            "SHARED library exporting interface/exports.txt",
        ))
    for target in sorted(exported_targets):
        kind = library_kinds.get(target)
        if kind != "SHARED":
            findings.append(Finding(
                display, 1, "product-library-not-shared",
                f"{target} exports public symbols but is declared "
                f"{kind or 'with an unresolvable kind'}; it must be SHARED",
            ))

    stale_copy = repository_root / "include" / "kvspace"
    if stale_copy.exists():
        findings.append(Finding(
            relative_display(stale_copy, repository_root), 1,
            "stale-kvspace-header-copy",
            "kvspace headers have one definition point, "
            "kvspace/include/kvspace/; a copy under include/kvspace/ must not exist",
        ))
    return findings

def audit(repository_root: Path, runtimec_root: Path) -> tuple[list[Finding], int]:
    findings = audit_build_graph(runtimec_root, repository_root)
    findings.extend(audit_addressed_binrwir_model(runtimec_root, repository_root))
    scanned = 0
    for source in iter_product_sources(runtimec_root):
        scanned += 1
        relative = PurePosixPath(source.relative_to(runtimec_root).as_posix())
        source_layer = layer_for_relative(relative)
        display = relative_display(source, repository_root)
        if source_layer is None:
            raise SystemExit(
                f"{display}: product source has no layer; add {relative.parts[0]!r} "
                "to KNOWN_LAYERS or stop iterating it")
        if (source_layer == "vmbin" and
                relative.name in BINRWIR_NEUTRAL_FILE_NAMES):
            findings.append(Finding(
                display, 1, "binrwir-owned-by-vmbin",
                "neutral BinRWIR file must live under binrwir",
            ))
        try:
            lines = source.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeError) as error:
            findings.append(Finding(display, 1, "read-error", str(error)))
            continue
        code = c_code_without_comments_or_literals("\n".join(lines))

        included_layers: set[str] = set()
        for line_number, line in enumerate(lines, 1):
            match = INCLUDE_PATTERN.match(line)
            if not match:
                continue
            delimiter, token = match.groups()
            normalized = token.replace("\\", "/")
            if FORBIDDEN_TREE_PATTERN.search(normalized):
                findings.append(Finding(
                    display, line_number, "forbidden-tree-include",
                    f"{source_layer} includes deleted tree {token!r}",
                ))
                continue
            resolved = resolve_include(token, source, runtimec_root, repository_root)
            target_layer = None
            if resolved is not None:
                try:
                    target_relative = PurePosixPath(
                        resolved.relative_to(runtimec_root.resolve()).as_posix()
                    )
                except ValueError:
                    continue
                target_layer = layer_for_relative(target_relative)
            else:
                target_layer = inferred_include_layer(normalized)
                if delimiter == '"':
                    findings.append(Finding(
                        display, line_number, "unresolved-private-include",
                        f"cannot resolve private include {token!r}",
                    ))
            if target_layer is None:
                continue
            included_layers.add(target_layer)
            if target_layer not in ALLOWED_DEPENDENCIES[source_layer]:
                findings.append(Finding(
                    display, line_number, "forbidden-layer-edge",
                    f"{source_layer} -> {target_layer} via {token!r}",
                ))

        for match in PROCESS_ENTRY_PATTERN.finditer(code):
            findings.append(Finding(
                display, code.count("\n", 0, match.start()) + 1,
                "process-entry-in-product-source",
                "runtimec is embedded as a shared library; tests own process entry",
            ))
        for match in PROCESS_GLOBAL_HOOK_PATTERN.finditer(code):
            findings.append(Finding(
                display, code.count("\n", 0, match.start()) + 1,
                "process-global-hook-in-product-source",
                f"{match.group(0).rstrip('(')} owns process-global state; the "
                "embeddable library must not",
            ))

        merged_policy_layers = included_layers & POLICY_LAYERS
        if (source_layer == "engine" and len(merged_policy_layers) > 1 and
                relative.as_posix() not in ENGINE_OWNER_FILES):
            findings.append(Finding(
                display, 1, "policy-merge-outside-owner",
                "only the engine owner graph (execution window, session) may merge "
                "binrwir_compiler and vmbin",
            ))

        if relative.as_posix() not in DISPATCH_OWNER_FILES:
            module = relative.as_posix().rsplit(".", 1)[0]
            for entry_point, owner_module in EXECUTION_ENTRY_POINTS.items():
                if entry_point not in code or module == owner_module:
                    continue
                findings.append(Finding(
                    display,
                    code.count("\n", 0, code.index(entry_point)) + 1,
                    "execution-dispatch-outside-window",
                    f"only engine/execution_window may call {entry_point[:-1]}; "
                    f"its definition lives in {owner_module}",
                ))
            if (EXECUTION_TARGET_TYPE in code and
                    not relative.as_posix().startswith(
                        EXECUTION_TARGET_OWNER + "/")):
                findings.append(Finding(
                    display,
                    code.count("\n", 0, code.index(EXECUTION_TARGET_TYPE)) + 1,
                    "execution-dispatch-outside-window",
                    f"only engine/execution_window and the compiler may reference "
                    f"{EXECUTION_TARGET_TYPE}",
                ))
    return sorted(set(findings)), scanned

SELFTEST_CMAKE = """\
cmake_minimum_required(VERSION 3.20)
project(runtimec_selftest LANGUAGES C)
runtimec_add_component(runtimec_contract contract)
runtimec_add_component(runtimec_binrwir binrwir)
runtimec_add_component(runtimec_vmkv vmkv)
runtimec_add_component(runtimec_binrwir_compiler binrwir_compiler)
runtimec_add_component(runtimec_vmbin vmbin)
runtimec_add_component(runtimec_engine engine)
add_library(runtimec_interface STATIC interface/public_interface.c)
add_library(runtimec_product SHARED interface/public_interface.c)
target_link_options(runtimec_product PRIVATE "LINKER:--version-script=exports.map")
target_link_libraries(runtimec_contract PUBLIC)
target_link_libraries(runtimec_binrwir PUBLIC runtimec_contract)
target_link_libraries(runtimec_vmkv PUBLIC runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_binrwir_compiler PUBLIC runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_vmbin PUBLIC runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_engine PUBLIC runtimec_contract runtimec_binrwir runtimec_vmkv runtimec_binrwir_compiler runtimec_vmbin)
target_link_libraries(runtimec_interface PUBLIC runtimec_contract runtimec_engine)
"""

SELFTEST_ARGUMENT_HEADER = """\
typedef struct binrwir_argument {
    uint32_t argument_index;
    uint8_t reference_kind;
    uint8_t dereference;
    uint16_t slot;
    uint64_t payload;
} binrwir_argument_t;
static_assert(sizeof(binrwir_argument_t) == 16U, "size");
static_assert(offsetof(binrwir_argument_t, argument_index) == 0U, "index");
static_assert(offsetof(binrwir_argument_t, payload) == 8U, "payload");
"""

SELFTEST_INSTRUCTION_HEADER = """\
typedef struct binrwir_instruction {
    binrwir_operation_identifier_t operation_identifier;
} binrwir_instruction_t;
static_assert(sizeof(binrwir_instruction_t) == 2U, "size");
static_assert(offsetof(binrwir_instruction_t, operation_identifier) == 0U, "op");
"""

SELFTEST_SAFEPOINT = """\
#define BINRWIR_JIT_SAFEPOINT_BOUNDARY_INSTRUCTION_COMPLETION 1
#define BINRWIR_JIT_SAFEPOINT_ACTION_COMMIT_COMPLETED_STATE 2
#define BINRWIR_JIT_SAFEPOINT_ACTION_DISCARD_INCOMPLETE_STATE 3
#define BINRWIR_JIT_MATERIALIZATION_FLAG_COMPLETED_DIRTY 4
int binrwir_jit_safepoint_commit_completed_state(void);
int binrwir_jit_safepoint_discard_incomplete_state(void);
"""

SELFTEST_LOWERING_PLAN = """\
typedef struct binrwir_jit_lowering_plan_options_t {
    size_t structure_size;
    uint32_t code_generation_revision;
} binrwir_jit_lowering_plan_options_t;
"""

SELFTEST_CONTRACT_HEADER = """\
#define RUNTIMEC_XVALUE_BODY_OFFSET_BYTES KVSPACE_XVALUE_HEAD_LEN
static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 8U, "offset");
runtimec_value_body_address_t
runtimec_xvalue_body_address(runtimec_xvalue_address_t xvalue_address);
runtimec_status_t
runtimec_kvspace_access_resolve_static_xvalue_address(
    runtimec_kvspace_access_t *access, runtimec_xvalue_address_t *output);
"""

SELFTEST_CONTRACT_SOURCE = """\
runtimec_value_body_address_t
runtimec_xvalue_body_address(runtimec_xvalue_address_t xvalue_address) {
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
        xvalue_address > UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {
        return RUNTIMEC_VALUE_BODY_ADDRESS_NONE;
    }
    return xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}
"""

SELFTEST_EXECUTION_WINDOW = """\
#include "binrwir_compiler/compiler.h"
#include "vmbin/interpreter.h"
static runtimec_status_t window_dispatch(void) {
    binrwir_execution_target_t target = BINRWIR_EXECUTION_TARGET_VMBIN;
    if (target == BINRWIR_EXECUTION_TARGET_VMBIN) {
        return vmbin_execute(0, 0, 0, 0, 0, 0);
    }
    return binrwir_jit_execute(0, 0, 0, 0);
}
"""

SELFTEST_SESSION = """\
#include "binrwir_compiler/compiler.h"
#include "vmbin/interpreter.h"
"""

SELFTEST_VMBIN_INTERPRETER = """\
runtimec_status_t
vmbin_execute(const binrwir_binfunc_t *program, const binrwir_binvthread_frame_t *in,
              binrwir_binvthread_frame_t *out, const binrwir_frame_registry_t *registry,
              binrwir_execution_exit_t *exit,
              runtimec_error_t *error) {
    return RUNTIMEC_STATUS_ERROR;
}
"""

SELFTEST_VMBIN_VALUE_ACCESS = """\
static runtimec_value_body_address_t body_of(runtimec_xvalue_address_t address) {
    return runtimec_xvalue_body_address(address);
}
"""

SELFTEST_JIT_EXECUTOR = """\
runtimec_status_t
binrwir_jit_execute(const void *code, const void *frame,
                    binrwir_execution_exit_t *exit, runtimec_error_t *error) {
    return RUNTIMEC_STATUS_ERROR;
}
"""

SELFTEST_ENCODER = """\
static runtimec_value_body_address_t encode_body(runtimec_xvalue_address_t address) {
    return runtimec_xvalue_body_address(address);
}
"""

SELFTEST_COMPILER_HEADER = """\
typedef enum binrwir_execution_target_t {
    BINRWIR_EXECUTION_TARGET_VMBIN = 0,
    BINRWIR_EXECUTION_TARGET_JIT = 1
} binrwir_execution_target_t;
"""

SELFTEST_CONTRACTS_TEST = """\
_Static_assert(RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION == 4U, "version");
"""

def selftest_base_tree() -> dict[str, str]:
    return {
        "CMakeLists.txt": SELFTEST_CMAKE,
        "interface/public_interface.c": "int runtimec_public_interface(void) { return 0; }\n",
        "contract/kvspace_access.h": SELFTEST_CONTRACT_HEADER,
        "contract/kvspace_access.c": SELFTEST_CONTRACT_SOURCE,
        "binrwir/argument.h": SELFTEST_ARGUMENT_HEADER,
        "binrwir/argument.c": (
            "int binrwir_argument_validate(void) {\n"
            "    if (reference_kind == BINRWIR_REFERENCE_STATIC &&\n"
            "        payload == BINRWIR_BODY_ADDRESS_NONE) {\n"
            "        return 0;\n"
            "    }\n"
            "    memset(output_argument, 0, sizeof(*output_argument));\n"
            "    return 1;\n"
            "}\n"
        ),
        "binrwir/instruction.h": SELFTEST_INSTRUCTION_HEADER,
        "binrwir/instruction.c": (
            "int binrwir_instruction_validate(void) {\n"
            "    return binrwir_argument_validate();\n"
            "}\n"
        ),
        "binrwir_compiler/compiler.h": SELFTEST_COMPILER_HEADER,
        "binrwir_compiler/jit/register_allocation.h": "int binrwir_jit_allocate(void);\n",
        "binrwir_compiler/jit/register_allocation.c": "int binrwir_jit_allocate(void) { return 0; }\n",
        "binrwir_compiler/jit/safepoint.h": SELFTEST_SAFEPOINT,
        "binrwir_compiler/jit/safepoint.c": "int binrwir_jit_safepoint(void) { return 0; }\n",
        "binrwir_compiler/jit/lowering_plan.h": SELFTEST_LOWERING_PLAN,
        "binrwir_compiler/jit/executor.h": (
            "runtimec_status_t binrwir_jit_execute(const void *code, const void *frame,\n"
            "                                      binrwir_execution_exit_t *exit,\n"
            "                                      runtimec_error_t *error);\n"
        ),
        "binrwir_compiler/jit/executor.c": SELFTEST_JIT_EXECUTOR,
        "vmkv/encoder.c": SELFTEST_ENCODER,
        "vmbin/interpreter.h": (
            "runtimec_status_t vmbin_execute(const binrwir_binfunc_t *program,\n"
            "                                const binrwir_binvthread_frame_t *in,\n"
            "                                binrwir_binvthread_frame_t *out,\n"
            "                                const binrwir_frame_registry_t *registry,\n"
            "                                binrwir_execution_exit_t *exit,\n"
            "                                runtimec_error_t *error);\n"
        ),
        "vmbin/interpreter.c": SELFTEST_VMBIN_INTERPRETER,
        "vmbin/value_access.c": SELFTEST_VMBIN_VALUE_ACCESS,
        "engine/execution_window.c": SELFTEST_EXECUTION_WINDOW,
        "engine/session.c": SELFTEST_SESSION,
        "tests/contracts_test.c": SELFTEST_CONTRACTS_TEST,
    }

@dataclass(frozen=True)
class SelftestCase:
    name: str
    expect: tuple[str, ...] = ()
    forbid: tuple[str, ...] = ()
    write: tuple[tuple[str, object], ...] = ()
    delete: tuple[str, ...] = ()

SELFTEST_CASES = (
    SelftestCase("baseline"),
    SelftestCase("build-file-missing", ("missing-build-file",),
                 delete=("CMakeLists.txt",)),
    SelftestCase("component-order-swapped", ("component-order",),
                 write=((("CMakeLists.txt"),
                         SELFTEST_CMAKE.replace(
                             "runtimec_add_component(runtimec_contract contract)\n"
                             "runtimec_add_component(runtimec_binrwir binrwir)\n",
                             "runtimec_add_component(runtimec_binrwir binrwir)\n"
                             "runtimec_add_component(runtimec_contract contract)\n")),)),
    SelftestCase("interface-component-missing", ("missing-interface-component",),
                 write=(("CMakeLists.txt",
                         SELFTEST_CMAKE.replace(
                             "add_library(runtimec_interface STATIC "
                             "interface/public_interface.c)\n", "")),)),
    SelftestCase("target-dependencies-wrong", ("target-dependencies",),
                 write=(("CMakeLists.txt",
                         SELFTEST_CMAKE.replace(
                             "target_link_libraries(runtimec_vmbin PUBLIC "
                             "runtimec_contract runtimec_binrwir)\n",
                             "target_link_libraries(runtimec_vmbin PUBLIC "
                             "runtimec_contract)\n")),)),
    SelftestCase("product-library-static", ("product-library-not-shared",),
                 write=(("CMakeLists.txt",
                         SELFTEST_CMAKE.replace(
                             "add_library(runtimec_product SHARED",
                             "add_library(runtimec_product STATIC")),)),
    SelftestCase("product-library-export-map-missing",
                 ("product-library-not-shared",),
                 write=(("CMakeLists.txt",
                         SELFTEST_CMAKE.replace(
                             'target_link_options(runtimec_product PRIVATE '
                             '"LINKER:--version-script=exports.map")\n', "")),)),
    SelftestCase("process-entry-in-product-source",
                 ("process-entry-in-product-source",),
                 write=(("engine/session.c",
                         SELFTEST_SESSION +
                         "int main(void) { return 0; }\n"),)),
    SelftestCase("process-global-hook-in-product-source",
                 ("process-global-hook-in-product-source",),
                 write=(("vmbin/value_access.c",
                         SELFTEST_VMBIN_VALUE_ACCESS + "int probe(void) { return atexit(0); }\n"),)),
    SelftestCase("process-control-in-tests-allowed",
                 forbid=("process-entry-in-product-source",
                         "process-global-hook-in-product-source"),
                 write=(("tests/contracts_test.c",
                         SELFTEST_CONTRACTS_TEST +
                         "int main(void) { atexit(0); return 0; }\n"),)),
    SelftestCase("binrwir-file-missing", ("missing-addressed-binrwir-file",),
                 delete=("binrwir/argument.h",)),
    SelftestCase("binrwir-layout-wrong", ("addressed-binrwir-layout",),
                 write=(("binrwir/argument.h",
                         SELFTEST_ARGUMENT_HEADER.replace(
                             "    uint16_t slot;\n", "    uint32_t slot;\n")),)),
    SelftestCase("binrwir-size-assertion-missing", ("addressed-binrwir-size",),
                 write=(("binrwir/argument.h",
                         SELFTEST_ARGUMENT_HEADER.replace(
                             'static_assert(sizeof(binrwir_argument_t) == 16U, "size");\n',
                             "")),)),
    SelftestCase("binrwir-offset-assertion-missing", ("addressed-binrwir-offset",),
                 write=(("binrwir/argument.h",
                         SELFTEST_ARGUMENT_HEADER.replace(
                             'static_assert(offsetof(binrwir_argument_t, payload) '
                             '== 8U, "payload");\n', "")),)),
    SelftestCase("zero-address-rejection-missing",
                 ("missing-zero-address-rejection",),
                 write=(("binrwir/argument.c",
                         "int binrwir_argument_validate(void) { return 1; }\n"),)),
    SelftestCase("removed-storage-model-file", ("removed-binrwir-storage-model",),
                 write=(("binrwir/value.h", "int binrwir_value;\n"),)),
    SelftestCase("jit-state-files-missing", ("missing-jit-state-contract",),
                 delete=("binrwir_compiler/jit/safepoint.h",)),
    SelftestCase("jit-commit-discard-missing",
                 ("missing-jit-commit-discard-contract",),
                 write=(("binrwir_compiler/jit/safepoint.h",
                         "int binrwir_jit_safepoint(void);\n"),)),
    SelftestCase("jit-boundary-optional", ("optional-or-ambiguous-jit-boundary",),
                 write=(("binrwir_compiler/jit/safepoint.h",
                         SELFTEST_SAFEPOINT +
                         "#define observe_every_instruction 1\n"),)),
    SelftestCase("jit-lowering-field-extra", ("optional-instruction-boundary",),
                 write=(("binrwir_compiler/jit/lowering_plan.h",
                         SELFTEST_LOWERING_PLAN.replace(
                             "    uint32_t code_generation_revision;\n",
                             "    uint32_t code_generation_revision;\n"
                             "    uint32_t optional_flag;\n")),)),
    SelftestCase("contract-anchor-missing", ("contract-anchor-missing",),
                 delete=("contract/kvspace_access.h",)),
    SelftestCase("xvalue-body-offset-follows-head-length",
                 forbid=("xvalue-body-address-contract",),
                 write=(("contract/kvspace_access.h",
                         SELFTEST_CONTRACT_HEADER.replace(
                             "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 8U",
                             "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 16U")),)),
    SelftestCase("xvalue-body-offset-numeric-literal",
                 ("xvalue-body-address-contract",),
                 write=(("contract/kvspace_access.h",
                         SELFTEST_CONTRACT_HEADER.replace(
                             "#define RUNTIMEC_XVALUE_BODY_OFFSET_BYTES "
                             "KVSPACE_XVALUE_HEAD_LEN",
                             "#define RUNTIMEC_XVALUE_BODY_OFFSET_BYTES 8U")),)),
    SelftestCase("xvalue-body-contract-token-missing",
                 ("xvalue-body-address-contract",),
                 write=(("contract/kvspace_access.h",
                         SELFTEST_CONTRACT_HEADER.replace(
                             "runtimec_status_t\n"
                             "runtimec_kvspace_access_resolve_static_xvalue_address(\n"
                             "    runtimec_kvspace_access_t *access, "
                             "runtimec_xvalue_address_t *output);\n", "")),)),
    SelftestCase("contract-derivation-missing",
                 ("contract-body-address-derivation",),
                 write=(("contract/kvspace_access.c", "int contract_placeholder;\n"),)),
    SelftestCase("contract-derivation-zero-address-removed",
                 ("contract-body-address-derivation",),
                 write=(("contract/kvspace_access.c",
                         SELFTEST_CONTRACT_SOURCE.replace(
                             "    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||\n"
                             "        xvalue_address > UINT64_MAX - "
                             "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {\n",
                             "    if (xvalue_address > UINT64_MAX - "
                             "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) {\n")),)),
    SelftestCase("contract-derivation-comment-decoy",
                 ("contract-body-address-derivation",),
                 write=(("contract/kvspace_access.c",
                         "/* runtimec_xvalue_body_address( { { */\n"
                         "runtimec_value_body_address_t\n"
                         "runtimec_xvalue_body_address(runtimec_xvalue_address_t "
                         "xvalue_address) {\n"
                         "    /* if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) */\n"
                         "    const char *decoy = "
                         "\"xvalue_address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES\";\n"
                         "    (void)decoy;\n"
                         "    return xvalue_address + "
                         "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;\n"
                         "}\n"),)),
    SelftestCase("contract-derivation-decoy-clean",
                 forbid=("contract-body-address-derivation",),
                 write=(("contract/kvspace_access.c",
                         "/* runtimec_xvalue_body_address( { { */\n" +
                         SELFTEST_CONTRACT_SOURCE),)),
    SelftestCase("body-offset-arithmetic-outside-contract",
                 ("body-address-derivation-outside-contract",),
                 write=(("vmbin/value_access.c",
                         "static unsigned long probe(unsigned long address) {\n"
                         "    return address + RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;\n"
                         "}\n"),)),
    SelftestCase("hardcoded-body-offset-outside-contract",
                 ("hardcoded-body-address-derivation-outside-contract",),
                 write=(("vmbin/value_access.c",
                         "static unsigned long probe(unsigned long xvalue_address) {\n"
                         "    return xvalue_address + 8;\n"
                         "}\n"),)),
    SelftestCase("removed-resolver-name", ("removed-static-address-resolver",),
                 write=(("contract/status.c",
                         "int resolve_static_value_body_address(void);\n"),)),
    SelftestCase("stale-kvspace-header-copy", ("stale-kvspace-header-copy",),
                 write=(("../include/kvspace/xvalue_head.h",
                         "#define KVSPACE_XVALUE_HEAD_LEN 16u\n"),)),
    SelftestCase("unreadable-source", ("read-error",),
                 write=(("vmbin/value_access.c", b"\xff\xfe\x00"),)),
    SelftestCase("jit-storage-outside-jit", ("jit-storage-outside-jit",),
                 write=(("vmbin/value_access.c",
                         "struct register_file probe;\n"),)),
    SelftestCase("removed-value-cache-model", ("removed-value-cache-model",),
                 write=(("vmbin/value_access.c", "struct value_cache probe;\n"),)),
    SelftestCase("forbidden-tree-include", ("forbidden-tree-include",),
                 write=(("engine/execution_window.c",
                         '#include "runtime/legacy.h"\n' + SELFTEST_EXECUTION_WINDOW),)),
    SelftestCase("kvspace-include-resolves", forbid=("unresolved-private-include",),
                 write=(("contract/kvspace_access.h",
                         '#include "kvspace/xvalue_head.h"\n' + SELFTEST_CONTRACT_HEADER),
                        ("../../kvspace/include/kvspace/xvalue_head.h",
                         "#define KVSPACE_XVALUE_HEAD_LEN 16u\n"))),
    SelftestCase("unresolved-private-include", ("unresolved-private-include",),
                 write=(("engine/execution_window.c",
                         '#include "not_here/missing.h"\n' + SELFTEST_EXECUTION_WINDOW),)),
    SelftestCase("forbidden-layer-edge", ("forbidden-layer-edge",),
                 write=(("contract/status.h", "int status_layer;\n"),
                        ("contract/status.c",
                         '#include "binrwir/binfunc.h"\n' + "int status_layer;\n"))),
    SelftestCase("binrwir-owned-by-vmbin", ("binrwir-owned-by-vmbin",),
                 write=(("vmbin/value.c", "int binrwir_value_copy;\n"),)),
    SelftestCase("policy-merge-non-owner", ("policy-merge-outside-owner",),
                 write=(("engine/compilation_repository.c",
                         '#include "binrwir_compiler/compiler.h"\n'
                         '#include "vmbin/interpreter.h"\n'),)),
    SelftestCase("policy-merge-owner-allowed", forbid=("policy-merge-outside-owner",),
                 write=(("engine/session.c", SELFTEST_SESSION),)),
    SelftestCase("dispatch-call-outside-window",
                 ("execution-dispatch-outside-window",),
                 write=(("engine/session.c",
                         SELFTEST_SESSION +
                         "static runtimec_status_t probe(void) {\n"
                         "    return vmbin_execute(0, 0, 0, 0, 0, 0);\n"
                         "}\n"),)),
    SelftestCase("dispatch-target-type-outside-window",
                 ("execution-dispatch-outside-window",),
                 write=(("engine/session.c",
                         "static binrwir_execution_target_t probe;\n"),)),
    SelftestCase("dispatch-in-window-allowed",
                 forbid=("execution-dispatch-outside-window",),
                 write=(("engine/execution_window.c", SELFTEST_EXECUTION_WINDOW),)),
)

def run_selftest() -> int:
    failures: list[str] = []
    covered: set[str] = set()
    rule_names = {case_rule for case in SELFTEST_CASES for case_rule in case.expect}
    with tempfile.TemporaryDirectory(prefix="runtimec-layers-selftest-") as directory:
        workspace = Path(directory)
        for case in SELFTEST_CASES:
            root = workspace / "kvlang"
            runtimec_root = root / "runtimec"
            if root.exists():
                shutil.rmtree(root)
            tree = selftest_base_tree()
            for relative in case.delete:
                tree.pop(relative, None)
            tree.update(dict(case.write))
            for relative, content in tree.items():
                path = (runtimec_root / relative).resolve()
                path.parent.mkdir(parents=True, exist_ok=True)
                if isinstance(content, bytes):
                    path.write_bytes(content)
                else:
                    path.write_text(content, encoding="utf-8")
            findings, _ = audit(root, runtimec_root)
            rules = {finding.rule for finding in findings}
            if case.name == "baseline" and rules:
                failures.append(f"{case.name}: a clean tree reported {sorted(rules)}")
            missing = set(case.expect) - rules
            if missing:
                failures.append(
                    f"{case.name}: expected {sorted(missing)} but the rule did not fire")
            leaked = set(case.forbid) & rules
            if leaked:
                failures.append(
                    f"{case.name}: expected no {sorted(leaked)} but the rule fired")
            covered |= set(case.expect)
    with tempfile.TemporaryDirectory(prefix="runtimec-layers-empty-") as empty:
        with contextlib.redirect_stdout(io.StringIO()):
            status = main(["--source-root", empty, "--quiet"])
    if status != 2:
        failures.append(
            f"missing-runtimec: expected exit status 2 for an empty tree, got {status}")
    for finding in failures:
        print(f"[selftest] {finding}")
    uncovered = sorted(set(AUDIT_RULES) - covered)
    if uncovered:
        print(f"[selftest] fixtures never expected these rules: {uncovered}")
    if failures or uncovered:
        print(f"check_layers selftest: FAIL ({len(failures)} failure(s), "
              f"{len(covered)} rule(s) covered)")
        return 1
    print(f"check_layers selftest: OK ({len(SELFTEST_CASES)} fixtures, "
          f"{len(covered)} rule(s) covered)")
    return 0

def main(command_arguments: list[str] | None = None) -> int:
    arguments = parse_arguments(command_arguments or sys.argv[1:])
    if arguments.selftest:
        return run_selftest()
    source_root = arguments.source_root
    if source_root is None:
        script = Path(__file__).resolve()
        source_root = script.parents[3] if len(script.parents) > 3 else Path.cwd()
    repository_root, runtimec_root = locate_runtimec(source_root)
    if not runtimec_root.is_dir():
        print(f"{runtimec_root}:1: [missing-runtimec] runtimec source directory does not exist")
        return 2
    findings, scanned = audit(repository_root, runtimec_root)
    for finding in findings:
        print(finding.render())
    if findings:
        print(f"check_layers: FAIL ({len(findings)} finding(s), {scanned} file(s) scanned)")
        return 1
    if not arguments.quiet:
        print(f"check_layers: OK ({scanned} file(s) scanned)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
