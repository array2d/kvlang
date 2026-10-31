#!/usr/bin/env python3
"""Enforce the runtimec2 vmkv -> vmbin -> vmjit naming trunk."""

from __future__ import annotations

import argparse
import bisect
import fnmatch
import hashlib
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml


SCHEMA = "kvlang.runtimec2.trunk-rename.v1"
CANONICAL_TRUNK = ("vmkv", "vmbin", "vmjit")
REQUIRED_RENAME_GROUPS = (
    "paths",
    "namespaces",
    "types",
    "functions",
    "config",
    "targets",
    "ctests",
    "tools",
)


class GuardError(RuntimeError):
    pass


@dataclass(frozen=True)
class Finding:
    rule: str
    path: str
    line: int
    token: str
    message: str
    expires_at_phase: int | None = None
    forced_violation: bool = False


@dataclass(frozen=True)
class ScanStats:
    scanned_files: int
    excluded_files: int


@dataclass(frozen=True)
class ScanReport:
    phase: int
    debt: tuple[Finding, ...]
    violations: tuple[Finding, ...]
    stats: ScanStats

    @property
    def debt_counts(self) -> Counter[str]:
        return Counter(finding.rule for finding in self.debt)

    @property
    def violation_counts(self) -> Counter[str]:
        return Counter(finding.rule for finding in self.violations)


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise GuardError(f"cannot read manifest {path}: {exc}") from exc
    except yaml.YAMLError as exc:
        raise GuardError(f"invalid YAML in {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise GuardError("manifest root must be a mapping")
    validate_manifest(data)
    return data


def validate_manifest(manifest: dict[str, Any]) -> None:
    if manifest.get("schema") != SCHEMA:
        raise GuardError(f"unsupported manifest schema: {manifest.get('schema')!r}")
    if tuple(manifest.get("canonical_trunk", ())) != CANONICAL_TRUNK:
        raise GuardError("canonical_trunk must be exactly [vmkv, vmbin, vmjit]")
    phases = manifest.get("phases")
    if phases != list(range(6)):
        raise GuardError("phases must be exactly [0, 1, 2, 3, 4, 5]")

    renames = manifest.get("renames")
    if not isinstance(renames, dict):
        raise GuardError("renames must be a mapping")
    for group in REQUIRED_RENAME_GROUPS:
        entries = renames.get(group)
        if not isinstance(entries, list) or not entries:
            raise GuardError(f"renames.{group} must be a non-empty list")
        for entry in entries:
            if not isinstance(entry, dict) or "from" not in entry or "to" not in entry:
                raise GuardError(f"renames.{group} entry must contain from/to")

    path_sources = [entry["from"] for entry in renames["paths"]]
    if len(path_sources) != len(set(path_sources)):
        raise GuardError("renames.paths contains duplicate source paths")
    for entry in renames["paths"]:
        expiry = entry.get("expires_at_phase")
        if not isinstance(expiry, int) or not 1 <= expiry <= 6:
            raise GuardError(f"invalid path expiry for {entry['from']!r}")

    new_paths = manifest.get("new_paths", [])
    if not isinstance(new_paths, list):
        raise GuardError("new_paths must be a list")
    new_path_sources = []
    for entry in new_paths:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise GuardError("new_paths entry must contain a path")
        new_path_sources.append(entry["path"])
        if "renamed_from" in entry:
            if entry["renamed_from"] not in path_sources:
                raise GuardError(
                    f"new_paths {entry['path']!r} renamed_from is not a renames.paths source"
                )
        elif entry.get("predecessor") != "none":
            raise GuardError(
                f"new_paths {entry['path']!r} needs renamed_from or predecessor: none"
            )
    if len(new_path_sources) != len(set(new_path_sources)):
        raise GuardError("new_paths contains duplicate paths")
    if set(new_path_sources) & set(path_sources):
        raise GuardError("new_paths overlaps renames.paths sources")

    rules = manifest.get("rules")
    if not isinstance(rules, list) or not rules:
        raise GuardError("rules must be a non-empty list")
    rule_ids = [rule.get("id") for rule in rules if isinstance(rule, dict)]
    if len(rule_ids) != len(rules) or len(rule_ids) != len(set(rule_ids)):
        raise GuardError("rules must have unique non-empty ids")
    for rule in rules:
        kind = rule.get("kind")
        if kind == "regex":
            try:
                re.compile(rule["pattern"])
            except (KeyError, re.error) as exc:
                raise GuardError(f"invalid regex rule {rule.get('id')}: {exc}") from exc
        elif kind == "root_namespace_type":
            if not rule.get("namespace"):
                raise GuardError(f"root namespace rule {rule.get('id')} lacks namespace")
        else:
            raise GuardError(f"unknown rule kind {kind!r}")

    symbols = manifest.get("permanent_allowlist", {}).get("c_symbols", [])
    if len(symbols) != 16 or len(symbols) != len(set(symbols)):
        raise GuardError("permanent C symbol allowlist must contain exactly 16 unique names")
    if any(not str(symbol).startswith("kvlang") for symbol in symbols):
        raise GuardError("permanent C symbol allowlist contains a non-kvlang name")

    known_rules = set(rule_ids)
    for section in ("baseline_debt", "compatibility_allowances"):
        entries = manifest.get(section, [])
        if not isinstance(entries, list):
            raise GuardError(f"{section} must be a list")
        for entry in entries:
            if entry.get("rule") not in known_rules:
                raise GuardError(f"{section} references unknown rule {entry.get('rule')!r}")
            if not isinstance(entry.get("max_count"), int) or entry["max_count"] < 0:
                raise GuardError(f"{section} entry has invalid max_count")
            expiry = entry.get("expires_at_phase")
            if not isinstance(expiry, int) or not 1 <= expiry <= 6:
                raise GuardError(f"{section} entry has invalid expires_at_phase")


def _matches_glob(path: str, pattern: str) -> bool:
    normalized = pattern.removeprefix("./")
    return fnmatch.fnmatchcase(path, normalized) or Path(path).match(normalized)


def _is_configured_exclusion(path: str, scan_config: dict[str, Any]) -> bool:
    patterns = scan_config.get("excluded_globs", []) + scan_config.get("generated_globs", [])
    return any(_matches_glob(path, pattern) for pattern in patterns)


def _is_text_candidate(path: Path, scan_config: dict[str, Any]) -> bool:
    suffixes = set(scan_config.get("text_suffixes", []))
    names = set(scan_config.get("text_names", []))
    return path.suffix.lower() in suffixes or path.name in names


def _looks_binary(path: Path, scan_config: dict[str, Any]) -> bool:
    if path.suffix.lower() in set(scan_config.get("binary_suffixes", [])):
        return True
    try:
        return b"\0" in path.read_bytes()[:4096]
    except OSError as exc:
        raise GuardError(f"cannot inspect {path}: {exc}") from exc


def discover_text_files(root: Path, scan_config: dict[str, Any]) -> tuple[list[Path], int]:
    discovered: dict[str, Path] = {}
    excluded = 0
    for configured_root in scan_config.get("roots", []):
        start = root / configured_root
        if not start.exists():
            continue
        candidates = [start] if start.is_file() else start.rglob("*")
        for candidate in candidates:
            if not candidate.is_file():
                continue
            relative = candidate.relative_to(root).as_posix()
            if relative in discovered:
                continue
            if _is_configured_exclusion(relative, scan_config):
                excluded += 1
                continue
            if not _is_text_candidate(candidate, scan_config) or _looks_binary(candidate, scan_config):
                excluded += 1
                continue
            discovered[relative] = candidate
    return [discovered[key] for key in sorted(discovered)], excluded


def _line_number(line_starts: list[int], offset: int) -> int:
    return bisect.bisect_right(line_starts, offset)


def _regex_findings(
    relative: str,
    text: str,
    rule: dict[str, Any],
) -> list[Finding]:
    line_starts = [0]
    line_starts.extend(match.end() for match in re.finditer("\n", text))
    pattern = re.compile(rule["pattern"])
    return [
        Finding(
            rule=rule["id"],
            path=relative,
            line=_line_number(line_starts, match.start()),
            token=match.group(0).replace("\n", "\\n"),
            message=rule["message"],
        )
        for match in pattern.finditer(text)
    ]


_CPP_NON_CODE = re.compile(
    r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
    re.DOTALL,
)
_NAMESPACE_OPEN = re.compile(r"\bnamespace(?:\s+([A-Za-z_]\w*(?:::\w+)*))?\s*\{")
_TYPE_DECLARATION = re.compile(
    r"\b(?:struct|class|enum\s+class)\s+([A-Za-z_]\w*)"
    r"|\busing\s+([A-Za-z_]\w*)\s*="
)


def _blank_non_code(match: re.Match[str]) -> str:
    return "".join("\n" if character == "\n" else " " for character in match.group(0))


def _root_namespace_findings(
    relative: str,
    text: str,
    rule: dict[str, Any],
) -> list[Finding]:
    if Path(relative).suffix.lower() not in {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}:
        return []
    sanitized = _CPP_NON_CODE.sub(_blank_non_code, text)
    target_namespace = rule["namespace"]
    namespace_stack: list[tuple[str, int]] = []
    brace_depth = 0
    findings: list[Finding] = []

    for line_number, line in enumerate(sanitized.splitlines(), 1):
        for match in _NAMESPACE_OPEN.finditer(line):
            raw_name = match.group(1)
            parent_name = namespace_stack[-1][0] if namespace_stack else ""
            if raw_name is None:
                full_name = f"{parent_name}::<anonymous>" if parent_name else "<anonymous>"
            elif "::" in raw_name or not parent_name:
                full_name = raw_name
            else:
                full_name = f"{parent_name}::{raw_name}"
            prefix = line[: match.end()]
            namespace_depth = brace_depth + prefix.count("{") - prefix.count("}")
            namespace_stack.append((full_name, namespace_depth))

        if namespace_stack and namespace_stack[-1][0] == target_namespace:
            for match in _TYPE_DECLARATION.finditer(line):
                type_name = match.group(1) or match.group(2)
                findings.append(
                    Finding(
                        rule=rule["id"],
                        path=relative,
                        line=line_number,
                        token=type_name,
                        message=rule["message"],
                    )
                )

        brace_depth += line.count("{") - line.count("}")
        while namespace_stack and brace_depth < namespace_stack[-1][1]:
            namespace_stack.pop()
    return findings


def _mapped_path_findings(root: Path, manifest: dict[str, Any]) -> list[Finding]:
    findings: list[Finding] = []
    for entry in manifest["renames"]["paths"]:
        old_path = str(entry["from"])
        if (root / old_path).exists():
            findings.append(
                Finding(
                    rule="mapped-old-path",
                    path=old_path,
                    line=0,
                    token=old_path,
                    message=f"old path must move to {entry['to']}",
                    expires_at_phase=entry["expires_at_phase"],
                )
            )
    return findings


def _unmapped_path_findings(
    root: Path,
    files: list[Path],
    manifest: dict[str, Any],
) -> list[Finding]:
    mapped_sources = {str(entry["from"]) for entry in manifest["renames"]["paths"]}
    mapped_sources.update(
        str(entry["path"]) for entry in manifest.get("new_paths", [])
    )
    findings: list[Finding] = []
    for path in files:
        relative = path.relative_to(root).as_posix()
        if relative in mapped_sources:
            continue
        for rule in manifest.get("unmapped_path_rules", []):
            if re.search(rule["pattern"], relative):
                findings.append(
                    Finding(
                        rule=rule["id"],
                        path=relative,
                        line=0,
                        token=relative,
                        message=rule["message"],
                        forced_violation=True,
                    )
                )
    return findings


def _protected_file_findings(root: Path, manifest: dict[str, Any], phase: int) -> list[Finding]:
    findings: list[Finding] = []
    for entry in manifest.get("protected_files", []):
        if phase > entry["protect_through_phase"]:
            continue
        relative = entry["path"]
        path = root / relative
        if not path.is_file():
            findings.append(
                Finding(
                    rule="protected-file",
                    path=relative,
                    line=0,
                    token="missing",
                    message="protected file is missing",
                    forced_violation=True,
                )
            )
            continue
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != entry["sha256"]:
            findings.append(
                Finding(
                    rule="protected-file",
                    path=relative,
                    line=0,
                    token=actual,
                    message=f"protected SHA-256 differs from {entry['sha256']}",
                    forced_violation=True,
                )
            )
    return findings


def collect_findings(
    root: Path,
    manifest: dict[str, Any],
    phase: int,
) -> tuple[list[Finding], ScanStats]:
    scan_config = manifest["scan"]
    files, excluded = discover_text_files(root, scan_config)
    findings = _mapped_path_findings(root, manifest)
    findings.extend(_unmapped_path_findings(root, files, manifest))
    findings.extend(_protected_file_findings(root, manifest, phase))
    for path in files:
        relative = path.relative_to(root).as_posix()
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            raise GuardError(f"cannot read {relative}: {exc}") from exc
        for rule in manifest["rules"]:
            if rule["kind"] == "regex":
                findings.extend(_regex_findings(relative, text, rule))
            else:
                findings.extend(_root_namespace_findings(relative, text, rule))
    findings.sort(key=lambda item: (item.path, item.line, item.rule, item.token))
    return findings, ScanStats(scanned_files=len(files), excluded_files=excluded)


def _active_limit(
    manifest: dict[str, Any],
    rule: str,
    path: str,
    phase: int,
) -> int:
    limits: list[int] = []
    for section in ("baseline_debt", "compatibility_allowances"):
        for entry in manifest.get(section, []):
            if entry["rule"] != rule or phase >= entry["expires_at_phase"]:
                continue
            if _matches_glob(path, entry["path"]):
                limits.append(entry["max_count"])
    return max(limits, default=0)


def classify_findings(
    findings: list[Finding],
    manifest: dict[str, Any],
    phase: int,
) -> tuple[tuple[Finding, ...], tuple[Finding, ...]]:
    debt: list[Finding] = []
    violations: list[Finding] = []
    text_groups: dict[tuple[str, str], list[Finding]] = defaultdict(list)

    for finding in findings:
        if finding.forced_violation:
            violations.append(finding)
        elif finding.expires_at_phase is not None:
            if phase < finding.expires_at_phase:
                debt.append(finding)
            else:
                violations.append(finding)
        else:
            text_groups[(finding.rule, finding.path)].append(finding)

    for (rule, path), group in sorted(text_groups.items()):
        limit = _active_limit(manifest, rule, path, phase)
        debt.extend(group[:limit])
        violations.extend(group[limit:])

    debt.sort(key=lambda item: (item.path, item.line, item.rule, item.token))
    violations.sort(key=lambda item: (item.path, item.line, item.rule, item.token))
    return tuple(debt), tuple(violations)


def run_scan(root: Path, manifest_path: Path, phase: int) -> ScanReport:
    if phase not in range(6):
        raise GuardError("phase must be between 0 and 5")
    root = root.resolve()
    if not root.is_dir():
        raise GuardError(f"scan root is not a directory: {root}")
    manifest = load_manifest(manifest_path.resolve())
    findings, stats = collect_findings(root, manifest, phase)
    debt, violations = classify_findings(findings, manifest, phase)
    return ScanReport(phase=phase, debt=debt, violations=violations, stats=stats)


def _location(finding: Finding) -> str:
    return f"{finding.path}:{finding.line}" if finding.line else finding.path


def print_report(report: ScanReport, show_debt: bool = False) -> None:
    for finding in report.violations:
        print(
            f"ERROR [{finding.rule}] {_location(finding)}: {finding.message}; "
            f"token={finding.token!r}"
        )
    if show_debt:
        for finding in report.debt:
            print(
                f"DEBT  [{finding.rule}] {_location(finding)}: {finding.message}; "
                f"token={finding.token!r}"
            )
    for rule, count in sorted(report.debt_counts.items()):
        print(f"DEBT-SUMMARY [{rule}] count={count}")
    for rule, count in sorted(report.violation_counts.items()):
        print(f"ERROR-SUMMARY [{rule}] count={count}")
    print(
        f"SUMMARY phase={report.phase} scanned={report.stats.scanned_files} "
        f"excluded={report.stats.excluded_files} debt={len(report.debt)} "
        f"violations={len(report.violations)}"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    default_root = Path(__file__).resolve().parents[3]
    default_manifest = Path(__file__).with_name("rename_manifest.yaml")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=default_root, help="repository root")
    parser.add_argument("--phase", type=int, choices=range(6), required=True)
    parser.add_argument("--manifest", type=Path, default=default_manifest)
    parser.add_argument("--show-debt", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        report = run_scan(args.root, args.manifest, args.phase)
    except GuardError as exc:
        print(f"ERROR [guard] {exc}", file=sys.stderr)
        return 2
    print_report(report, show_debt=args.show_debt)
    return 1 if report.violations else 0


if __name__ == "__main__":
    sys.exit(main())
