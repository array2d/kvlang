#!/usr/bin/env python3
"""Enforce the runtimec vocabulary and reject alternate architecture paths."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


PRODUCT_DIRECTORIES = (
    "include", "interface", "contract", "host", "vmbin", "vmkv",
    "vmjit", "engine", "generated", "specification",
)
TEXT_SUFFIXES = {".c", ".h", ".cmake", ".in", ".map", ".exports", ".def"}
FORBIDDEN_PATH_PARTS = {
    "abi": "execution_interface", "api": "interface", "compat": "delete",
    "compatibility": "delete", "docs": "documentation", "legacy": "delete",
    "spec": "specification",
}
FORBIDDEN_NAMES = (
    ("old-binir-name", re.compile(r"\bbinir\b", re.IGNORECASE)),
    ("old-binir-identifier", re.compile(r"\bbinir_[A-Za-z0-9_]*\b")),
    ("old-runtimec2-name", re.compile(r"\bruntimec2\b")),
    ("old-runtime-tree", re.compile(r"(?:^|[\"<])runtime/")),
    ("old-success-name", re.compile(r"\bRUNTIMEC_STATUS_OK\b")),
    ("compatibility-language", re.compile(
        r"\b(?:backward|compat|compatibility|legacy|forwarding)[ _-]?(?:header|layer|path|support)?\b",
        re.IGNORECASE,
    )),
    ("third-fast-kernel", re.compile(r"\bfast[ _-]?kernel\b", re.IGNORECASE)),
    ("third-executor", re.compile(
        r"\b(?:fast|quick|direct|native|optimized)[ _-]?(?:executor|interpreter|execute|runner)\b",
        re.IGNORECASE,
    )),
    ("old-fast-operation", re.compile(r"\bfast[ _-]?op(?:eration)?\b", re.IGNORECASE)),
    ("alternate-execution-window", re.compile(r"\bexecute[ _-]?window\b", re.IGNORECASE)),
    ("old-vmkv-type", re.compile(r"\bVmkv\b")),
    ("old-vmbin-type", re.compile(r"\bVmBin\b")),
    ("ambiguous-segment-type", re.compile(r"\b(?:Segment|CodeSeg|CInsn)\b")),
    ("old-native-context", re.compile(r"\b(?:NativeCtx|NativeFn)\b")),
    ("old-frame-type", re.compile(r"\b(?:FrameCtx|RunResult)\b")),
    ("old-runtimec2-macro", re.compile(r"\b(?:RC2|KV2)_[A-Za-z0-9_]*\b")),
    ("old-runtimec2-test-name", re.compile(r"\bc2_[A-Za-z0-9_]*\b")),
    ("old-codeseg-name", re.compile(r"\bcodeseg\b", re.IGNORECASE)),
)
ABBREVIATIONS = {
    "abi": "binary_interface or interface", "api": "interface",
    "arg": "argument", "buf": "buffer", "cfg": "control_flow_graph",
    "ctx": "context", "desc": "descriptor", "dst": "destination",
    "fn": "function", "id": "identifier", "impl": "implementation",
    "len": "length", "op": "operation", "pc": "program_counter",
    "ptr": "pointer", "ref": "reference", "req": "request",
    "src": "source", "tmp": "temporary",
}
ABBREVIATION_PATTERNS = tuple(
    (abbreviation, replacement, re.compile(
        rf"(?<![A-Za-z0-9]){re.escape(abbreviation)}(?![A-Za-z0-9])",
        re.IGNORECASE,
    ))
    for abbreviation, replacement in ABBREVIATIONS.items()
)
C_NON_CODE_PATTERN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
VOID_LIFECYCLE_PATTERN = re.compile(
    r"\bvoid\s+([A-Za-z_][A-Za-z0-9_]*(?:destroy|close)[A-Za-z0-9_]*)"
    r"\s*\([^;{}]*\)\s*(?:;|\{)",
    re.IGNORECASE | re.DOTALL,
)


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str
    token: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] forbidden name {self.token!r}"


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


def iter_product_files(runtimec_root: Path):
    cmake_lists = runtimec_root / "CMakeLists.txt"
    if cmake_lists.is_file():
        yield cmake_lists
    for directory_name in PRODUCT_DIRECTORIES:
        directory = runtimec_root / directory_name
        if directory.is_dir():
            for path in sorted(directory.rglob("*")):
                if path.is_file() and (
                    path.suffix in TEXT_SUFFIXES or path.name == "CMakeLists.txt"
                ):
                    yield path


def audit_public_headers(runtimec_root: Path, repository_root: Path) -> list[Finding]:
    include_root = runtimec_root / "include"
    allowed_header = include_root / "kvlang" / "runtimec.h"
    return [
        Finding(relative_display(header, repository_root), 1,
                "noncanonical-public-header",
                header.relative_to(include_root).as_posix())
        for header in sorted(include_root.rglob("*.h"))
        if header != allowed_header
    ] if include_root.is_dir() else []


def c_code_without_comments_or_literals(text: str) -> str:
    return C_NON_CODE_PATTERN.sub(
        lambda match: re.sub(r"[^\n]", " ", match.group(0)), text,
    )


def audit(repository_root: Path, runtimec_root: Path) -> tuple[list[Finding], int]:
    findings = audit_public_headers(runtimec_root, repository_root)
    scanned = 0
    for forbidden_directory in ("api", "compat", "docs", "spec"):
        path = runtimec_root / forbidden_directory
        if path.exists():
            findings.append(Finding(relative_display(path, repository_root), 1,
                                    "forbidden-directory", forbidden_directory))

    for path in iter_product_files(runtimec_root):
        scanned += 1
        display = relative_display(path, repository_root)
        relative_path = path.relative_to(runtimec_root)
        for part in relative_path.parts:
            replacement = FORBIDDEN_PATH_PARTS.get(part.lower())
            if replacement is not None:
                findings.append(Finding(display, 1, "forbidden-path-name",
                                        f"{part} -> {replacement}"))
        for rule, pattern in FORBIDDEN_NAMES:
            match = pattern.search(relative_path.as_posix())
            if match:
                findings.append(Finding(display, 1, f"{rule}-in-path", match.group(0)))
        try:
            text = path.read_text(encoding="utf-8")
            lines = text.splitlines()
        except (OSError, UnicodeError) as error:
            findings.append(Finding(display, 1, "read-error", str(error)))
            continue
        if path.suffix in {".c", ".h"}:
            code = c_code_without_comments_or_literals(text)
            for match in VOID_LIFECYCLE_PATTERN.finditer(code):
                findings.append(Finding(
                    display,
                    code.count("\n", 0, match.start()) + 1,
                    "void-lifecycle-function",
                    match.group(1),
                ))
        for line_number, line in enumerate(lines, 1):
            for rule, pattern in FORBIDDEN_NAMES:
                for match in pattern.finditer(line):
                    findings.append(Finding(display, line_number, rule, match.group(0)))
            if path.name == "CMakeLists.txt":
                continue
            for abbreviation, replacement, pattern in ABBREVIATION_PATTERNS:
                for match in pattern.finditer(line):
                    findings.append(Finding(
                        display, line_number,
                        f"abbreviation-{abbreviation}-use-{replacement}", match.group(0),
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
        print(f"check_names: FAIL ({len(findings)} finding(s), {scanned} file(s) scanned)")
        return 1
    if not arguments.quiet:
        print(f"check_names: OK ({scanned} file(s) scanned)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
