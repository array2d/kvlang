#!/usr/bin/env python3
"""Generate and verify the two-level myrwircap manifest.

Level 1 — `specification/rwir_catalog.def`: capability rows. One row per **semantic
operator**, carrying a read/write TypeSet bitmask, an operand constraint, a result rule,
an alias policy and the behavior-decision list that governs it.

Level 2 — `specification/rwir_alias_table.def`: surface name -> capability row. The 316
names registered by `runtime/src/myrwircaps.c` are collapsed by type family and by alias;
the four compiler-selected specializations (`concat`, `streq`, `ptreq`, `bool·eq`) are not
registered names and are declared separately.

Coverage scope: **only the C runtime's `myrwircaps`**. byteseek is an extension runtime
whose in-process opcodes are deliberately not represented here (decision #622 D); neither
are stdlib `def rwir` declarations written by layout.

The manifest is the single source of capability identity for runtimec. This tool only
*reads* the registered capability set and the five-language behavior matrix; it never
invents a language behavior. A capability whose semantics has no matrix decision is
emitted into the gap list, not into the manifest.

Gate (non-zero exit on drift):

    python3 runtimec/tools/architecture/generate_rwir_catalog.py --check --require-complete \\
        --root <repo>
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SPEC = Path("runtimec/specification")
REGISTRY_SOURCE = Path("runtime/src/myrwircaps.c")
MANIFEST = SPEC / "rwir_catalog.def"
ALIAS_TABLE = SPEC / "rwir_alias_table.def"
GAPS = SPEC / "rwir_catalog_gaps.md"
# 第四份产物：layout 在构建期消费的签名表。layout 不访问 kvspace（用户裁决），
# 所以 native rwir 的返回类型只能从这份生成表里读，不能运行期查 `/lib/<op>`。
SIGNATURE_TABLE = SPEC / "native_rwir_decls.kv"
# 产物清单是封闭集合：--check 必须逐份比对，少一份就红。否则某份产物可以从校验
# 里静默消失，而生成器照样打印 OK。
ARTIFACTS = (MANIFEST, ALIAS_TABLE, GAPS, SIGNATURE_TABLE)
MATRIX = SPEC / "five-language-behavior-matrix.md"
INSTRUCTION_HEADER = Path("runtimec/binrwir/instruction.h")

MIDDLE_DOT = "·"

# ---------------------------------------------------------------------------------------
# TypeSet: bit index -> kind. 0..17 keep the #583 §4.7 mapping; 18 is the pointer category,
# which the §4.7 snapshot (taken from runtimec2's LangType) did not have. `None` is the
# empty mask, never a member. Bits above 18 are reserved and must stay zero.
# ---------------------------------------------------------------------------------------
KIND_BITS = {
    "int8": 0, "int16": 1, "int32": 2, "int64": 3,
    "uint8": 4, "uint16": 5, "uint32": 6, "uint64": 7,
    "float32": 8, "float64": 9, "bool": 10,
    "char/utf8": 11, "char/utf32": 12, "char/ascii": 13,
    "array": 14, "stringkeymap": 15, "structref": 16, "ext": 17,
    "pointer": 18,
}
TYPE_SET_WIDTH = 19
TYPE_SET_LIMIT = 1 << TYPE_SET_WIDTH


def type_set(*kinds: str) -> int:
    value = 0
    for kind in kinds:
        value |= 1 << KIND_BITS[kind]
    return value


SIGNED_INTEGER = type_set("int8", "int16", "int32", "int64")
UNSIGNED_INTEGER = type_set("uint8", "uint16", "uint32", "uint64")
INTEGER = SIGNED_INTEGER | UNSIGNED_INTEGER
FLOAT = type_set("float32", "float64")
NUMBER = INTEGER | FLOAT
BOOLEAN = type_set("bool")
CHARACTER_UTF8 = type_set("char/utf8")
CHARACTER = type_set("char/utf8", "char/utf32", "char/ascii")

WIDE_INTEGER = type_set("int64", "uint64")
LOGICAL = NUMBER | BOOLEAN
CAST_SOURCE = NUMBER | BOOLEAN | CHARACTER
ARRAY = type_set("array")
STRING_KEY_MAP = type_set("stringkeymap")
STRUCT_REF = type_set("structref")
POINTER = type_set("pointer")
FLOAT64 = type_set("float64")

# 签名表允许出现的名字型 langtype。取自 layout/src/langtype.rs 的 known_kind：
# 列在这里的是**校验集**，不是事实源——写少了会误报，写多了会让生成器放过 layout
# 认不出的名字。time 族今天只用 time、time/duration、int64、bool，扩展时同步。
SIGNATURE_LANGTYPES = ("time", "time/duration", "int64", "bool")
# 类型串直接嵌进声明文本：逗号会切断参数表、竖线与引号会改变语义。这些字符一出现，
# 生成物就会被 layout 解析成另一件事，而肉眼看着仍然像一条合法声明。
FORBIDDEN_LANGTYPE_CHARACTERS = (",", "|", '"', "'", " ", "\t", "(", ")")

TYPE_SET_NAMES = {
    SIGNED_INTEGER: "signed integer",
    UNSIGNED_INTEGER: "unsigned integer",
    INTEGER: "integer",
    FLOAT: "float",
    NUMBER: "number",
    BOOLEAN: "boolean",
    CHARACTER_UTF8: "char/utf8",
    CHARACTER: "character",
    WIDE_INTEGER: "64-bit integer",
    LOGICAL: "number or boolean",
    CAST_SOURCE: "cast source scalar",
    ARRAY: "array",
    STRING_KEY_MAP: "stringkeymap",
    STRUCT_REF: "structref",
    POINTER: "pointer",
    FLOAT64: "float64",
}

# Constraint / result / alias vocabulary of the record. No fifth value may appear.
CONSTRAINTS = ("NONE", "SAME_CLASS", "NUMERIC_COMPATIBLE", "NUMERIC_COMPARE")
RESULT_RULES = ("NONE", "SAME_FIRST", "SAME_SECOND", "WIDEN", "WIDEN_INTEGER",
                "BOOLEAN", "FLOAT64", "TARGET_TYPE", "IDENTITY", "CHARACTER_UTF8")
ALIAS_POLICIES = ("FORBIDDEN", "DECLARED")


@dataclass(frozen=True)
class Row:
    identifier: str
    operator: str
    opid: int
    read_mask: int
    write_mask: int
    constraint: str
    result_rule: str
    alias_policy: str
    decisions: tuple[str, ...]
    # `value` rows are on the scalar union channel; `parameterized` and `control` rows are
    # not, and their mask must stay zero unless a category bit is stated explicitly.
    channel: str
    # 名字型类型（`time` / `time/duration`）：TypeSet 位表达不了它们，因为它们是 langtype 的
    # 名字而不是宽度。空串 = 该行不声明，由 `/lib/<op>` 的安装签名给出。
    read_langtypes: tuple[str, ...] = ()
    write_langtypes: tuple[str, ...] = ()


# ---------------------------------------------------------------------------------------
# Level 1a: the 29 value operators (#583 §4.8), Copy, the control operators, debugger and
# the object literal. opid is explicit: it never derives from table position.
# ---------------------------------------------------------------------------------------
VALUE_ROWS = (
    # identifier suffix          operator  opid   read      write     constraint           result          alias      decisions
    ("COPY", "copy", 0x0001, 0, 0, "NONE", "IDENTITY", "DECLARED", ("read-write-alias",), "parameterized"),
    ("ADD", "add", 0x0002, NUMBER, NUMBER, "NUMERIC_COMPATIBLE", "WIDEN", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent",), "value"),
    ("SUB", "sub", 0x0003, NUMBER, NUMBER, "NUMERIC_COMPATIBLE", "WIDEN", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent",), "value"),
    ("MUL", "mul", 0x0004, NUMBER, NUMBER, "NUMERIC_COMPATIBLE", "WIDEN", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent",), "value"),
    ("DIV", "div", 0x0005, NUMBER, NUMBER, "NUMERIC_COMPATIBLE", "WIDEN", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent", "signed-integer-division",
      "float-division-by-zero"), "value"),
    ("MOD", "mod", 0x0006, INTEGER, INTEGER, "SAME_CLASS", "WIDEN_INTEGER", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent", "integer-remainder-sign"), "value"),
    ("MAX", "max", 0x0007, NUMBER, NUMBER, "NUMERIC_COMPATIBLE", "WIDEN", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent", "floating-min-max-nan"), "value"),
    ("MIN", "min", 0x0008, NUMBER, NUMBER, "NUMERIC_COMPATIBLE", "WIDEN", "FORBIDDEN",
     ("mixed-precision-promotion-order-independent", "floating-min-max-nan"), "value"),
    # #622 4.1.6 / decision `unsigned-neg-abs-sign`: unsigned participates in the union, so
    # `neg` wraps at the width, `abs` is identity on unsigned, and `sign(UINT64_MAX)` is
    # positive. ResultRule SAME_FIRST keeps the operand's kind.
    ("ABSOLUTE", "abs", 0x0009, NUMBER, NUMBER, "NONE", "SAME_FIRST", "FORBIDDEN",
     ("signed-neg-abs-sign", "unsigned-neg-abs-sign"), "value"),
    ("NEGATION", "neg", 0x000A, NUMBER, NUMBER, "NONE", "SAME_FIRST", "FORBIDDEN",
     ("signed-neg-abs-sign", "unsigned-neg-abs-sign"), "value"),
    ("SIGN", "sign", 0x000B, NUMBER, NUMBER, "NONE", "SAME_FIRST", "FORBIDDEN",
     ("signed-neg-abs-sign", "unsigned-neg-abs-sign"), "value"),
    ("BITWISE_AND", "bitand", 0x000C, WIDE_INTEGER, WIDE_INTEGER, "SAME_CLASS", "SAME_FIRST",
     "FORBIDDEN", ("mixed-signed-unsigned-arithmetic",), "value"),
    ("BITWISE_OR", "bitor", 0x000D, WIDE_INTEGER, WIDE_INTEGER, "SAME_CLASS", "SAME_FIRST",
     "FORBIDDEN", ("mixed-signed-unsigned-arithmetic",), "value"),
    ("BITWISE_XOR", "bitxor", 0x000E, WIDE_INTEGER, WIDE_INTEGER, "SAME_CLASS", "SAME_FIRST",
     "FORBIDDEN", ("mixed-signed-unsigned-arithmetic",), "value"),
    ("SHIFT_LEFT", "shl", 0x000F, WIDE_INTEGER, WIDE_INTEGER, "SAME_CLASS", "SAME_FIRST",
     "FORBIDDEN", ("shift-count-masking", "mixed-signed-unsigned-arithmetic"), "value"),
    ("SHIFT_RIGHT", "shr", 0x0010, WIDE_INTEGER, WIDE_INTEGER, "SAME_CLASS", "SAME_FIRST",
     "FORBIDDEN", ("shift-count-masking", "mixed-signed-unsigned-arithmetic"), "value"),
    ("SQUARE_ROOT", "sqrt", 0x0011, NUMBER, FLOAT64, "NONE", "FLOAT64", "FORBIDDEN",
     ("float-transcendental-result-kind",), "value"),
    ("EXPONENTIAL", "exp", 0x0012, NUMBER, FLOAT64, "NONE", "FLOAT64", "FORBIDDEN",
     ("float-transcendental-result-kind",), "value"),
    ("LOGARITHM", "log", 0x0013, NUMBER, FLOAT64, "NONE", "FLOAT64", "FORBIDDEN",
     ("float-transcendental-result-kind",), "value"),
    ("POWER", "pow", 0x0014, NUMBER, FLOAT64, "NONE", "FLOAT64", "FORBIDDEN",
     ("float-transcendental-result-kind",), "value"),
    ("EQUAL", "eq", 0x0015, NUMBER, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("mixed-numeric-equality",), "value"),
    ("NOT_EQUAL", "neq", 0x0016, NUMBER, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("mixed-numeric-equality",), "value"),
    ("LESS", "lt", 0x0017, NUMBER, BOOLEAN, "NUMERIC_COMPARE", "BOOLEAN", "FORBIDDEN",
     ("mixed-numeric-equality", "mixed-signed-unsigned-arithmetic"), "value"),
    ("LESS_EQUAL", "le", 0x0018, NUMBER, BOOLEAN, "NUMERIC_COMPARE", "BOOLEAN", "FORBIDDEN",
     ("mixed-numeric-equality", "mixed-signed-unsigned-arithmetic"), "value"),
    ("GREATER", "gt", 0x0019, NUMBER, BOOLEAN, "NUMERIC_COMPARE", "BOOLEAN", "FORBIDDEN",
     ("mixed-numeric-equality", "mixed-signed-unsigned-arithmetic"), "value"),
    ("GREATER_EQUAL", "ge", 0x001A, NUMBER, BOOLEAN, "NUMERIC_COMPARE", "BOOLEAN",
     "FORBIDDEN", ("mixed-numeric-equality", "mixed-signed-unsigned-arithmetic"), "value"),
    ("LOGICAL_AND", "and", 0x001B, LOGICAL, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("logical-operators-truthy",), "value"),
    ("LOGICAL_OR", "or", 0x001C, LOGICAL, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("logical-operators-truthy",), "value"),
    ("LOGICAL_NOT", "not", 0x001D, LOGICAL, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("logical-operators-truthy",), "value"),
    ("CAST", "cast", 0x001E, CAST_SOURCE, CAST_SOURCE, "NONE", "TARGET_TYPE", "FORBIDDEN",
     ("cast-target-kind", "floating-to-integer-truncation", "int-to-float32-single-rounding",
      "uint64-to-float-promotion", "bool-cast-strict", "character-conversion-result"),
     "value"),
    # Compiler-selected specializations (#583 §4.8, 2026-10-31 split). No registered surface
    # name resolves to these rows as its primary capability.
    ("CONCAT", "concat", 0x001F, CHARACTER, CHARACTER_UTF8, "SAME_CLASS", "CHARACTER_UTF8",
     "FORBIDDEN", ("character-concat-kind",), "value"),
    ("CHARACTER_EQUAL", "streq", 0x0020, CHARACTER, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("character-equality-by-text",), "value"),
    ("POINTER_EQUAL", "ptreq", 0x0021, POINTER, BOOLEAN, "SAME_CLASS", "BOOLEAN", "FORBIDDEN",
     ("pointer-equality",), "value"),
    ("BOOLEAN_EQUAL", "bool·eq", 0x0022, BOOLEAN, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("boolean-equality",), "value"),
    # `!=` 的特化不能再靠 invert 位：BinRWIR 指令头只有 opid。三类操作数各有一条独立行，
    # 结果直接是取反后的布尔值（裁决 decision-encoder-gaps 第 3 条）。
    ("CHARACTER_NOT_EQUAL", "streq·not", 0x0029, CHARACTER, BOOLEAN, "NONE", "BOOLEAN",
     "FORBIDDEN", ("character-equality-by-text",), "value"),
    ("POINTER_NOT_EQUAL", "ptreq·not", 0x002A, POINTER, BOOLEAN, "SAME_CLASS", "BOOLEAN",
     "FORBIDDEN", ("pointer-equality",), "value"),
    ("BOOLEAN_NOT_EQUAL", "bool·neq", 0x002B, BOOLEAN, BOOLEAN, "NONE", "BOOLEAN",
     "FORBIDDEN", ("boolean-equality",), "value"),
    ("DEBUGGER", "debugger", 0x0023, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("debugger-pauses-execution",), "control"),
    ("CALL", "call", 0x0024, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("RETURN", "return", 0x0025, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("GOTO", "goto", 0x0026, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("BRANCH", "branch", 0x0027, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("OBJECT_LITERAL", "object", 0x0028, STRING_KEY_MAP, STRING_KEY_MAP, "NONE", "IDENTITY",
     "DECLARED", ("map-literal-replaces-members",), "value"),
)

# ---------------------------------------------------------------------------------------
# Level 1b: the named families. One row per `<family>·<operation>`; masks stay zero because
# the operands and the result are declared per installation by the `/lib/<operator>` RWIR
# signature slots, which mapping rule 1 forbids duplicating here. Rows that produce or
# rewrite a *category* value state that category bit explicitly.
# ---------------------------------------------------------------------------------------
SCALAR_KINDS = ("int8", "int16", "int32", "int64", "uint8", "uint16",
                "uint32", "uint64", "float32", "float64")
CAST_TARGETS = SCALAR_KINDS + ("bool", "char/utf8", "char/utf32", "char/ascii")


@dataclass(frozen=True)
class FamilyRow:
    family: str
    operation: str
    opid: int
    decision: str
    read_mask: int = 0
    write_mask: int = 0
    alias_policy: str = "FORBIDDEN"
    # 名字型类型：`time` / `time/duration` 没有对应的 TypeSet 位（它们是 langtype 的名字，
    # 不是宽度），用逗号分隔的 langtype 名表达。缺省为空 = 该行不声明签名，
    # 由 `/lib/<op>` 的安装签名给出。
    read_langtypes: str = ""
    write_langtypes: str = ""


# alias policy: only the writers that rewrite their own destination in place may alias a
# read parameter with the write parameter (matrix decision `read-write-alias`).
DECLARED_WRITERS = {
    ("compactarray", "set"), ("compactarray", "fill"),
    ("scatterarray", "set"), ("scatterarray", "append"),
    ("string", "set"), ("xv", "set"),
    ("map", "new"), ("struct", "new"),
}

FAMILY_ROWS = (
    # compactarray
    FamilyRow("compactarray", "copy", 0x0100, "array-copy-result-kind"),
    FamilyRow("compactarray", "newslice", 0x0101, "string-slice-bounds"),
    FamilyRow("compactarray", "new", 0x0102, "array-element-access-by-kind"),
    FamilyRow("compactarray", "fill", 0x0103, "array-fill-write-param"),
    FamilyRow("compactarray", "numel", 0x0104, "array-element-access-by-kind"),
    FamilyRow("compactarray", "dim", 0x0105, "array-element-access-by-kind"),
    FamilyRow("compactarray", "shape", 0x0106, "array-element-access-by-kind"),
    FamilyRow("compactarray", "at", 0x0107, "array-element-access-by-kind"),
    FamilyRow("compactarray", "set", 0x0108, "array-element-access-by-kind"),
    FamilyRow("compactarray", "newreshape", 0x0109, "array-reshape-numel-mismatch"),
    FamilyRow("compactarray", "newreinterpret", 0x010A, "array-reinterpret-raw"),
    FamilyRow("compactarray", "langtype", 0x010B, "xv-introspection-result-kind"),
    FamilyRow("compactarray", "bodylen", 0x010C, "xv-introspection-result-kind"),
    FamilyRow("compactarray", "parselangtype", 0x010D, "xv-introspection-result-kind"),
    # scatterarray
    FamilyRow("scatterarray", "new", 0x0110, "scatterarray-next-index"),
    FamilyRow("scatterarray", "len", 0x0111, "scatterarray-next-index"),
    FamilyRow("scatterarray", "at", 0x0112, "scatterarray-next-index"),
    FamilyRow("scatterarray", "set", 0x0113, "scatterarray-next-index"),
    FamilyRow("scatterarray", "newscatter", 0x0114, "scatterarray-next-index"),
    FamilyRow("scatterarray", "newcompact", 0x0115, "scatterarray-next-index"),
    FamilyRow("scatterarray", "append", 0x0116, "scatterarray-next-index"),
    FamilyRow("scatterarray", "slice", 0x0117, "string-slice-bounds"),
    # string
    FamilyRow("string", "set", 0x0120, "string-codepoint-indexing"),
    FamilyRow("string", "char", 0x0121, "string-codepoint-indexing"),
    FamilyRow("string", "ord", 0x0122, "string-codepoint-indexing"),
    FamilyRow("string", "cmp", 0x0123, "string-compare-ordering"),
    FamilyRow("string", "find", 0x0124, "string-codepoint-indexing"),
    FamilyRow("string", "len", 0x0125, "string-codepoint-indexing"),
    FamilyRow("string", "slice", 0x0126, "string-slice-bounds"),
    FamilyRow("string", "concat", 0x0127, "string-concat-result-kind"),
    FamilyRow("string", "formatint", 0x0128, "string-format-base-radix"),
    FamilyRow("string", "formatuint", 0x0129, "string-format-base-radix"),
    FamilyRow("string", "parseint", 0x012A, "string-parse-strict-full-consumption"),
    FamilyRow("string", "parseuint", 0x012B, "string-parse-strict-full-consumption"),
    FamilyRow("string", "parsefloat", 0x012C, "string-parse-strict-full-consumption"),
    # xvalue
    FamilyRow("xv", "at", 0x0130, "array-element-access-by-kind"),
    FamilyRow("xv", "set", 0x0131, "array-element-access-by-kind"),
    FamilyRow("xv", "reshape", 0x0132, "array-reshape-numel-mismatch"),
    FamilyRow("xv", "reinterpret", 0x0133, "array-reinterpret-raw"),
    FamilyRow("xv", "langtype", 0x0134, "xv-introspection-result-kind"),
    FamilyRow("xv", "bodylen", 0x0135, "xv-introspection-result-kind"),
    FamilyRow("xv", "parselangtype", 0x0136, "xv-introspection-result-kind"),
    # ndarray
    FamilyRow("ndarray", "numel", 0x0140, "array-element-access-by-kind"),
    FamilyRow("ndarray", "dim", 0x0141, "array-element-access-by-kind"),
    FamilyRow("ndarray", "shape", 0x0142, "array-element-access-by-kind"),
    # time：签名来自 runtime/src/rwir_time.c 的实现（不是推断）。
    #   现在 → time；time·sub(time,time) → time/duration；time·add(time,time/duration) → time；
    #   before/after(time,time) → bool。
    FamilyRow("time", "now", 0x0150, "time-realtime-nanoseconds",
              read_langtypes="", write_langtypes="time"),
    FamilyRow("time", "sub", 0x0151, "time-arithmetic-result",
              read_langtypes="time,time", write_langtypes="time/duration"),
    FamilyRow("time", "add", 0x0152, "time-arithmetic-result",
              read_langtypes="time,time/duration", write_langtypes="time"),
    FamilyRow("time", "before", 0x0153, "time-arithmetic-result",
              read_langtypes="time,time", write_langtypes="bool"),
    FamilyRow("time", "after", 0x0154, "time-arithmetic-result",
              read_langtypes="time,time", write_langtypes="bool"),
    # time/duration：整数进、time/duration 出，或 time/duration 进、int64/bool 出。
    FamilyRow("time/duration", "nanos", 0x0160, "duration-overflow-wrap",
              read_langtypes="int64", write_langtypes="time/duration"),
    FamilyRow("time/duration", "millis", 0x0161, "duration-overflow-wrap",
              read_langtypes="int64", write_langtypes="time/duration"),
    FamilyRow("time/duration", "seconds", 0x0162, "duration-overflow-wrap",
              read_langtypes="int64", write_langtypes="time/duration"),
    FamilyRow("time/duration", "minutes", 0x0163, "duration-overflow-wrap",
              read_langtypes="int64", write_langtypes="time/duration"),
    FamilyRow("time/duration", "hours", 0x0164, "duration-overflow-wrap",
              read_langtypes="int64", write_langtypes="time/duration"),
    FamilyRow("time/duration", "as_nanos", 0x0165, "duration-truncating-scale",
              read_langtypes="time/duration", write_langtypes="int64"),
    FamilyRow("time/duration", "as_millis", 0x0166, "duration-truncating-scale",
              read_langtypes="time/duration", write_langtypes="int64"),
    FamilyRow("time/duration", "as_seconds", 0x0167, "duration-truncating-scale",
              read_langtypes="time/duration", write_langtypes="int64"),
    FamilyRow("time/duration", "as_minutes", 0x0168, "duration-truncating-scale",
              read_langtypes="time/duration", write_langtypes="int64"),
    FamilyRow("time/duration", "as_hours", 0x0169, "duration-truncating-scale",
              read_langtypes="time/duration", write_langtypes="int64"),
    FamilyRow("time/duration", "add", 0x016A, "duration-overflow-wrap",
              read_langtypes="time/duration,time/duration", write_langtypes="time/duration"),
    FamilyRow("time/duration", "sub", 0x016B, "duration-overflow-wrap",
              read_langtypes="time/duration,time/duration", write_langtypes="time/duration"),
    FamilyRow("time/duration", "before", 0x016C, "duration-truncating-scale",
              read_langtypes="time/duration,time/duration", write_langtypes="bool"),
    FamilyRow("time/duration", "after", 0x016D, "duration-truncating-scale",
              read_langtypes="time/duration,time/duration", write_langtypes="bool"),
    # kvspace
    FamilyRow("kvspace", "get", 0x0170, "kvspace-get-missing-key"),
    FamilyRow("kvspace", "set", 0x0171, "kvspace-set-write-scope"),
    FamilyRow("kvspace", "del", 0x0172, "kvspace-set-write-scope"),
    FamilyRow("kvspace", "deltree", 0x0173, "kvspace-set-write-scope"),
    FamilyRow("kvspace", "cp", 0x0174, "kvspace-copy-semantics"),
    FamilyRow("kvspace", "cpdir", 0x0175, "kvspace-copy-semantics"),
    FamilyRow("kvspace", "cplist", 0x0176, "kvspace-copy-semantics"),
    FamilyRow("kvspace", "listlen", 0x0177, "kvspace-list-ordering"),
    FamilyRow("kvspace", "listat", 0x0178, "kvspace-list-ordering"),
    FamilyRow("kvspace", "mkindex", 0x0179, "kvspace-index-capacity"),
    FamilyRow("kvspace", "extindex", 0x017A, "kvspace-extension-index"),
    FamilyRow("kvspace", "rmindexext", 0x017B, "kvspace-extension-index"),
    FamilyRow("kvspace", "watch", 0x017C, "kvspace-watch-blocking"),
    # vthread
    FamilyRow("vthread", "create", 0x0180, "vthread-control-opcodes"),
    FamilyRow("vthread", "run", 0x0181, "vthread-control-opcodes"),
    FamilyRow("vthread", "call", 0x0182, "vthread-control-opcodes"),
    FamilyRow("vthread", "sleep", 0x0183, "vthread-control-opcodes"),
    FamilyRow("vthread", "setstatus", 0x0184, "vthread-control-opcodes"),
    # random
    FamilyRow("random", "uint64", 0x0190, "random-source-and-range"),
    FamilyRow("random", "int63", 0x0191, "random-source-and-range"),
    FamilyRow("random", "intn", 0x0192, "random-source-and-range"),
    # map / struct / kvlang
    FamilyRow("map", "new", 0x01A0, "map-literal-replaces-members",
              STRING_KEY_MAP, STRING_KEY_MAP),
    FamilyRow("struct", "new", 0x01A1, "struct-new-from-prototype",
              STRUCT_REF, STRUCT_REF),
    FamilyRow("kvlang", "abs", 0x01A2, "address-of-yields-pointer", 0, POINTER),
)

# ---------------------------------------------------------------------------------------
# Surface name -> capability row. Only these tables decide the collapse; there is no second
# signature dictionary anywhere in the tree.
# ---------------------------------------------------------------------------------------
SYMBOLIC_ALIASES = {
    "!": "LOGICAL_NOT", "&&": "LOGICAL_AND", "||": "LOGICAL_OR",
    "+": "ADD", "-": "SUB", "×": "MUL", "÷": "DIV", "%": "MOD",
    "&": "BITWISE_AND", "|": "BITWISE_OR", "^": "BITWISE_XOR",
    "<<": "SHIFT_LEFT", ">>": "SHIFT_RIGHT",
    "√": "SQUARE_ROOT",
    "==": "EQUAL", "!=": "NOT_EQUAL", "≠": "NOT_EQUAL",
    "<": "LESS", "<=": "LESS_EQUAL", "≤": "LESS_EQUAL",
    ">": "GREATER", ">=": "GREATER_EQUAL", "≥": "GREATER_EQUAL",
}

BARE_ALIASES = {
    "=": "COPY",
    "br": "BRANCH",
    "obj": "OBJECT_LITERAL",
    "debugger": "DEBUGGER",
    "call": "CALL", "return": "RETURN", "goto": "GOTO",
}

# A `<kind>·<operation>` name is the type specialization of one value row.
KIND_SPECIALIZED_OPERATIONS = {
    "add": "ADD", "sub": "SUB", "mul": "MUL", "div": "DIV", "mod": "MOD",
    "max": "MAX", "min": "MIN", "abs": "ABSOLUTE", "neg": "NEGATION", "sign": "SIGN",
    "bitand": "BITWISE_AND", "bitor": "BITWISE_OR", "bitxor": "BITWISE_XOR",
    "shl": "SHIFT_LEFT", "shr": "SHIFT_RIGHT",
    "sqrt": "SQUARE_ROOT", "exp": "EXPONENTIAL", "log": "LOGARITHM", "pow": "POWER",
    "eq": "EQUAL", "neq": "NOT_EQUAL",
    "lt": "LESS", "le": "LESS_EQUAL", "gt": "GREATER", "ge": "GREATER_EQUAL",
}

# (surface, first operand mask, second operand mask, invert, row). Every entry is a
# selection: the row itself already carries the semantics, so no selection inverts.
SPECIALIZATIONS = (
    ("+", CHARACTER, CHARACTER, False, "CONCAT"),
    ("==", CHARACTER, CHARACTER, False, "CHARACTER_EQUAL"),
    ("!=", CHARACTER, CHARACTER, False, "CHARACTER_NOT_EQUAL"),
    ("==", POINTER, POINTER, False, "POINTER_EQUAL"),
    ("!=", POINTER, POINTER, False, "POINTER_NOT_EQUAL"),
    ("==", BOOLEAN, BOOLEAN, False, "BOOLEAN_EQUAL"),
    ("!=", BOOLEAN, BOOLEAN, False, "BOOLEAN_NOT_EQUAL"),
)

# Identifier word expansion: p8 forbids abbreviations, so every family operation word gets a
# written-out identifier word. A word missing here is a hard error, never a silent default.
IDENTIFIER_WORDS = {
    "at": "AT", "set": "SET", "copy": "COPY", "new": "NEW", "fill": "FILL",
    "newslice": "NEW_SLICE", "numel": "NUMEL", "dim": "DIMENSION", "shape": "SHAPE",
    "newreshape": "NEW_RESHAPE", "newreinterpret": "NEW_REINTERPRET",
    "langtype": "LANGTYPE", "bodylen": "BODY_LENGTH", "parselangtype": "PARSE_LANGTYPE",
    "len": "LENGTH", "char": "CHARACTER", "ord": "ORDINAL", "cmp": "COMPARE",
    "find": "FIND", "slice": "SLICE", "concat": "CONCAT",
    "formatint": "FORMAT_INTEGER", "formatuint": "FORMAT_UNSIGNED_INTEGER",
    "parseint": "PARSE_INTEGER", "parseuint": "PARSE_UNSIGNED_INTEGER",
    "parsefloat": "PARSE_FLOAT",
    "reshape": "RESHAPE", "reinterpret": "REINTERPRET",
    "newscatter": "NEW_SCATTER", "newcompact": "NEW_COMPACT", "append": "APPEND",
    "now": "NOW", "sub": "SUB", "add": "ADD", "before": "BEFORE", "after": "AFTER",
    "nanos": "NANOSECONDS", "millis": "MILLISECONDS", "seconds": "SECONDS",
    "minutes": "MINUTES", "hours": "HOURS",
    "as_nanos": "AS_NANOSECONDS", "as_millis": "AS_MILLISECONDS",
    "as_seconds": "AS_SECONDS", "as_minutes": "AS_MINUTES", "as_hours": "AS_HOURS",
    "get": "GET", "del": "DELETE", "deltree": "DELETE_TREE", "cp": "COPY",
    "cpdir": "COPY_DIRECTORY", "cplist": "COPY_LIST", "listlen": "LIST_LENGTH",
    "listat": "LIST_AT", "mkindex": "MAKE_INDEX", "extindex": "EXTENSION_INDEX",
    "rmindexext": "REMOVE_INDEX_EXTENSION", "watch": "WATCH",
    "create": "CREATE", "run": "RUN", "call": "CALL", "sleep": "SLEEP",
    "setstatus": "SET_STATUS",
    "uint64": "UNSIGNED_64", "int63": "SIGNED_63", "intn": "BELOW",
    "abs": "ADDRESS_OF",
}

FAMILY_IDENTIFIER_WORDS = {
    "compactarray": "COMPACT_ARRAY", "scatterarray": "SCATTER_ARRAY",
    "string": "STRING", "xv": "XVALUE", "ndarray": "NDARRAY", "time": "TIME",
    "time/duration": "DURATION", "kvspace": "KVSPACE", "vthread": "VTHREAD",
    "random": "RANDOM", "map": "MAP", "struct": "STRUCT", "kvlang": "KVLANG",
}


def preprocess_registry(root: Path) -> list[str]:
    """Expand the capability table with the C preprocessor so macro families are visible."""
    source = root / REGISTRY_SOURCE
    command = [
        "gcc", "-E", "-I", str(root / "runtime/include"),
        "-I", str(root / "runtime/src"), "-I", str(root / "include"), str(source),
    ]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL)
    if completed.returncode != 0:
        raise SystemExit(f"cannot preprocess {REGISTRY_SOURCE}")
    text = "\n".join(line for line in completed.stdout.splitlines()
                     if not line.startswith("#"))
    start = text.index("myrwircaps[] = {")
    body = text[start:]
    body = body[:body.index("\n};")]
    opcodes: list[str] = []
    for literal in re.findall(r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\s*,\s*\w+', body):
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', literal)
        name = "".join(parts).replace("\\xC2\\xB7", MIDDLE_DOT)
        opcodes.append(name)
    return opcodes


IDENTIFIER_CONSTANT_PATTERN = re.compile(
    r"#define\s+BINRWIR_OPERATION_IDENTIFIER_(\w+)\s+UINT32_C\(\s*(0x[0-9A-Fa-f]+)\s*\)")


def reserved_identifier_base(root: Path) -> int:
    """Read the reserved-band base from the binary instruction header.

    The header owns the value; the generator must not keep a second copy. A missing
    constant or a non-hexadecimal spelling is a hard error: without the boundary the
    guard cannot run, and a guard that silently degrades is not a guard.
    """
    path = root / INSTRUCTION_HEADER
    if not path.is_file():
        raise SystemExit(f"cannot read {INSTRUCTION_HEADER}")
    constants = {name: int(value, 16) for name, value in
                 IDENTIFIER_CONSTANT_PATTERN.findall(path.read_text(encoding="utf-8"))}
    if "RESERVED_BASE" not in constants:
        raise SystemExit(
            f"{INSTRUCTION_HEADER}: BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE is required")
    return constants["RESERVED_BASE"]


def matrix_decisions(root: Path) -> set[str]:
    text = (root / MATRIX).read_text(encoding="utf-8")
    return set(re.findall(r"^\| ([a-z][a-z0-9-]+) \|", text, re.M))


def family_identifier(family: str, operation: str) -> str:
    return "RUNTIMEC_RWIR_" + FAMILY_IDENTIFIER_WORDS[family] + \
        "_" + IDENTIFIER_WORDS[operation]


def _split_langtypes(text: str) -> tuple[str, ...]:
    return tuple(part.strip() for part in text.split(",") if part.strip())


def build_rows() -> list[Row]:
    rows: list[Row] = []
    for (identifier, operator, opid, read_mask, write_mask, constraint,
         result_rule, alias_policy, decisions, channel) in VALUE_ROWS:
        rows.append(Row(f"RUNTIMEC_RWIR_{identifier}", operator, opid, read_mask,
                        write_mask, constraint, result_rule, alias_policy, decisions,
                        channel))
    for family_row in FAMILY_ROWS:
        identifier = family_identifier(family_row.family, family_row.operation)
        alias_policy = family_row.alias_policy
        if (family_row.family, family_row.operation) in DECLARED_WRITERS:
            alias_policy = "DECLARED"
        rows.append(Row(identifier, f"{family_row.family}{MIDDLE_DOT}{family_row.operation}",
                        family_row.opid, family_row.read_mask, family_row.write_mask,
                        "NONE", "IDENTITY", alias_policy, (family_row.decision,),
                        "parameterized",
                        _split_langtypes(family_row.read_langtypes),
                        _split_langtypes(family_row.write_langtypes)))
    return rows


def surface_row(opcode: str, rows_by_suffix: dict[str, Row],
                rows_by_operator: dict[str, Row]) -> str | None:
    """Map one registered surface name to its capability row identifier."""
    def row_named(suffix: str) -> Row:
        row = rows_by_suffix.get(suffix)
        if row is None:
            raise SystemExit(f"alias table names an unknown capability row: {suffix}")
        return row

    if opcode in SYMBOLIC_ALIASES:
        return row_named(SYMBOLIC_ALIASES[opcode]).identifier
    if opcode in BARE_ALIASES:
        return row_named(BARE_ALIASES[opcode]).identifier
    if opcode in CAST_TARGETS:
        return row_named("CAST").identifier
    kind, separator, word = opcode.partition(MIDDLE_DOT)
    if separator and kind in SCALAR_KINDS:
        if word not in KIND_SPECIALIZED_OPERATIONS:
            return None
        return row_named(KIND_SPECIALIZED_OPERATIONS[word]).identifier
    row = rows_by_operator.get(opcode)
    return row.identifier if row is not None else None


def build_alias_table(opcodes: list[str], rows: list[Row],
                      ) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    rows_by_suffix = {row.identifier.removeprefix("RUNTIMEC_RWIR_"): row for row in rows}
    rows_by_operator = {row.operator: row for row in rows}
    aliases: list[tuple[str, str]] = []
    gaps: list[tuple[str, str]] = []
    for opcode in sorted(opcodes):
        target = surface_row(opcode, rows_by_suffix, rows_by_operator)
        if target is None:
            gaps.append((opcode, "no capability row"))
        else:
            aliases.append((opcode, target))
    return aliases, gaps


def validate(rows: list[Row], opcodes: list[str], aliases: list[tuple[str, str]],
             decisions: set[str], gaps: list[tuple[str, str]],
             reserved_base: int) -> list[str]:
    findings: list[str] = []
    identifiers = [row.identifier for row in rows]
    opids = [row.opid for row in rows]
    for label, values in (("identifier", identifiers), ("opid", opids)):
        duplicates = {value for value in values if values.count(value) > 1}
        if duplicates:
            findings.append(f"duplicate {label}: {sorted(duplicates)[:5]}")
    # 保留区（>= RESERVED_BASE）留给 FOREIGN 这类结构标记，能力行不得占用。
    # 判据是白名单：opid 必须严格落在 (0, reserved_base)，不是「不许等于某个值」。
    for row in rows:
        if not 0 < row.opid < reserved_base:
            findings.append(
                f"{row.identifier}: opid 0x{row.opid:08X} is outside the capability "
                f"band (0, 0x{reserved_base:08X})")
    for row in rows:
        if row.constraint not in CONSTRAINTS:
            findings.append(f"{row.identifier}: unknown constraint {row.constraint}")
        if row.result_rule not in RESULT_RULES:
            findings.append(f"{row.identifier}: unknown result rule {row.result_rule}")
        if row.alias_policy not in ALIAS_POLICIES:
            findings.append(f"{row.identifier}: unknown alias policy {row.alias_policy}")
        for name in ("read", "write"):
            mask = row.read_mask if name == "read" else row.write_mask
            if mask & ~(TYPE_SET_LIMIT - 1):
                findings.append(f"{row.identifier}: {name} mask sets a reserved bit")
        for decision in row.decisions:
            if decision not in decisions:
                findings.append(f"{row.identifier}: unknown behavior decision {decision}")
        if row.channel == "value" and row.result_rule != "IDENTITY":
            if row.write_mask == 0:
                findings.append(f"{row.identifier}: value row has no write mask")
        if row.channel == "control" and (row.read_mask or row.write_mask):
            findings.append(f"{row.identifier}: control row carries a value mask")
        if row.result_rule == "IDENTITY" and row.channel == "value" and row.read_mask == 0:
            findings.append(f"{row.identifier}: identity row states no source mask")
    registered = set(opcodes)
    alias_names = [name for name, _ in aliases]
    missing = registered - set(alias_names) - {name for name, _ in gaps}
    extra = set(alias_names) - registered
    if missing:
        findings.append(f"registered opcode missing from alias table: {sorted(missing)[:5]}")
    if extra:
        findings.append(f"alias table names an unregistered opcode: {sorted(extra)[:5]}")
    images = {target for _, target in aliases}
    row_identifiers = set(identifiers)
    if not images <= row_identifiers:
        findings.append(f"alias image outside the capability rows: "
                        f"{sorted(images - row_identifiers)[:5]}")
    # 没有 surface 名字的行只能由特化表选择 —— 白名单不存在，合法身份的代价是写一条
    # 特化规则（surface 必须已注册、掩码合法、目标行存在），而不是写一个名字。
    selected = {f"RUNTIMEC_RWIR_{target}" for _, _, _, _, target in SPECIALIZATIONS}
    unreachable = row_identifiers - images
    if unreachable != selected:
        findings.append(
            f"compiler-selected rows must be exactly the specialization targets: "
            f"rows without a surface name {sorted(unreachable)}, "
            f"specialization targets {sorted(selected)}")
    declared = [row for row in rows if row.read_langtypes or row.write_langtypes]
    row_by_surface = {row.operator: row for row in rows}
    for row in declared:
        if row.operator not in row_by_surface:
            findings.append(f"{row.identifier}: signature row has no surface name")
        for langtype in row.read_langtypes + row.write_langtypes:
            if langtype not in SIGNATURE_LANGTYPES:
                findings.append(
                    f"{row.identifier}: {langtype!r} is not a langtype layout accepts")
            for character in FORBIDDEN_LANGTYPE_CHARACTERS:
                if character in langtype:
                    findings.append(
                        f"{row.identifier}: langtype {langtype!r} contains "
                        f"{character!r}, which would corrupt the declaration text")
    if not declared:
        findings.append("the signature table would be empty; "
                        "layout needs it for name-typed returns")
    else:
        expected_shapes = [
            (row.operator,
             [f"{SIGNATURE_PARAMETER_NAME}{index}: {langtype}"
              for index, langtype in enumerate(row.read_langtypes)],
             [f"{SIGNATURE_RESULT_NAME}{index}: {langtype}"
              for index, langtype in enumerate(row.write_langtypes)])
            for row in declared]
        recovered = read_back_signature_table(render_signature_table(rows))
        if recovered != expected_shapes:
            findings.append(
                "the signature table does not read back as written: "
                f"expected {expected_shapes[:2]}…, recovered {recovered[:2]}…")

    for surface, first_mask, second_mask, _, target in SPECIALIZATIONS:
        if surface not in registered:
            findings.append(f"specialization names an unregistered surface {surface}")
        if f"RUNTIMEC_RWIR_{target}" not in row_identifiers:
            findings.append(f"specialization targets an unknown row {target}")
        for mask in (first_mask, second_mask):
            if mask == 0 or mask & ~(TYPE_SET_LIMIT - 1):
                findings.append(f"specialization for {surface} has an invalid mask")
    return findings


def render_signature_table(rows: list[Row]) -> str:
    """Emit the layout-facing native rwir declarations.

    格式就是 kvlang 的 `rwir` 声明语法——与 `rwfunc` 同形，只差函数体（由 runtime 实现）。
    这样 layout 现成的 `rwir_decls` 解析器直接消费，不需要为这张表再写一个解析器；手写的
    `rwir xxx` 声明与 runtime 提供的 native rwir 从此是同一件事，只是兑现方不同。

    空 kindexpr（`name:` 后面什么都不写）就是「该参数/返回值不声明类型」的官方写法：
    `any` 已被禁用、缺冒号是错误。生成物只用这一种写法，不发明第三种占位。

    只收录声明了名字型类型的行。其余行的类型由 TypeSet 位或 `/lib/<op>` 的安装签名给出，
    在 layout 侧是另一套判据，不该在这份声明里出现第二份。
    """
    declared = [row for row in rows if row.read_langtypes or row.write_langtypes]
    lines = [
        "// 组件：specification/native_rwir_decls.kv",
        "// 作用：native rwir 的签名声明，供 layout 在**构建期**编进二进制。",
        "// 联系：真源是 runtime/src/myrwircaps.c 的注册表 + five-language-behavior-matrix.md",
        "//       的裁决；二者生成 rwir_catalog.def 的能力行，再由能力行生成本文件。",
        "//       改签名要改真源，不改任何一份产物。本文件由",
        "//       tools/architecture/generate_rwir_catalog.py 生成并由同一脚本校验。",
        "// 约束：不让 layout 运行期访问 kvspace 读 /lib/<op>；也不在 layout 里手写第二份。",
        "// 语法：与 rwfunc 同形的 rwir 声明，只差函数体——兑现方是 runtime。",
        "//       空 kindexpr（`name:`）表示该参数/返回值不声明类型。",
        f"// 统计：{len(declared)} 条声明"
        "（其余行的签名由 TypeSet 位或安装签名给出）。",
        "",
    ]
    for row in declared:
        parameters = ", ".join(
            f"{SIGNATURE_PARAMETER_NAME}{index}: {langtype}"
            for index, langtype in enumerate(row.read_langtypes))
        returns = ", ".join(
            f"{SIGNATURE_RESULT_NAME}{index}: {langtype}"
            for index, langtype in enumerate(row.write_langtypes))
        lines.append(f"rwir {row.operator}({parameters}) -> ({returns})")
    lines.append("")
    return "\n".join(lines)


# 参数名与返回值名是我们自己造的，就必须无歧义：`a` 可读成 argument/array/address，
# `r` 可读成 result/register。名字只是占位（layout 不按名字匹配），但读的人会。
SIGNATURE_PARAMETER_NAME = "argument"
SIGNATURE_RESULT_NAME = "result"

SIGNATURE_LINE_PATTERN = re.compile(
    r"^rwir (\S+)\(([^)]*)\) -> \(([^)]*)\)$")


def read_back_signature_table(text: str) -> list[tuple[str, list[str], list[str]]]:
    """Parse the emitted declarations the way layout's `rwir` reader would.

    独立于渲染器：渲染器把行拼成字符串，这里把它读回来。两边同时写错才会互相抵消，
    而那种情形由 layout 自己的 `vet` 兜底——本函数只保证生成物**结构**没坏
    （括号、箭头、参数名与类型能对上）。
    """
    recovered: list[tuple[str, list[str], list[str]]] = []
    for line in text.splitlines():
        if not line.startswith("rwir "):
            continue
        match = SIGNATURE_LINE_PATTERN.match(line)
        if match is None:
            raise SystemExit(f"generated declaration is malformed: {line!r}")
        surface, raw_parameters, raw_returns = match.groups()
        parameters = [item.strip() for item in raw_parameters.split(",") if item.strip()]
        returns = [item.strip() for item in raw_returns.split(",") if item.strip()]
        recovered.append((surface, parameters, returns))
    return recovered


def render_catalog(rows: list[Row], aliases: list[tuple[str, str]],
                   opcodes: list[str], gaps: list[tuple[str, str]]) -> str:
    identity_names = {row.operator for row in rows}
    alias_count = len(aliases) - len(identity_names & {name for name, _ in aliases})
    lines = [
        "/*",
        " * 组件：specification/rwir_catalog.def",
        " * 作用：myrwircap 的唯一 capability manifest —— 一行一个语义算子。",
        " * 联系：surface 名字到本表行的映射在 rwir_alias_table.def；两表都由",
        " *       tools/architecture/generate_rwir_catalog.py 生成并校验。",
        " * 约束：opid 与 identifier 显式分配，不来自数组下标、文件顺序或声明顺序；",
        " *       读写掩码是 TypeSet 位集，不是占位串，不含 wildcard；未获得矩阵决策的",
        " *       能力不进入本文件，记录在 rwir_catalog_gaps.md。",
        " * 掩码位：0 int8，1 int16，2 int32，3 int64，4 uint8，5 uint16，6 uint32，",
        " *       7 uint64，8 float32，9 float64，10 bool，11 char/utf8，12 char/utf32，",
        " *       13 char/ascii，14 array，15 stringkeymap，16 structref，17 ext，",
        " *       18 pointer；19 位以上保留且必须为零。",
        " * 掩码零：该行不在标量 union 通道。parameterized 行的参数与结果类型由",
        " *       /lib/<operator> 的 def rwir 槽在安装时声明（唯一签名来源，禁止在本表",
        " *       复制第二张签名表）；ResultRule IDENTITY 表示结果类型取安装签名，",
        " *       不是 wildcard。control 行不产生值。",
        f" * 统计：注册表 {len(opcodes)} 个 surface 名字 → {len(aliases)} 条别名映射；",
        f" *       {len(identity_names & {name for name, _ in aliases})} 条同名映射，"
        f"{alias_count} 条别名；能力行 {len(rows)} 条，待裁 {len(gaps)} 条。",
        " */",
        "#ifndef RUNTIMEC_RWIR_OPERATION",
        '#error "Define RUNTIMEC_RWIR_OPERATION before including rwir_catalog.def"',
        "#endif",
        "",
        "/* RUNTIMEC_RWIR_OPERATION(identifier, operator, opid,",
        " *                        read_union_mask, write_union_mask,",
        " *                        constraint, result_rule, alias_policy, behavior_decisions)",
        " *",
        " * Constraint    : RUNTIMEC_RWIR_CONSTRAINT_{NONE,SAME_CLASS,NUMERIC_COMPATIBLE,",
        " *                 NUMERIC_COMPARE}",
        " * ResultRule    : RUNTIMEC_RWIR_RESULT_{NONE,SAME_FIRST,SAME_SECOND,WIDEN,",
        " *                 WIDEN_INTEGER,BOOLEAN,FLOAT64,TARGET_TYPE,IDENTITY,CHARACTER_UTF8}",
        " * AliasPolicy   : RUNTIMEC_RWIR_ALIAS_{FORBIDDEN,DECLARED}",
        " * behavior_decisions: 一个或多个逗号分隔的矩阵决策；第一个是该行的主决策。",
        " */",
    ]
    for row in rows:
        lines.append(f"/* behavior-decision: {row.decisions[0]} */")
        for decision in row.decisions[1:]:
            lines.append(f"/* behavior-decision: {decision} */")
        read_name = TYPE_SET_NAMES.get(row.read_mask, "") or "install signature"
        write_name = TYPE_SET_NAMES.get(row.write_mask, "") or "install signature"
        if row.channel == "control":
            read_name = write_name = "no value"
        lines.append(
            f'RUNTIMEC_RWIR_OPERATION({row.identifier}, "{row.operator}", 0x{row.opid:04X},')
        lines.append(
            f'                        0x{row.read_mask:08X}, 0x{row.write_mask:08X},')
        lines.append(
            f'                        RUNTIMEC_RWIR_CONSTRAINT_{row.constraint},')
        lines.append(
            f'                        RUNTIMEC_RWIR_RESULT_{row.result_rule},')
        lines.append(
            f'                        RUNTIMEC_RWIR_ALIAS_{row.alias_policy},')
        lines.append(
            f'                        "{",".join(row.decisions)}")'
            f'  /* {read_name} -> {write_name} */'
        )
    lines.append("")
    lines.append(f"/* next free opid: 0x{max(row.opid for row in rows) + 1:04X} */")
    lines.append("")
    return "\n".join(lines)


def render_alias_table(aliases: list[tuple[str, str]], opcodes: list[str]) -> str:
    lines = [
        "/*",
        " * 组件：specification/rwir_alias_table.def",
        " * 作用：surface 名字 -> capability row 的唯一映射（数据，不是能力行）。",
        " * 约束：每个注册名恰好一条记录；名字相同不代表语义相同，语义只由 rwir_catalog.def",
        " *       的行定义。rwir_catalog.def 是唯一签名来源，本表不携带类型。",
        " * 特化：RUNTIMEC_RWIR_SPECIALIZATION 声明编译器按操作数静态类型改选的行；",
        " *       invert 为真时结果取反（`!=` 的字符/指针/布尔比较）。",
        f" * 统计：{len(opcodes)} 个注册 surface 名字，{len(aliases)} 条别名映射，"
        f"{len(SPECIALIZATIONS)} 条特化。",
        " */",
        "#ifndef RUNTIMEC_RWIR_ALIAS",
        '#error "Define RUNTIMEC_RWIR_ALIAS before including rwir_alias_table.def"',
        "#endif",
        "#ifndef RUNTIMEC_RWIR_SPECIALIZATION",
        '#error "Define RUNTIMEC_RWIR_SPECIALIZATION before including rwir_alias_table.def"',
        "#endif",
        "",
        '/* RUNTIMEC_RWIR_ALIAS(surface_name, capability_identifier) */',
    ]
    for surface, target in aliases:
        lines.append(f'RUNTIMEC_RWIR_ALIAS("{surface}", {target})')
    lines.append("")
    lines.append("/* compiler-selected specializations */")
    lines.append("/* RUNTIMEC_RWIR_SPECIALIZATION(surface_name, first_mask, second_mask,")
    lines.append(" *                              invert, capability_identifier) */")
    for surface, first_mask, second_mask, invert, target in SPECIALIZATIONS:
        inverted = "true" if invert else "false"
        lines.append(
            f'RUNTIMEC_RWIR_SPECIALIZATION("{surface}", 0x{first_mask:08X}, '
            f'0x{second_mask:08X}, {inverted}, RUNTIMEC_RWIR_{target})'
        )
    lines.append("")
    return "\n".join(lines)


def render_gaps(gaps: list[tuple[str, str]], opcodes: list[str],
                rows: list[Row]) -> str:
    lines = [
        "# rwir_catalog 缺口清单",
        "",
        "本文件列出 `runtime/src/myrwircaps.c` 中已注册、但无法收敛到一条已裁决",
        "capability row 的 surface 名字。它们**不**进入 `rwir_alias_table.def`：",
        "补齐决策前不允许进入 canonical BinRWIR。",
        "",
        f"注册表 surface 名字：{len(opcodes)}；能力行：{len(rows)}；待裁：{len(gaps)}。",
        "",
        "| surface name | 缺什么 |",
        "|---|---|",
    ]
    if not gaps:
        lines.append("| — | 无 |")
    for opcode, reason in gaps:
        lines.append(f"| `{opcode}` | {reason} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=None,
                        help="repository root; defaults to the tree this script lives in")
    parser.add_argument("--check", action="store_true",
                        help="verify the checked-in files instead of rewriting them")
    parser.add_argument("--require-complete", action="store_true",
                        help="fail when any registered surface name still lacks a capability row")
    arguments = parser.parse_args()

    if arguments.root is not None:
        root = arguments.root.resolve()
    elif len(Path(__file__).resolve().parents) > 3:
        root = Path(__file__).resolve().parents[3]
    else:
        root = Path.cwd()
    opcodes = preprocess_registry(root)
    decisions = matrix_decisions(root)
    rows = build_rows()
    aliases, gaps = build_alias_table(opcodes, rows)
    reserved_base = reserved_identifier_base(root)
    findings = validate(rows, opcodes, aliases, decisions, gaps, reserved_base)
    expected = {
        MANIFEST: render_catalog(rows, aliases, opcodes, gaps),
        ALIAS_TABLE: render_alias_table(aliases, opcodes),
        GAPS: render_gaps(gaps, opcodes, rows),
        SIGNATURE_TABLE: render_signature_table(rows),
    }

    if not arguments.check:
        for path, text in expected.items():
            (root / path).write_text(text, encoding="utf-8")
        print(f"generate_rwir_catalog: wrote {len(rows)} capability rows, "
              f"{len(aliases)} alias mappings, {len(gaps)} gaps "
              f"of {len(opcodes)} registered surface names")
        for finding in findings:
            print(f"[drift] {finding}")
        return 1 if findings else 0

    drift = list(findings)
    for path, text in expected.items():
        actual = (root / path).read_text(encoding="utf-8") if (root / path).is_file() else ""
        if actual != text:
            drift.append(f"{path}: out of date; re-run without --check")
    compared = set(expected)
    for artifact in ARTIFACTS:
        if artifact not in compared:
            drift.append(f"{artifact}: declared artifact is not verified by --check")
    unseen = compared - set(ARTIFACTS)
    if unseen:
        drift.append(f"verification covers undeclared artifacts: {sorted(unseen)}")

    if drift:
        for finding in drift:
            print(f"[drift] {finding}")
        print(f"generate_rwir_catalog: FAIL ({len(drift)} drift finding(s))")
        return 1
    if arguments.require_complete and gaps:
        for opcode, reason in gaps:
            print(f"[gap] undeclared surface name {opcode!r}: {reason}")
        print(f"generate_rwir_catalog: INCOMPLETE ({len(gaps)} surface name(s); "
              f"add a capability row before --require-complete can pass)")
        return 1
    print(f"generate_rwir_catalog: OK ({len(rows)} capability rows, "
          f"{len(aliases)} alias mappings, {len(gaps)} gaps, "
          f"{len(opcodes)} registered surface names)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
