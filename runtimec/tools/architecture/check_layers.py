#!/usr/bin/env python3
"""Enforce the single runtimec dependency trunk."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


SOURCE_SUFFIXES = {".c", ".h"}
BINRWIR_CONTRACT_FILES = {
    "value.h", "reference.h", "signature.h", "instruction.h", "program.h",
    "program.c", "binding.h", "frame.h", "frame.c", "exit.h",
}
PRODUCT_DIRECTORIES = (
    "include", "interface", "contract", "host", "vmbin", "vmkv",
    "vmjit", "engine", "generated",
)
KNOWN_LAYERS = {
    "public", "contract", "host", "binrwir", "vmbin", "vmkv", "vmjit",
    "engine", "interface",
}
ALLOWED_DEPENDENCIES = {
    "public": {"public"},
    "contract": {"public", "contract"},
    "host": {"contract", "host"},
    "binrwir": {"contract", "binrwir"},
    "vmbin": {"contract", "binrwir", "vmbin"},
    "vmkv": {"contract", "binrwir", "vmkv"},
    "vmjit": {"contract", "binrwir", "vmjit"},
    "engine": {
        "contract", "host", "binrwir", "vmbin", "vmkv", "vmjit", "engine",
    },
    "interface": {"public", "contract", "engine", "interface"},
}
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')
FORBIDDEN_TREE_PATTERN = re.compile(r"(?:^|/)(?:runtime|runtimec2)(?:/|$)")
INTERNAL_PREFIXES = {
    "contract", "host", "vmbin", "vmkv", "vmjit", "engine", "interface",
}
EXECUTION_LAYERS = {"vmkv", "vmbin", "vmjit"}


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
    parts = relative.parts
    if not parts:
        return None
    if parts[0] == "include":
        return "public"
    if parts[0] == "interface":
        return "interface"
    if parts[0] == "generated":
        return "contract"
    if parts[0] == "vmbin" and len(parts) >= 2:
        if parts[1] == "binrwir" or parts[-1] in BINRWIR_CONTRACT_FILES:
            return "binrwir"
    return parts[0] if parts[0] in KNOWN_LAYERS else None


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
    if parts[0] == "vmbin" and len(parts) > 1:
        if parts[1] == "binrwir" or parts[-1] in BINRWIR_CONTRACT_FILES:
            return "binrwir"
    return parts[0] if parts[0] in INTERNAL_PREFIXES else None


def resolve_include(
    token: str, source: Path, runtimec_root: Path, repository_root: Path,
) -> Path | None:
    candidates = [
        source.parent / token, runtimec_root / token,
        runtimec_root / "include" / token, runtimec_root / "contract" / token,
        runtimec_root / "host" / token, runtimec_root / "vmbin" / token,
        runtimec_root / "vmkv" / token, runtimec_root / "vmjit" / token,
        runtimec_root / "engine" / token, runtimec_root / "interface" / token,
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


def audit(repository_root: Path, runtimec_root: Path) -> tuple[list[Finding], int]:
    findings: list[Finding] = []
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
        try:
            lines = source.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeError) as error:
            findings.append(Finding(display, 1, "read-error", str(error)))
            continue

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
            if target_layer not in ALLOWED_DEPENDENCIES[source_layer]:
                findings.append(Finding(
                    display, line_number, "forbidden-layer-edge",
                    f"{source_layer} -> {target_layer} via {token!r}",
                ))
            if (source_layer == "engine" and target_layer in EXECUTION_LAYERS
                    and relative.as_posix() not in {"engine/route.c", "engine/route.h"}):
                findings.append(Finding(
                    display, line_number, "route-bypass",
                    f"only engine/route may depend on {target_layer}",
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
