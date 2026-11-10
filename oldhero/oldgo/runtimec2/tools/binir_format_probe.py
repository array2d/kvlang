#!/usr/bin/env python3
"""Deterministic offline size probe for proposed BinIR encodings.

This tool measures normalized instruction *shapes*. It does not implement, emit,
or freeze a production BinIR format. Input is UTF-8 JSON with this schema::

    {
      "schema": "kvlang.binir-probe.v1",
      "corpus": "a stable corpus label",
      "provenance": "how the data was obtained",
      "fixture_only": true,
      "functions": [{
        "name": "/lib/example",
        "register_count": 3,
        "constant_count": 1,
        "instructions": [{
          "op": "int64.add",
          "family": "generic",
          "reads": [{"kind": "reg", "index": 0},
                    {"kind": "const", "index": 0}],
          "writes": [2],
          "targets": [],
          "source_id": 7,
          "effect": false,
          "safepoint": false,
          "side_exits": []
        }]
      }]
    }

``family`` is one of ``generic``, ``branch``, ``array``, or ``call``.
``targets`` are relative instruction displacements. A call's ``immediate`` is
its callee/segment-pool index. A future layout/runtimec2 exporter should walk
``Segment::insns`` in s0 order, map literal ``RegDesc`` entries to stable
per-function ConstIds, copy rd/wr shapes, resolve branch targets to relative
instruction offsets, and emit source/effect/safepoint/side-exit annotations.
That adapter is deliberately not part of this probe.

Measured candidates (all assumptions are also embedded in every report):

* ``fixed8``: an 8-byte narrow form with two tagged read bytes, one write byte,
  and one 16-bit immediate/target. Narrow tagged reads have only 7 index bits,
  so both RegId and ConstId are limited to 0..127. A self-describing wide form
  uses 16-bit operands and 32-bit immediates/targets.
* ``opcode_specific``: opcode-known arity, compact one-byte operands, native
  branch/array/call shapes, and a two-byte extension descriptor when indexes
  or immediate widths overflow the compact form.
* ``codeunit16``: one 16-bit header plus one 16-bit unit per operand or short
  immediate/target. Reads use the high bit as the const tag (0..32767 index).
  More than four units is reported as extended/WIDE for comparison.

Code byte counts exclude the segment header, constant payloads, RegDesc data,
and alignment between sections because normalized shape input cannot measure
them. Metadata is never treated as zero: source/PC entries cost 8 bytes per
instruction, effect and safepoint maps are dense bitsets per function, and each
side-exit entry costs 12 bytes.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

INPUT_SCHEMA = "kvlang.binir-probe.v1"
REPORT_SCHEMA = "kvlang.binir-probe.report.v1"
FAMILIES = frozenset({"generic", "branch", "array", "call"})
CORE_EXTENSION_REASONS = (
    "three_operand_branch",
    "array_operation",
    "multi_argument_call",
    "multi_return",
)


class ProbeError(ValueError):
    """Raised for malformed or ambiguous corpus input."""


@dataclass(frozen=True)
class Operand:
    kind: str
    index: int


@dataclass(frozen=True)
class SideExit:
    target: int
    reason: str


@dataclass(frozen=True)
class Instruction:
    op: str
    family: str
    reads: tuple[Operand, ...]
    writes: tuple[int, ...]
    targets: tuple[int, ...]
    immediate: int | None
    source_id: int | None
    effect: bool
    safepoint: bool
    side_exits: tuple[SideExit, ...]


@dataclass(frozen=True)
class Function:
    name: str
    register_count: int
    constant_count: int
    instructions: tuple[Instruction, ...]


@dataclass(frozen=True)
class Corpus:
    labels: tuple[str, ...]
    provenance: tuple[str, ...]
    fixture_only: bool
    functions: tuple[Function, ...]


@dataclass(frozen=True)
class EncodingMeasurement:
    code_bytes: int
    wide: bool
    read_register_overflows: int
    write_register_overflows: int
    constant_overflows: int
    extension_reasons: tuple[str, ...]


ASSUMPTIONS = {
    "scope": (
        "Offline instruction-shape measurement only; no production encoding is "
        "implemented or frozen."
    ),
    "code_bytes_exclude": [
        "segment_header",
        "constant_pool_payloads",
        "regdesc_table",
        "inter_section_alignment",
    ],
    "fixed8": (
        "NARROW is 8 bytes: tagged u8 reads (RegId/ConstId 0..127), u8 write "
        "RegId, and one i16/u16 target/immediate. WIDE has an 8-byte descriptor, "
        "u16 operands, and i32/u32 targets/immediates."
    ),
    "opcode_specific": (
        "Opcode-known arity uses compact tagged u8 reads and u8 writes; branch "
        "supports one condition plus two i16 targets, array arity is opcode-known, "
        "and call carries up to 15 args/4 results. A 2-byte descriptor selects "
        "u16 operands or 32-bit targets/immediates when compact fields overflow."
    ),
    "codeunit16": (
        "One 16-bit header plus one unit per operand/short target/immediate; tagged "
        "reads have 15 index bits. Values beyond a native unit use three units. "
        "Instructions longer than four units are classified WIDE."
    ),
    "metadata": (
        "source_pc is pc:u32+source_id:u32 per instruction (unknown source uses a "
        "sentinel); effect and safepoint are separate dense bitsets per function; "
        "each side exit is 12 bytes (pc:u32,target:i32,reason:u16,flags:u16)."
    ),
    "ratio_definition": (
        "NARROW/WIDE is candidate-specific: fixed8 means 8-byte versus expanded; "
        "opcode_specific means compact versus extension descriptor; codeunit16 "
        "means at most versus more than four 16-bit units."
    ),
}


def _expect_object(value: Any, where: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ProbeError(f"{where}: expected object")
    return value


def _expect_list(value: Any, where: str) -> list[Any]:
    if not isinstance(value, list):
        raise ProbeError(f"{where}: expected array")
    return value


def _expect_string(value: Any, where: str) -> str:
    if not isinstance(value, str) or not value:
        raise ProbeError(f"{where}: expected non-empty string")
    return value


def _expect_bool(value: Any, where: str) -> bool:
    if not isinstance(value, bool):
        raise ProbeError(f"{where}: expected boolean")
    return value


def _expect_int(value: Any, where: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ProbeError(f"{where}: expected integer")
    if value < minimum or value > maximum:
        raise ProbeError(f"{where}: expected {minimum}..{maximum}, got {value}")
    return value


def _reject_unknown(obj: dict[str, Any], allowed: set[str], where: str) -> None:
    unknown = sorted(set(obj) - allowed)
    if unknown:
        raise ProbeError(f"{where}: unknown fields: {', '.join(unknown)}")


def _parse_operand(value: Any, where: str) -> Operand:
    obj = _expect_object(value, where)
    _reject_unknown(obj, {"kind", "index"}, where)
    kind = _expect_string(obj.get("kind"), f"{where}.kind")
    if kind not in {"reg", "const"}:
        raise ProbeError(f"{where}.kind: expected 'reg' or 'const'")
    index = _expect_int(obj.get("index"), f"{where}.index", 0, 0x7FFFFFFF)
    return Operand(kind=kind, index=index)


def _parse_side_exit(value: Any, where: str) -> SideExit:
    obj = _expect_object(value, where)
    _reject_unknown(obj, {"target", "reason"}, where)
    return SideExit(
        target=_expect_int(obj.get("target"), f"{where}.target", -0x80000000, 0x7FFFFFFF),
        reason=_expect_string(obj.get("reason"), f"{where}.reason"),
    )


def _parse_instruction(value: Any, where: str) -> Instruction:
    obj = _expect_object(value, where)
    _reject_unknown(
        obj,
        {
            "op",
            "family",
            "reads",
            "writes",
            "targets",
            "immediate",
            "source_id",
            "effect",
            "safepoint",
            "side_exits",
        },
        where,
    )
    family = _expect_string(obj.get("family"), f"{where}.family")
    if family not in FAMILIES:
        raise ProbeError(f"{where}.family: expected one of {', '.join(sorted(FAMILIES))}")
    reads = tuple(
        _parse_operand(item, f"{where}.reads[{index}]")
        for index, item in enumerate(_expect_list(obj.get("reads", []), f"{where}.reads"))
    )
    writes = tuple(
        _expect_int(item, f"{where}.writes[{index}]", 0, 0x7FFFFFFF)
        for index, item in enumerate(_expect_list(obj.get("writes", []), f"{where}.writes"))
    )
    targets = tuple(
        _expect_int(item, f"{where}.targets[{index}]", -0x80000000, 0x7FFFFFFF)
        for index, item in enumerate(_expect_list(obj.get("targets", []), f"{where}.targets"))
    )
    immediate_value = obj.get("immediate")
    immediate = None
    if immediate_value is not None:
        immediate = _expect_int(
            immediate_value, f"{where}.immediate", 0, 0xFFFFFFFF
        )
    source_value = obj.get("source_id")
    source_id = None
    if source_value is not None:
        source_id = _expect_int(source_value, f"{where}.source_id", 0, 0xFFFFFFFF)
    side_exits = tuple(
        _parse_side_exit(item, f"{where}.side_exits[{index}]")
        for index, item in enumerate(
            _expect_list(obj.get("side_exits", []), f"{where}.side_exits")
        )
    )
    instruction = Instruction(
        op=_expect_string(obj.get("op"), f"{where}.op"),
        family=family,
        reads=reads,
        writes=writes,
        targets=targets,
        immediate=immediate,
        source_id=source_id,
        effect=_expect_bool(obj.get("effect", False), f"{where}.effect"),
        safepoint=_expect_bool(obj.get("safepoint", False), f"{where}.safepoint"),
        side_exits=side_exits,
    )
    if family == "branch":
        if not targets:
            raise ProbeError(f"{where}: branch requires at least one target")
        if immediate is not None:
            raise ProbeError(f"{where}: branch uses targets, not immediate")
    elif targets:
        raise ProbeError(f"{where}: targets are only valid for branch instructions")
    if family == "call" and immediate is None:
        raise ProbeError(f"{where}: call requires a callee index in immediate")
    return instruction


def _parse_function(value: Any, where: str) -> Function:
    obj = _expect_object(value, where)
    _reject_unknown(
        obj,
        {"name", "register_count", "constant_count", "instructions"},
        where,
    )
    register_count = _expect_int(
        obj.get("register_count"), f"{where}.register_count", 0, 0x7FFFFFFF
    )
    constant_count = _expect_int(
        obj.get("constant_count"), f"{where}.constant_count", 0, 0x7FFFFFFF
    )
    instructions = tuple(
        _parse_instruction(item, f"{where}.instructions[{index}]")
        for index, item in enumerate(
            _expect_list(obj.get("instructions"), f"{where}.instructions")
        )
    )
    function = Function(
        name=_expect_string(obj.get("name"), f"{where}.name"),
        register_count=register_count,
        constant_count=constant_count,
        instructions=instructions,
    )
    for instruction_index, instruction in enumerate(instructions):
        for operand_index, operand in enumerate(instruction.reads):
            limit = register_count if operand.kind == "reg" else constant_count
            if operand.index >= limit:
                raise ProbeError(
                    f"{where}.instructions[{instruction_index}].reads[{operand_index}]: "
                    f"{operand.kind} index {operand.index} is outside declared count {limit}"
                )
        for write_index, register in enumerate(instruction.writes):
            if register >= register_count:
                raise ProbeError(
                    f"{where}.instructions[{instruction_index}].writes[{write_index}]: "
                    f"register index {register} is outside declared count {register_count}"
                )
    return function


def _load_document(path: Path) -> tuple[str, str, bool, tuple[Function, ...]]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProbeError(f"{path}: cannot load JSON: {error}") from error
    obj = _expect_object(raw, str(path))
    _reject_unknown(
        obj,
        {"schema", "corpus", "provenance", "fixture_only", "functions"},
        str(path),
    )
    if obj.get("schema") != INPUT_SCHEMA:
        raise ProbeError(f"{path}.schema: expected {INPUT_SCHEMA!r}")
    functions = tuple(
        _parse_function(item, f"{path}.functions[{index}]")
        for index, item in enumerate(_expect_list(obj.get("functions"), f"{path}.functions"))
    )
    return (
        _expect_string(obj.get("corpus"), f"{path}.corpus"),
        _expect_string(obj.get("provenance"), f"{path}.provenance"),
        _expect_bool(obj.get("fixture_only"), f"{path}.fixture_only"),
        functions,
    )


def _input_files(paths: Sequence[Path]) -> list[Path]:
    files: set[Path] = set()
    for path in paths:
        if path.is_dir():
            files.update(item for item in path.rglob("*.json") if item.is_file())
        elif path.is_file():
            files.add(path)
        else:
            raise ProbeError(f"{path}: input does not exist")
    if not files:
        raise ProbeError("no JSON corpus files found")
    return sorted(files, key=lambda item: item.as_posix())


def load_corpus(paths: Sequence[Path]) -> Corpus:
    labels: list[str] = []
    provenance: list[str] = []
    fixture_flags: list[bool] = []
    functions: list[Function] = []
    for path in _input_files(paths):
        label, origin, fixture_only, document_functions = _load_document(path)
        labels.append(label)
        provenance.append(origin)
        fixture_flags.append(fixture_only)
        functions.extend(document_functions)
    functions.sort(key=lambda function: function.name)
    duplicates = [
        functions[index].name
        for index in range(1, len(functions))
        if functions[index - 1].name == functions[index].name
    ]
    if duplicates:
        raise ProbeError(f"duplicate function names: {', '.join(sorted(set(duplicates)))}")
    return Corpus(
        labels=tuple(labels),
        provenance=tuple(provenance),
        fixture_only=all(fixture_flags),
        functions=tuple(functions),
    )


def _index_overflows(
    instruction: Instruction, read_limit: int, write_limit: int
) -> tuple[int, int, int]:
    read_register = sum(
        operand.kind == "reg" and operand.index > read_limit
        for operand in instruction.reads
    )
    write_register = sum(register > write_limit for register in instruction.writes)
    constants = sum(
        operand.kind == "const" and operand.index > read_limit
        for operand in instruction.reads
    )
    return read_register, write_register, constants


def _short_width_overflow(instruction: Instruction) -> bool:
    return any(target < -0x8000 or target > 0x7FFF for target in instruction.targets) or (
        instruction.immediate is not None and instruction.immediate > 0xFFFF
    )


def _shape_reason_flags(instruction: Instruction) -> list[str]:
    reasons: list[str] = []
    if instruction.family == "branch" and len(instruction.reads) + len(instruction.targets) >= 3:
        reasons.append("three_operand_branch")
    if instruction.family == "array":
        reasons.append("array_operation")
    if instruction.family == "call" and len(instruction.reads) > 1:
        reasons.append("multi_argument_call")
    if len(instruction.writes) > 1:
        reasons.append("multi_return")
    return reasons


def _overflow_reasons(
    read_register: int,
    write_register: int,
    constants: int,
    width_overflow: bool,
) -> list[str]:
    reasons: list[str] = []
    if read_register or write_register:
        reasons.append("register_index_overflow")
    if constants:
        reasons.append("constant_index_overflow")
    if width_overflow:
        reasons.append("immediate_or_target_overflow")
    return reasons


def _align2(value: int) -> int:
    return (value + 1) & ~1


def _fixed8(instruction: Instruction) -> EncodingMeasurement:
    read_register, write_register, constants = _index_overflows(instruction, 127, 255)
    width_overflow = _short_width_overflow(instruction)
    if instruction.family == "branch":
        shape_ok = (
            len(instruction.reads) <= 1
            and not instruction.writes
            and len(instruction.targets) == 1
        )
    elif instruction.family == "call":
        shape_ok = (
            len(instruction.reads) <= 1
            and len(instruction.writes) <= 1
            and not instruction.targets
        )
    else:
        shape_ok = (
            len(instruction.reads) <= 2
            and len(instruction.writes) <= 1
            and not instruction.targets
        )
    wide = not shape_ok or bool(read_register or write_register or constants or width_overflow)
    if not wide:
        code_bytes = 8
        reasons: list[str] = []
    else:
        code_bytes = 8 + 2 * (len(instruction.reads) + len(instruction.writes))
        code_bytes += 4 * len(instruction.targets)
        if instruction.immediate is not None:
            code_bytes += 4
        reasons = _shape_reason_flags(instruction)
        reasons.extend(
            _overflow_reasons(
                read_register, write_register, constants, width_overflow
            )
        )
        if not reasons:
            reasons.append("other_operand_shape")
    return EncodingMeasurement(
        code_bytes=_align2(code_bytes),
        wide=wide,
        read_register_overflows=read_register,
        write_register_overflows=write_register,
        constant_overflows=constants,
        extension_reasons=tuple(dict.fromkeys(reasons)),
    )


def _opcode_specific(instruction: Instruction) -> EncodingMeasurement:
    read_register, write_register, constants = _index_overflows(instruction, 127, 255)
    width_overflow = _short_width_overflow(instruction)
    count_overflow = instruction.family == "call" and (
        len(instruction.reads) > 15 or len(instruction.writes) > 4
    )
    wide = bool(
        read_register or write_register or constants or width_overflow or count_overflow
    )
    read_width = 2 if read_register or constants else 1
    write_width = 2 if write_register else 1
    target_width = 4 if any(
        target < -0x8000 or target > 0x7FFF for target in instruction.targets
    ) else 2
    immediate_width = 0
    if instruction.immediate is not None:
        immediate_width = 4 if instruction.immediate > 0xFFFF else 2
    if instruction.family == "call":
        code_bytes = 4 + immediate_width
    else:
        code_bytes = 2 + immediate_width
    code_bytes += len(instruction.reads) * read_width
    code_bytes += len(instruction.writes) * write_width
    code_bytes += len(instruction.targets) * target_width
    if wide:
        code_bytes += 2
    reasons = _overflow_reasons(
        read_register, write_register, constants, width_overflow
    )
    if count_overflow:
        reasons.extend(_shape_reason_flags(instruction))
        if not reasons:
            reasons.append("call_arity_overflow")
    return EncodingMeasurement(
        code_bytes=_align2(code_bytes),
        wide=wide,
        read_register_overflows=read_register,
        write_register_overflows=write_register,
        constant_overflows=constants,
        extension_reasons=tuple(dict.fromkeys(reasons)),
    )


def _codeunit16(instruction: Instruction) -> EncodingMeasurement:
    read_register, write_register, constants = _index_overflows(
        instruction, 0x7FFF, 0xFFFF
    )
    units = 1
    for operand in instruction.reads:
        limit = 0x7FFF
        units += 1 if operand.index <= limit else 3
    for register in instruction.writes:
        units += 1 if register <= 0xFFFF else 3
    width_overflow = False
    for target in instruction.targets:
        if -0x8000 <= target <= 0x7FFF:
            units += 1
        else:
            units += 3
            width_overflow = True
    if instruction.immediate is not None:
        if instruction.immediate <= 0xFFFF:
            units += 1
        else:
            units += 3
            width_overflow = True
    wide = units > 4
    reasons: list[str] = []
    if wide:
        reasons.extend(_shape_reason_flags(instruction))
        reasons.extend(
            _overflow_reasons(
                read_register, write_register, constants, width_overflow
            )
        )
        if not reasons:
            reasons.append("more_than_four_code_units")
    return EncodingMeasurement(
        code_bytes=units * 2,
        wide=wide,
        read_register_overflows=read_register,
        write_register_overflows=write_register,
        constant_overflows=constants,
        extension_reasons=tuple(dict.fromkeys(reasons)),
    )


CANDIDATES = {
    "codeunit16": _codeunit16,
    "fixed8": _fixed8,
    "opcode_specific": _opcode_specific,
}


def _ratio(part: int, total: int) -> tuple[float, float]:
    if not total:
        return 0.0, 0.0
    ratio = part / total
    return round(ratio, 6), round(ratio * 100, 2)


def _metadata_for_function(function: Function) -> dict[str, Any]:
    instruction_count = len(function.instructions)
    bitmap_bytes = (instruction_count + 7) // 8
    known_sources = sum(
        instruction.source_id is not None for instruction in function.instructions
    )
    effects = sum(instruction.effect for instruction in function.instructions)
    safepoints = sum(instruction.safepoint for instruction in function.instructions)
    side_exits = sum(len(instruction.side_exits) for instruction in function.instructions)
    source_pc_bytes = instruction_count * 8
    effect_bytes = bitmap_bytes
    safepoint_bytes = bitmap_bytes
    side_exit_bytes = side_exits * 12
    return {
        "effect_map": {
            "bytes": effect_bytes,
            "set_bits": effects,
        },
        "safepoint_map": {
            "bytes": safepoint_bytes,
            "set_bits": safepoints,
        },
        "side_exit_map": {
            "bytes": side_exit_bytes,
            "entries": side_exits,
        },
        "source_pc_map": {
            "bytes": source_pc_bytes,
            "known_source_entries": known_sources,
            "unknown_source_entries": instruction_count - known_sources,
        },
        "total_bytes": source_pc_bytes
        + effect_bytes
        + safepoint_bytes
        + side_exit_bytes,
    }


def _sum_metadata(items: Iterable[dict[str, Any]]) -> dict[str, Any]:
    metadata = list(items)
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


def _candidate_report(corpus: Corpus, encoder: Any) -> dict[str, Any]:
    totals = {
        "code_bytes": 0,
        "narrow_instructions": 0,
        "wide_instructions": 0,
        "read_register": 0,
        "write_register": 0,
        "constant": 0,
    }
    reason_totals: dict[str, int] = {reason: 0 for reason in CORE_EXTENSION_REASONS}
    per_function: list[dict[str, Any]] = []
    for function in corpus.functions:
        function_totals = {
            "code_bytes": 0,
            "narrow_instructions": 0,
            "wide_instructions": 0,
            "read_register": 0,
            "write_register": 0,
            "constant": 0,
        }
        function_reasons: dict[str, int] = {reason: 0 for reason in CORE_EXTENSION_REASONS}
        for instruction in function.instructions:
            measured = encoder(instruction)
            function_totals["code_bytes"] += measured.code_bytes
            if measured.wide:
                function_totals["wide_instructions"] += 1
            else:
                function_totals["narrow_instructions"] += 1
            function_totals["read_register"] += measured.read_register_overflows
            function_totals["write_register"] += measured.write_register_overflows
            function_totals["constant"] += measured.constant_overflows
            for reason in measured.extension_reasons:
                function_reasons[reason] = function_reasons.get(reason, 0) + 1
        for key in totals:
            totals[key] += function_totals[key]
        for reason, count in function_reasons.items():
            reason_totals[reason] = reason_totals.get(reason, 0) + count
        instruction_count = len(function.instructions)
        narrow_ratio, narrow_percent = _ratio(
            function_totals["narrow_instructions"], instruction_count
        )
        wide_ratio, wide_percent = _ratio(
            function_totals["wide_instructions"], instruction_count
        )
        per_function.append(
            {
                "code_bytes": function_totals["code_bytes"],
                "extension_instructions_by_reason": dict(sorted(function_reasons.items())),
                "function": function.name,
                "index_overflows": {
                    "constant": function_totals["constant"],
                    "read_register": function_totals["read_register"],
                    "register": function_totals["read_register"]
                    + function_totals["write_register"],
                    "write_register": function_totals["write_register"],
                },
                "instructions": instruction_count,
                "narrow_instructions": function_totals["narrow_instructions"],
                "narrow_percent": narrow_percent,
                "narrow_ratio": narrow_ratio,
                "wide_instructions": function_totals["wide_instructions"],
                "wide_percent": wide_percent,
                "wide_ratio": wide_ratio,
            }
        )
    instruction_count = sum(len(function.instructions) for function in corpus.functions)
    narrow_ratio, narrow_percent = _ratio(totals["narrow_instructions"], instruction_count)
    wide_ratio, wide_percent = _ratio(totals["wide_instructions"], instruction_count)
    return {
        "code_bytes": totals["code_bytes"],
        "extension_instructions_by_reason": dict(sorted(reason_totals.items())),
        "index_overflows": {
            "constant": totals["constant"],
            "read_register": totals["read_register"],
            "register": totals["read_register"] + totals["write_register"],
            "write_register": totals["write_register"],
        },
        "instructions": instruction_count,
        "narrow_instructions": totals["narrow_instructions"],
        "narrow_percent": narrow_percent,
        "narrow_ratio": narrow_ratio,
        "per_function": per_function,
        "wide_instructions": totals["wide_instructions"],
        "wide_percent": wide_percent,
        "wide_ratio": wide_ratio,
    }


def build_report(corpus: Corpus) -> dict[str, Any]:
    function_metadata = [
        {
            "function": function.name,
            "instructions": len(function.instructions),
            "metadata": _metadata_for_function(function),
        }
        for function in corpus.functions
    ]
    metadata = _sum_metadata(item["metadata"] for item in function_metadata)
    instruction_count = sum(len(function.instructions) for function in corpus.functions)
    candidates = {
        name: _candidate_report(corpus, encoder)
        for name, encoder in sorted(CANDIDATES.items())
    }
    for candidate in candidates.values():
        candidate["code_plus_metadata_bytes"] = candidate["code_bytes"] + metadata["total_bytes"]
    return {
        "assumptions": ASSUMPTIONS,
        "candidates": candidates,
        "corpus": {
            "fixture_only": corpus.fixture_only,
            "labels": list(corpus.labels),
            "provenance": list(corpus.provenance),
        },
        "functions": function_metadata,
        "metadata": metadata,
        "schema": REPORT_SCHEMA,
        "totals": {
            "functions": len(corpus.functions),
            "instructions": instruction_count,
        },
    }


def render_report(report: dict[str, Any], compact: bool = False) -> str:
    if compact:
        return json.dumps(report, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    return json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2) + "\n"


def _parser() -> argparse.ArgumentParser:
    return argparse.ArgumentParser(
        description=(
            "Compare candidate BinIR instruction encodings over normalized JSON "
            "corpora. This is a deterministic size probe, not a production encoder."
        ),
        epilog=(
            "Input assumptions: reads are {kind: reg|const, index}; writes are RegIds; "
            "branch targets are relative instruction offsets; call immediate is a "
            "callee index. fixed8/opcode_specific tagged reads have only 128 short "
            "RegId/ConstId values. Metadata costs are always reported. To measure real "
            "layout output, export each runtimec2 Segment in s0 order to the normalized "
            "schema documented in this module; do not label fixture data as project "
            "coverage."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = _parser()
    parser.add_argument(
        "inputs",
        nargs="+",
        type=Path,
        help="JSON corpus file or directory (directories are scanned recursively)",
    )
    parser.add_argument(
        "--compact",
        action="store_true",
        help="emit canonical one-line JSON instead of indented canonical JSON",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="write the report to this UTF-8 file instead of stdout",
    )
    args = parser.parse_args(argv)
    try:
        report = build_report(load_corpus(args.inputs))
        rendered = render_report(report, compact=args.compact)
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
    except ProbeError as error:
        parser.exit(2, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
