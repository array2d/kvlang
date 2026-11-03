#!/usr/bin/env python3
"""Enforce the runtimec component graph and its single execution-policy merge."""

from __future__ import annotations

import argparse
import re
import sys
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
    ("runtimec_host", "host"),
    ("runtimec_binrwir", "binrwir"),
    ("runtimec_vmkv", "vmkv"),
    ("runtimec_binrwir_compiler", "binrwir_compiler"),
    ("runtimec_vmbin", "vmbin"),
    ("runtimec_engine", "engine"),
)
EXPECTED_TARGET_DEPENDENCIES = {
    "runtimec_contract": set(),
    "runtimec_host": {"runtimec_contract"},
    "runtimec_binrwir": {"runtimec_contract"},
    "runtimec_vmkv": {"runtimec_contract", "runtimec_binrwir"},
    "runtimec_binrwir_compiler": {"runtimec_contract", "runtimec_binrwir"},
    "runtimec_vmbin": {"runtimec_contract", "runtimec_binrwir"},
    "runtimec_engine": {
        "runtimec_contract", "runtimec_host", "runtimec_binrwir",
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
ROUTE_FILES = {"engine/route.c", "engine/route.h"}
# 所有权不等于分派权：session 是 owner graph 的根，必须持有 host services、vmkv encoder、
# BinRWIR Compiler 与 compilation repository，所以它引入两个执行层是职责，不是绕过。
# 真正该防的「谁在选择执行器」由 DISPATCH_OWNER_FILES 那条直接守。
ENGINE_OWNER_FILES = ROUTE_FILES | {"engine/session.c", "engine/session.h"}
DISPATCH_OWNER_FILES = ROUTE_FILES
# 每个执行入口的「定义 + 声明」所在模块（不含后缀）；调用只允许 route。
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
# 只有 JIT 拥有的存储形态：寄存器文件、寄存器分配、寄存器描述符、spill 槽。
# 裸 `register` 不算——`register_parameters`（登记参数）是普通动词。
JIT_EXCLUSIVE_STORAGE_PATTERN = re.compile(
    r"(?:register[ _]?file|register[ _]?allocation|register[ _]?allocator|"
    r"register[ _]?desc(?:riptor)?|virtual[ _]?register|"
    r"spill[ _]?slot|spill[ _]?state|spilled?[ _]?register)",
    re.IGNORECASE,
)
# 二进制接口版本不写死具体数字：头文件定义与契约测试的 pin 必须一致。
BINARY_INTERFACE_VERSION_DEFINITION = re.compile(
    r"#define\s+RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION\s+(\d+)U\b"
)
BINARY_INTERFACE_VERSION_PIN = re.compile(
    r"RUNTIMEC_HOST_SERVICES_BINARY_INTERFACE_VERSION\s*==\s*(\d+)U\b"
)
# 裁决 decision-vmbin-data-path §2：+8 只有一处实现，在合同里。
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
HARDCODED_XVALUE_BODY_OFFSET_PATTERN = re.compile(
    r"\b(?:xvalue|resolved)[A-Za-z0-9_]*address\b\s*\+\s*"
    r"(?:8U?\b|UINT64_C\s*\(\s*8\s*\))",
    re.IGNORECASE,
)


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str
    detail: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.detail}"


def parse_arguments(command_arguments: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root", type=Path,
        default=Path(__file__).resolve().parents[3],
        help="repository source root, or the runtimec directory itself",
    )
    parser.add_argument("--quiet", action="store_true", help="suppress success output")
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
        repository_root / token,
        repository_root / "include" / token,
    ]
    return next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)


def function_body(code: str, name: str) -> tuple[str, int] | None:
    """Return the brace-matched body of the definition `name`, and its offset.

    A prototype (`..._;`) before the first `{` means there is no body. Comments and
    literals are masked first so a brace or a decoy call inside them can neither skew the
    brace matching nor satisfy a token count; masking an already masked text is a no-op.
    """
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
             "uint8_t reserved_8_bits",
             "uint16_t slot",
             "uint64_t payload"),
            "sizeof(binrwir_argument_t) == 16U",
        ),
        binrwir_root / "instruction.h": (
            "binrwir_instruction", "binrwir_instruction_t",
            ("uint32_t operation_identifier",),
            "sizeof(binrwir_instruction_t) == 4U",
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

    # 裁决 A.1：参数是引用，不是地址。唯一携带地址的是 STATIC，其 payload
    # 必须非零；零地址只能在 STATIC 分支被拒绝（其余 kind 的 payload 不是地址）。
    argument_source = binrwir_root / "argument.c"
    instruction_source = binrwir_root / "instruction.c"
    required_source_tokens = {
        argument_source: (
            "BINRWIR_REFERENCE_STATIC",
            "payload == BINRWIR_BODY_ADDRESS_NONE",
            "memset(output_argument, 0, sizeof(*output_argument))",
        ),
        # instruction 不直接判地址，它把每个参数交给 argument_validate；
        # 这里只要求它确实走了引用校验，不允许自己比较地址字段。
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

    host_services = runtimec_root / "contract" / "host_services.h"
    host_text = host_services.read_text(encoding="utf-8") \
        if host_services.is_file() else ""
    version = BINARY_INTERFACE_VERSION_DEFINITION.search(host_text)
    if version is None:
        findings.append(Finding(
            relative_display(host_services, repository_root), 1,
            "host-services-version",
            "an explicit unsigned binary interface version is required",
        ))
    else:
        pins = {}
        tests_directory = runtimec_root / "tests"
        for path in sorted(tests_directory.glob("*.c")):
            for match in BINARY_INTERFACE_VERSION_PIN.finditer(
                    c_code_without_comments_or_literals(
                        path.read_text(encoding="utf-8"))):
                pins.setdefault(match.group(1), []).append(
                    relative_display(path, repository_root))
        if not pins:
            findings.append(Finding(
                relative_display(host_services, repository_root), 1,
                "host-services-version-pin",
                "a contract test must pin the declared binary interface version",
            ))
        for pinned, places in sorted(pins.items()):
            if pinned != version.group(1):
                findings.append(Finding(
                    places[0], 1, "host-services-version-pin",
                    f"contract test pins version {pinned} while the header "
                    f"declares {version.group(1)}",
                ))
    for token in (
        "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES KVSPACE_XVALUE_HEAD_LEN",
        "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 8U",
        "resolve_static_xvalue_address",
    ):
        if token not in host_text:
            findings.append(Finding(
                relative_display(host_services, repository_root), 1,
                "xvalue-body-address-contract", token,
            ))

    code_by_path = {
        path: c_code_without_comments_or_literals(path.read_text(encoding="utf-8"))
        for path in iter_product_sources(runtimec_root)
    }
    # 裁决 decision-vmbin-data-path §2：`+8` 的唯一实现在合同里，vmkv 与 vmbin 都调用
    # `runtimec_xvalue_body_address`。判据按宏的出现位置：任何产品源只要自己写偏移
    # 算术就是违规，调用函数不算——调用式里根本不出现这个宏。
    contract_implementation = runtimec_root / "contract" / "host_services.c"
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
            relative_display(host_services, repository_root), 1,
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
        if path in (host_services, contract_implementation):
            continue
        if "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES" in code:
            findings.append(Finding(
                relative_display(path, repository_root),
                code.count("\n", 0, code.index(
                    "RUNTIMEC_XVALUE_BODY_OFFSET_BYTES")) + 1,
                "body-address-derivation-outside-contract",
                "the XValue body offset arithmetic belongs to "
                f"{CONTRACT_BODY_ADDRESS_FUNCTION[:-1]} in contract/host_services.c",
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
            findings.append(Finding(display, 1, "unknown-source-layer",
                                    "product source has no owner"))
            continue
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

        merged_policy_layers = included_layers & POLICY_LAYERS
        if (source_layer == "engine" and len(merged_policy_layers) > 1 and
                relative.as_posix() not in ENGINE_OWNER_FILES):
            findings.append(Finding(
                display, 1, "route-bypass",
                "only the engine owner graph (route, session) may merge "
                "binrwir_compiler and vmbin",
            ))

        # 直接守分派，而不是拿 include 计数当代理：只有 route 能调用执行入口，
        # 也只有 route 能在编译器之外引用执行目标类型。
        if relative.as_posix() not in DISPATCH_OWNER_FILES:
            module = relative.as_posix().rsplit(".", 1)[0]
            for entry_point, owner_module in EXECUTION_ENTRY_POINTS.items():
                if entry_point not in code or module == owner_module:
                    continue
                findings.append(Finding(
                    display,
                    code.count("\n", 0, code.index(entry_point)) + 1,
                    "execution-dispatch-outside-route",
                    f"only engine/route may call {entry_point[:-1]}; "
                    f"its definition lives in {owner_module}",
                ))
            if (EXECUTION_TARGET_TYPE in code and
                    not relative.as_posix().startswith(
                        EXECUTION_TARGET_OWNER + "/")):
                findings.append(Finding(
                    display,
                    code.count("\n", 0, code.index(EXECUTION_TARGET_TYPE)) + 1,
                    "execution-dispatch-outside-route",
                    f"only engine/route and the compiler may reference "
                    f"{EXECUTION_TARGET_TYPE}",
                ))
    return sorted(set(findings)), scanned


def main(command_arguments: list[str] | None = None) -> int:
    arguments = parse_arguments(command_arguments or sys.argv[1:])
    repository_root, runtimec_root = locate_runtimec(arguments.source_root)
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
