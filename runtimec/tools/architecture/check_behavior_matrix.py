#!/usr/bin/env python3
"""Require five-language decisions before runtimec language semantics."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


EXPECTED_COLUMNS = (
    "Decision", "Behavior", "C", "Python", "Rust", "Go", "V8/TypeScript",
    "Agreement", "kvlang decision", "Boundary fixture", "Invalid input",
    "Design record",
)
DECISION_PATTERN = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
MARKER_PATTERN = re.compile(r"behavior-decision:\s*([a-z][a-z0-9-]*)")
MANIFEST_ENTRY_PATTERN = re.compile(
    r"^\s*RUNTIMEC_[A-Z0-9_]*(?:OP|OPERATION)\s*\("
)
PLACEHOLDER_PATTERN = re.compile(r"\b(?:todo|tbd|unknown|placeholder)\b", re.IGNORECASE)
SCANNED_PRODUCT_DIRECTORIES = (
    "specification", "binrwir", "vmkv", "compiler", "vmbin", "vmjit",
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


def display(path: Path, repository_root: Path) -> str:
    try:
        return path.resolve().relative_to(repository_root).as_posix()
    except ValueError:
        return path.as_posix()


def table_cells(line: str) -> tuple[str, ...]:
    return tuple(cell.strip() for cell in line.strip().strip("|").split("|"))


def read_decisions(
    matrix_path: Path, repository_root: Path,
) -> tuple[set[str], list[Finding]]:
    findings: list[Finding] = []
    matrix_display = display(matrix_path, repository_root)
    if not matrix_path.is_file():
        return set(), [Finding(matrix_display, 1, "missing-matrix", "matrix is required")]
    lines = matrix_path.read_text(encoding="utf-8").splitlines()
    if "<!-- runtimec-behavior-matrix-version: 1 -->" not in lines:
        findings.append(Finding(matrix_display, 1, "missing-version", "version 1 marker is required"))
    header_index = next(
        (index for index, line in enumerate(lines) if table_cells(line) == EXPECTED_COLUMNS), None,
    )
    if header_index is None:
        return set(), findings + [Finding(
            matrix_display, 1, "invalid-header", "required twelve-column table is missing",
        )]

    decisions: set[str] = set()
    for index in range(header_index + 2, len(lines)):
        line = lines[index]
        if not line.startswith("|"):
            break
        cells = table_cells(line)
        line_number = index + 1
        if len(cells) != len(EXPECTED_COLUMNS):
            findings.append(Finding(matrix_display, line_number, "column-count",
                                    f"expected {len(EXPECTED_COLUMNS)}, found {len(cells)}"))
            continue
        decision = cells[0]
        if not DECISION_PATTERN.fullmatch(decision):
            findings.append(Finding(matrix_display, line_number, "invalid-decision", decision))
        elif decision in decisions:
            findings.append(Finding(matrix_display, line_number, "duplicate-decision", decision))
        else:
            decisions.add(decision)
        for column, cell in zip(EXPECTED_COLUMNS, cells):
            if not cell or PLACEHOLDER_PATTERN.search(cell):
                findings.append(Finding(matrix_display, line_number, "incomplete-cell", column))
        if cells[7] not in {"consensus", "split"}:
            findings.append(Finding(matrix_display, line_number, "invalid-agreement", cells[7]))
    if not decisions:
        findings.append(Finding(matrix_display, header_index + 1, "empty-matrix",
                                "at least one decision is required"))
    return decisions, findings


def iter_product_files(runtimec_root: Path):
    for directory_name in SCANNED_PRODUCT_DIRECTORIES:
        directory = runtimec_root / directory_name
        if directory.is_dir():
            for path in sorted(directory.rglob("*")):
                if path.is_file() and path.suffix in {".c", ".h", ".def"}:
                    yield path


def audit_markers(
    runtimec_root: Path, repository_root: Path, decisions: set[str],
) -> tuple[list[Finding], int]:
    findings: list[Finding] = []
    scanned = 0
    for path in iter_product_files(runtimec_root):
        scanned += 1
        lines = path.read_text(encoding="utf-8").splitlines()
        path_display = display(path, repository_root)
        for index, line in enumerate(lines):
            for match in MARKER_PATTERN.finditer(line):
                if match.group(1) not in decisions:
                    findings.append(Finding(path_display, index + 1, "unknown-decision",
                                            match.group(1)))
            if (path.suffix == ".def" and MANIFEST_ENTRY_PATTERN.search(line)
                    and "INVALID" not in line):
                context = "\n".join(lines[max(0, index - 2):index + 1])
                if MARKER_PATTERN.search(context) is None:
                    findings.append(Finding(
                        path_display, index + 1, "manifest-before-matrix",
                        "add a behavior-decision marker before this entry",
                    ))
    return findings, scanned


def main(command_arguments: list[str] | None = None) -> int:
    arguments = parse_arguments(command_arguments or sys.argv[1:])
    repository_root, runtimec_root = locate_runtimec(arguments.source_root)
    matrix_path = runtimec_root / "specification" / \
        "five-language-behavior-matrix.md"
    decisions, findings = read_decisions(matrix_path, repository_root)
    marker_findings, scanned = audit_markers(runtimec_root, repository_root, decisions)
    findings.extend(marker_findings)
    for finding in sorted(set(findings)):
        print(finding.render())
    if findings:
        print(f"check_behavior_matrix: FAIL ({len(set(findings))} finding(s))")
        return 1
    if not arguments.quiet:
        print(f"check_behavior_matrix: OK ({len(decisions)} decisions, {scanned} files scanned)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
