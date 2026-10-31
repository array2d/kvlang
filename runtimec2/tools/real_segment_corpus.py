#!/usr/bin/env python3
"""Build a deterministic ten-case report from real layout-backed Segments.

Each benchmark is laid out into its own temporary KVSpace. The existing
``segment_shape_dump`` exporter and ``binir_format_probe.py`` are then run per
case so repeated logical paths such as ``/lib/test`` never collide. The final
report measures exported instruction shapes only; it is not a complete Segment
size and does not freeze a BinIR format.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence


ROOT = Path(__file__).resolve().parents[2]
REPORT_SCHEMA = "kvlang.real-segment-corpus.report.v1"
EXPORT_SCHEMA = "kvlang.binir-probe.v1"
PROBE_SCHEMA = "kvlang.binir-probe.report.v1"
CASE_NAMES = (
    "binary_search",
    "binary_trees",
    "fib",
    "hash_table",
    "iops",
    "k_nucleotide",
    "matmul",
    "nqueens",
    "prime_sieve",
    "quicksort",
)
CANDIDATE_NAMES = ("codeunit16", "fixed8", "opcode_specific")
SUMMARY_FIELDS = (
    "code_bytes",
    "extension_instructions_by_reason",
    "index_overflows",
    "instructions",
    "narrow_instructions",
    "narrow_percent",
    "narrow_ratio",
    "wide_instructions",
    "wide_percent",
    "wide_ratio",
)


class CorpusError(RuntimeError):
    """Raised when a corpus input or subprocess result is incomplete."""


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    offset: int


@dataclass(frozen=True)
class Toolchain:
    source_root: Path
    byteseek: Path
    exporter: Path
    probe: Path
    timeout: int


def _identifier_start(char: str) -> bool:
    return char == "_" or char.isalpha()


def _identifier_continue(char: str) -> bool:
    return _identifier_start(char) or "0" <= char <= "9"


def _skip_quoted(source: str, start: int, quote: str) -> int:
    index = start + 1
    while index < len(source):
        if source[index] == "\\":
            index += 2
        elif source[index] == quote:
            return index + 1
        else:
            index += 1
    raise CorpusError(f"unterminated {quote} string at offset {start}")


def _raw_string_end(source: str, start: int) -> int | None:
    if source[start] != "r":
        return None
    index = start + 1
    while index < len(source) and source[index] == "#":
        index += 1
    if index >= len(source) or source[index] != '"':
        return None
    marker = '"' + "#" * (index - start - 1)
    end = source.find(marker, index + 1)
    if end < 0:
        raise CorpusError(f"unterminated raw string at offset {start}")
    return end + len(marker)


def _tokens(source: str) -> list[Token]:
    tokens: list[Token] = []
    index = 0
    while index < len(source):
        char = source[index]
        if char.isspace():
            index += 1
            continue
        if source.startswith("//", index):
            newline = source.find("\n", index + 2)
            index = len(source) if newline < 0 else newline + 1
            continue
        if source.startswith("/*", index):
            start = index
            depth = 1
            index += 2
            while index < len(source) and depth:
                if source.startswith("/*", index):
                    depth += 1
                    index += 2
                elif source.startswith("*/", index):
                    depth -= 1
                    index += 2
                else:
                    index += 1
            if depth:
                raise CorpusError(f"unterminated block comment at offset {start}")
            continue
        raw_end = _raw_string_end(source, index)
        if raw_end is not None:
            index = raw_end
            continue
        if char in {'"', "'"}:
            index = _skip_quoted(source, index, char)
            continue
        if _identifier_start(char):
            end = index + 1
            while end < len(source) and _identifier_continue(source[end]):
                end += 1
            tokens.append(Token("identifier", source[index:end], index))
            index = end
            continue
        tokens.append(Token("punctuation", char, index))
        index += 1
    return tokens


def parse_rwfunc_names(source: str, label: str = "<source>") -> tuple[str, ...]:
    tokens = _tokens(source)
    names: list[str] = []
    for index, token in enumerate(tokens):
        if token.kind != "identifier" or token.value != "rwfunc":
            continue
        if index + 2 >= len(tokens):
            continue
        name = tokens[index + 1]
        opening = tokens[index + 2]
        if name.kind != "identifier" or opening.value != "(":
            continue
        names.append(name.value)
    if not names:
        raise CorpusError(f"{label}: no rwfunc declarations")
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        raise CorpusError(f"{label}: duplicate rwfunc declarations: {', '.join(duplicates)}")
    return tuple(names)


def load_min_scales(run_path: Path) -> dict[str, int]:
    try:
        source = run_path.read_text(encoding="utf-8")
        tree = ast.parse(source, filename=str(run_path))
    except (OSError, UnicodeError, SyntaxError) as error:
        raise CorpusError(f"cannot read benchmark SWEEP from {run_path}: {error}") from error
    values: list[Any] = []
    for node in tree.body:
        try:
            if isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id == "SWEEP"
                for target in node.targets
            ):
                values.append(ast.literal_eval(node.value))
            elif (
                isinstance(node, ast.AnnAssign)
                and isinstance(node.target, ast.Name)
                and node.target.id == "SWEEP"
            ):
                values.append(ast.literal_eval(node.value))
        except (ValueError, TypeError) as error:
            raise CorpusError(f"{run_path}: SWEEP must be a literal dictionary") from error
    if len(values) != 1 or not isinstance(values[0], dict):
        raise CorpusError(f"{run_path}: expected exactly one literal SWEEP dictionary")
    sweep = values[0]
    if set(sweep) != set(CASE_NAMES):
        missing = sorted(set(CASE_NAMES) - set(sweep))
        extra = sorted(set(sweep) - set(CASE_NAMES))
        raise CorpusError(f"{run_path}: SWEEP case mismatch; missing={missing}, extra={extra}")
    minimums: dict[str, int] = {}
    for case in CASE_NAMES:
        scales = sweep[case]
        if (
            not isinstance(scales, (list, tuple))
            or not scales
            or any(isinstance(scale, bool) or not isinstance(scale, int) for scale in scales)
        ):
            raise CorpusError(f"{run_path}: SWEEP[{case!r}] must be non-empty integers")
        minimums[case] = min(scales)
    return minimums


def scaled_source(path: Path, scale: int) -> str:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise CorpusError(f"cannot read benchmark source {path}: {error}") from error
    if "__SCALE__" not in source:
        raise CorpusError(f"{path}: missing __SCALE__ token")
    return source.replace("__SCALE__", str(scale))


def _run(
    command: Sequence[str],
    *,
    cwd: Path,
    env: dict[str, str] | None,
    timeout: int,
    stage: str,
) -> subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(
            list(command), cwd=cwd, env=env, capture_output=True, timeout=timeout
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise CorpusError(f"{stage}: cannot complete command: {error}") from error
    if result.returncode != 0:
        stdout = result.stdout.decode("utf-8", errors="replace")
        stderr = result.stderr.decode("utf-8", errors="replace")
        raise CorpusError(
            f"{stage}: command exited {result.returncode}: {' '.join(command)}\n"
            f"stdout:\n{stdout}\nstderr:\n{stderr}"
        )
    return result


def _canonical_document(raw: bytes, label: str) -> dict[str, Any]:
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise CorpusError(f"{label}: invalid UTF-8 JSON: {error}") from error
    if not isinstance(document, dict):
        raise CorpusError(f"{label}: expected JSON object")
    canonical = (
        json.dumps(document, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
    if raw != canonical:
        raise CorpusError(f"{label}: output is not canonical compact JSON")
    return document


def _validate_exporter(
    document: dict[str, Any], expected_paths: list[str], label: str
) -> dict[str, int]:
    if document.get("schema") != EXPORT_SCHEMA:
        raise CorpusError(f"{label}: unexpected exporter schema")
    if document.get("fixture_only") is not False:
        raise CorpusError(f"{label}: real Segment export marked as fixture")
    functions = document.get("functions")
    if not isinstance(functions, list):
        raise CorpusError(f"{label}: exporter functions must be an array")
    actual_paths = [item.get("name") for item in functions if isinstance(item, dict)]
    if len(actual_paths) != len(functions) or actual_paths != expected_paths:
        raise CorpusError(
            f"{label}: exported functions differ from declarations; "
            f"expected={expected_paths}, actual={actual_paths}"
        )
    counts: dict[str, int] = {}
    for function in functions:
        instructions = function.get("instructions")
        if not isinstance(instructions, list):
            raise CorpusError(f"{label}: {function['name']} instructions must be an array")
        counts[function["name"]] = len(instructions)
    return counts


def _candidate_summary(candidate: dict[str, Any], label: str) -> dict[str, Any]:
    missing = [field for field in SUMMARY_FIELDS if field not in candidate]
    if missing:
        raise CorpusError(f"{label}: candidate summary lacks {', '.join(missing)}")
    return {field: candidate[field] for field in SUMMARY_FIELDS}


def _validate_probe(
    report: dict[str, Any], instruction_counts: dict[str, int], label: str
) -> None:
    if report.get("schema") != PROBE_SCHEMA:
        raise CorpusError(f"{label}: unexpected probe schema")
    totals = report.get("totals")
    if totals != {
        "functions": len(instruction_counts),
        "instructions": sum(instruction_counts.values()),
    }:
        raise CorpusError(f"{label}: probe totals differ from exporter")
    candidates = report.get("candidates")
    if not isinstance(candidates, dict) or set(candidates) != set(CANDIDATE_NAMES):
        raise CorpusError(f"{label}: probe candidate set changed")
    for name in CANDIDATE_NAMES:
        candidate = candidates[name]
        if not isinstance(candidate, dict):
            raise CorpusError(f"{label}: candidate {name} must be an object")
        _candidate_summary(candidate, f"{label}/{name}")
        per_function = candidate.get("per_function")
        if not isinstance(per_function, list):
            raise CorpusError(f"{label}/{name}: per_function must be an array")
        observed = {
            item.get("function"): item.get("instructions")
            for item in per_function
            if isinstance(item, dict)
        }
        if observed != instruction_counts:
            raise CorpusError(f"{label}/{name}: per-function totals differ from exporter")


def _sha256(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def _case_report(
    case: str,
    scale: int,
    source_relative: str,
    declared_names: tuple[str, ...],
    exporter_raw: bytes,
    exporter_document: dict[str, Any],
    probe_raw: bytes,
    probe_report: dict[str, Any],
) -> dict[str, Any]:
    instruction_counts = {
        function["name"]: len(function["instructions"])
        for function in exporter_document["functions"]
    }
    candidate_functions = {
        candidate: {
            item["function"]: item
            for item in probe_report["candidates"][candidate]["per_function"]
        }
        for candidate in CANDIDATE_NAMES
    }
    functions = []
    for logical_name in sorted(declared_names):
        segment_path = f"/lib/{logical_name}"
        functions.append(
            {
                "encodings": {
                    candidate: _candidate_summary(
                        candidate_functions[candidate][segment_path],
                        f"{case}/{logical_name}/{candidate}",
                    )
                    for candidate in CANDIDATE_NAMES
                },
                "instructions": instruction_counts[segment_path],
                "logical_name": logical_name,
                "segment_path": segment_path,
            }
        )
    return {
        "artifacts": {
            "exporter_json_sha256": _sha256(exporter_raw),
            "probe_report_sha256": _sha256(probe_raw),
        },
        "candidates": {
            candidate: _candidate_summary(
                probe_report["candidates"][candidate], f"{case}/{candidate}"
            )
            for candidate in CANDIDATE_NAMES
        },
        "case": case,
        "functions": functions,
        "metadata_model": probe_report["metadata"],
        "scale": scale,
        "source": source_relative,
        "totals": {
            "functions": len(functions),
            "instructions": sum(instruction_counts.values()),
        },
    }


def _ratio(part: int, total: int) -> tuple[float, float]:
    if not total:
        return 0.0, 0.0
    ratio = part / total
    return round(ratio, 6), round(ratio * 100, 2)


def _sum_named_counts(items: Sequence[dict[str, int]]) -> dict[str, int]:
    names = sorted({name for item in items for name in item})
    return {name: sum(item.get(name, 0) for item in items) for name in names}


def _aggregate_candidates(cases: Sequence[dict[str, Any]]) -> dict[str, Any]:
    aggregate: dict[str, Any] = {}
    for candidate_name in CANDIDATE_NAMES:
        candidates = [case["candidates"][candidate_name] for case in cases]
        instructions = sum(item["instructions"] for item in candidates)
        narrow = sum(item["narrow_instructions"] for item in candidates)
        wide = sum(item["wide_instructions"] for item in candidates)
        narrow_ratio, narrow_percent = _ratio(narrow, instructions)
        wide_ratio, wide_percent = _ratio(wide, instructions)
        aggregate[candidate_name] = {
            "code_bytes": sum(item["code_bytes"] for item in candidates),
            "extension_instructions_by_reason": _sum_named_counts(
                [item["extension_instructions_by_reason"] for item in candidates]
            ),
            "index_overflows": _sum_named_counts(
                [item["index_overflows"] for item in candidates]
            ),
            "instructions": instructions,
            "narrow_instructions": narrow,
            "narrow_percent": narrow_percent,
            "narrow_ratio": narrow_ratio,
            "wide_instructions": wide,
            "wide_percent": wide_percent,
            "wide_ratio": wide_ratio,
        }
    return aggregate


def _aggregate_metadata(cases: Sequence[dict[str, Any]]) -> dict[str, Any]:
    metadata = [case["metadata_model"] for case in cases]
    return {
        "effect_map": {
            "bytes": sum(item["effect_map"]["bytes"] for item in metadata),
            "set_bits": sum(item["effect_map"]["set_bits"] for item in metadata),
        },
        "safepoint_map": {
            "bytes": sum(item["safepoint_map"]["bytes"] for item in metadata),
            "set_bits": sum(item["safepoint_map"]["set_bits"] for item in metadata),
        },
        "side_exit_map": {
            "bytes": sum(item["side_exit_map"]["bytes"] for item in metadata),
            "entries": sum(item["side_exit_map"]["entries"] for item in metadata),
        },
        "source_pc_map": {
            "bytes": sum(item["source_pc_map"]["bytes"] for item in metadata),
            "known_source_entries": sum(
                item["source_pc_map"]["known_source_entries"] for item in metadata
            ),
            "unknown_source_entries": sum(
                item["source_pc_map"]["unknown_source_entries"] for item in metadata
            ),
        },
        "total_bytes": sum(item["total_bytes"] for item in metadata),
    }


def build_aggregate(cases: Sequence[dict[str, Any]]) -> dict[str, Any]:
    ordered = sorted(cases, key=lambda item: item["case"])
    if [item["case"] for item in ordered] != list(CASE_NAMES):
        raise CorpusError("aggregate does not contain exactly the frozen ten cases")
    return {
        "candidates": _aggregate_candidates(ordered),
        "cases": ordered,
        "corpus": {
            "case_names": list(CASE_NAMES),
            "fixture_only": False,
            "function_identity": (
                "Functions are scoped by case and retain their real /lib path; repeated "
                "/lib/test paths are never merged or renamed."
            ),
            "layout_only": True,
            "scale_selection": "minimum integer from benchmark/run.py SWEEP",
        },
        "limitations": {
            "exporter_missing": [
                "effect annotations (exported as false)",
                "safepoint annotations (exported as false)",
                "side-exit metadata (exported as empty)",
                "constant payload bytes and values",
                "RegDesc table bytes",
                "segment header bytes",
                "section alignment and padding",
            ],
            "format_status": (
                "Instruction-shape experiment only; no production BinIR format is frozen."
            ),
            "size_scope": (
                "code_bytes excludes constant payloads, RegDesc, headers, and alignment; "
                "metadata_model is the probe's modeled cost, not exported runtime metadata."
            ),
        },
        "metadata_model": _aggregate_metadata(ordered),
        "schema": REPORT_SCHEMA,
        "totals": {
            "cases": len(ordered),
            "functions": sum(item["totals"]["functions"] for item in ordered),
            "instructions": sum(item["totals"]["instructions"] for item in ordered),
        },
    }


def render_report(report: dict[str, Any]) -> bytes:
    return (
        json.dumps(report, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def run_corpus(toolchain: Toolchain, artifacts_root: Path) -> dict[str, Any]:
    for label, path in (
        ("source root", toolchain.source_root),
        ("byteseek", toolchain.byteseek),
        ("exporter", toolchain.exporter),
        ("probe", toolchain.probe),
    ):
        exists = path.is_dir() if label == "source root" else path.is_file()
        if not exists:
            raise CorpusError(f"required {label} does not exist: {path}")
    minimums = load_min_scales(toolchain.source_root / "benchmark/run.py")
    cases: list[dict[str, Any]] = []
    for case in CASE_NAMES:
        source_relative = f"benchmark/cases/{case}/{case}.kv"
        source_path = toolchain.source_root / source_relative
        rendered_source = scaled_source(source_path, minimums[case])
        declared_names = parse_rwfunc_names(rendered_source, source_relative)
        expected_paths = sorted(f"/lib/{name}" for name in declared_names)

        case_root = artifacts_root / case
        case_root.mkdir(parents=True, exist_ok=False)
        rendered_path = case_root / f"{case}.kv"
        rendered_path.write_text(rendered_source, encoding="utf-8")
        dsn = f"shm://{case_root.resolve()}/segments.shm"
        env = {**os.environ, "KVSPACE": dsn}

        _run(
            [str(toolchain.byteseek), "isa", "publish"],
            cwd=toolchain.source_root,
            env=env,
            timeout=toolchain.timeout,
            stage=f"{case}/isa publish",
        )
        _run(
            [str(toolchain.byteseek), "layout", str(rendered_path)],
            cwd=toolchain.source_root,
            env=env,
            timeout=toolchain.timeout,
            stage=f"{case}/layout",
        )
        exporter = _run(
            [str(toolchain.exporter), dsn, *expected_paths],
            cwd=toolchain.source_root,
            env=env,
            timeout=toolchain.timeout,
            stage=f"{case}/export",
        )
        exporter_document = _canonical_document(exporter.stdout, f"{case}/export")
        instruction_counts = _validate_exporter(
            exporter_document, expected_paths, f"{case}/export"
        )
        exporter_path = case_root / "exporter.json"
        exporter_path.write_bytes(exporter.stdout)

        probe = _run(
            [sys.executable, str(toolchain.probe), "--compact", str(exporter_path)],
            cwd=toolchain.source_root,
            env=env,
            timeout=toolchain.timeout,
            stage=f"{case}/probe",
        )
        probe_report = _canonical_document(probe.stdout, f"{case}/probe")
        _validate_probe(probe_report, instruction_counts, f"{case}/probe")
        (case_root / "probe.json").write_bytes(probe.stdout)
        cases.append(
            _case_report(
                case,
                minimums[case],
                source_relative,
                declared_names,
                exporter.stdout,
                exporter_document,
                probe.stdout,
                probe_report,
            )
        )
    return build_aggregate(cases)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Layout the frozen ten benchmark cases into isolated stores, export all "
            "declared rwfunc Segments, and emit one canonical BinIR probe aggregate."
        )
    )
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--byteseek", type=Path, default=ROOT / "bin/byteseek")
    parser.add_argument(
        "--exporter", type=Path, default=ROOT / "build/runtimec2/segment_shape_dump"
    )
    parser.add_argument(
        "--probe", type=Path, default=ROOT / "runtimec2/tools/binir_format_probe.py"
    )
    parser.add_argument(
        "--artifacts-dir",
        type=Path,
        help="retain per-case scaled source, canonical exporter JSON, and probe report",
    )
    parser.add_argument("--timeout", type=int, default=120)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    toolchain = Toolchain(
        source_root=args.source_root.resolve(),
        byteseek=args.byteseek.resolve(),
        exporter=args.exporter.resolve(),
        probe=args.probe.resolve(),
        timeout=args.timeout,
    )
    try:
        if args.artifacts_dir is not None:
            artifacts_root = args.artifacts_dir.resolve()
            if artifacts_root.exists() and any(artifacts_root.iterdir()):
                raise CorpusError(f"artifacts directory is not empty: {artifacts_root}")
            artifacts_root.mkdir(parents=True, exist_ok=True)
            report = run_corpus(toolchain, artifacts_root)
        else:
            with tempfile.TemporaryDirectory(prefix="kvlang-real-segment-corpus-") as directory:
                report = run_corpus(toolchain, Path(directory))
        sys.stdout.buffer.write(render_report(report))
    except CorpusError as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
