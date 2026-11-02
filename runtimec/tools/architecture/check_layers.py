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
BINRWIR_NEUTRAL_FILE_NAMES = {
    "value.c", "value.h", "reference.c", "reference.h", "signature.c",
    "signature.h", "instruction.c", "instruction.h", "program.c",
    "program.h", "execution_plan.c", "execution_plan.h",
}


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
    ]
    return next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)


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
                relative.as_posix() not in ROUTE_FILES):
            findings.append(Finding(
                display, 1, "route-bypass",
                "only engine/route may merge binrwir_compiler and vmbin",
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
