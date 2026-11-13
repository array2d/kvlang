
from __future__ import annotations

import argparse
import re
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

SPEC = Path("runtimec/specification")
REGISTRY_SOURCE = Path("runtimec/specification/myrwircaps.c")
MANIFEST = SPEC / "rwir_catalog.def"
ALIAS_TABLE = SPEC / "rwir_alias_table.def"
GAPS = SPEC / "rwir_catalog_gaps.md"
SIGNATURE_TABLE = SPEC / "native_rwir_decls.kv"
OPERATION_IDENTIFIERS_HEADER = Path("runtimec/binrwir/operation_identifiers.h")
SPECIALIZATION_TABLE = Path("runtimec/binrwir/specialization_table.c")
ARTIFACTS = (MANIFEST, ALIAS_TABLE, GAPS, SIGNATURE_TABLE,
             OPERATION_IDENTIFIERS_HEADER, SPECIALIZATION_TABLE)
MATRIX = SPEC / "five-language-behavior-matrix.md"
INSTRUCTION_HEADER = Path("runtimec/binrwir/instruction.h")
REGISTRY_HEADER = Path("runtimec/binrwir/registry.h")

MIDDLE_DOT = "·"

RETIRED_ROWS = (
    ("OBJECT_LITERAL", "object", 0x0028, "obj"),
)

def retired_surface_names() -> set[str]:
    names: set[str] = set()
    for row in RETIRED_ROWS:
        names.add(row[1])
        names.update(row[3:])
    return names

KIND_BITS = {
    "int8": 0, "int16": 1, "int32": 2, "int64": 3,
    "uint8": 4, "uint16": 5, "uint32": 6, "uint64": 7,
    "float32": 8, "float64": 9, "bool": 10,
    "char/utf8": 11, "char/utf32": 12, "char/ascii": 13,
    "array": 14, "stringkeymap": 15, "structref": 16, "ext": 17,
    "pointer": 18,
    "any_single": 19,
}
TYPE_SET_WIDTH = 20
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
ANY_SINGLE = type_set("any_single")
FLOAT64 = type_set("float64")

BASE_LANGTYPES = (
    "int8", "int16", "int32", "int64",
    "uint8", "uint16", "uint32", "uint64",
    "float32", "float64",
    "bool",
    "char/utf8", "char/utf32", "char/ascii",
    "time", "time/duration",
)

SHAPE_PREFIX_PATTERN = re.compile(r"^\[[0-9,]+\]")
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
    channel: str
    read_langtypes: tuple[str, ...] = ()
    write_langtypes: tuple[str, ...] = ()
    read_roles: tuple[str, ...] = ()
    write_roles: tuple[str, ...] = ()

VALUE_ROWS = (
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
    ("ABSOLUTE", "abs", 0x0009, NUMBER, NUMBER, "NONE", "SAME_FIRST", "FORBIDDEN",
     ("signed-neg-abs-sign", "unsigned-neg-abs-sign"), "value"),
    ("NEGATION", "neg", 0x000A, NUMBER, NUMBER, "NONE", "SAME_FIRST", "FORBIDDEN",
     ("signed-neg-abs-sign", "unsigned-neg-abs-sign"), "value"),
    ("SIGN", "sign", 0x000B, NUMBER, NUMBER, "NONE", "SAME_FIRST", "FORBIDDEN",
     ("signed-neg-abs-sign", "unsigned-neg-abs-sign"), "value"),
    ("BITWISE_AND", "bitand", 0x000C, INTEGER, INTEGER, "SAME_CLASS", "WIDEN",
     "FORBIDDEN", ("mixed-signed-unsigned-arithmetic",), "value"),
    ("BITWISE_OR", "bitor", 0x000D, INTEGER, INTEGER, "SAME_CLASS", "WIDEN",
     "FORBIDDEN", ("mixed-signed-unsigned-arithmetic",), "value"),
    ("BITWISE_XOR", "bitxor", 0x000E, INTEGER, INTEGER, "SAME_CLASS", "WIDEN",
     "FORBIDDEN", ("mixed-signed-unsigned-arithmetic",), "value"),
    ("SHIFT_LEFT", "shl", 0x000F, INTEGER, INTEGER, "SAME_CLASS", "WIDEN",
     "FORBIDDEN", ("shift-count-masking", "mixed-signed-unsigned-arithmetic"), "value"),
    ("SHIFT_RIGHT", "shr", 0x0010, INTEGER, INTEGER, "SAME_CLASS", "WIDEN",
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
    ("CONCAT", "concat", 0x001F, CHARACTER, CHARACTER_UTF8, "SAME_CLASS", "CHARACTER_UTF8",
     "FORBIDDEN", ("character-concat-kind",), "value"),
    ("CHARACTER_EQUAL", "streq", 0x0020, CHARACTER, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("character-equality-by-text",), "value"),
    ("POINTER_EQUAL", "ptreq", 0x0021, POINTER, BOOLEAN, "SAME_CLASS", "BOOLEAN", "FORBIDDEN",
     ("pointer-equality",), "value"),
    ("BOOLEAN_EQUAL", "bool·eq", 0x0022, BOOLEAN, BOOLEAN, "NONE", "BOOLEAN", "FORBIDDEN",
     ("boolean-equality",), "value"),
    ("CHARACTER_NOT_EQUAL", "streq·not", 0x0029, CHARACTER, BOOLEAN, "NONE", "BOOLEAN",
     "FORBIDDEN", ("character-equality-by-text",), "value"),
    ("POINTER_NOT_EQUAL", "ptreq·not", 0x002A, POINTER, BOOLEAN, "SAME_CLASS", "BOOLEAN",
     "FORBIDDEN", ("pointer-equality",), "value"),
    ("BOOLEAN_NOT_EQUAL", "bool·neq", 0x002B, BOOLEAN, BOOLEAN, "NONE", "BOOLEAN",
     "FORBIDDEN", ("boolean-equality",), "value"),
    ("DEBUGGER", "debugger", 0x0023, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("debugger-pauses-execution",), "control"),
    ("NONNULL", "nonnull", 0x01A4, ANY_SINGLE, BOOLEAN, "NONE", "BOOLEAN",
     "FORBIDDEN", ("none-criterion",), "value"),
    ("NONNULL_NOT", "nonnull·not", 0x01A5, ANY_SINGLE, BOOLEAN, "NONE", "BOOLEAN",
     "FORBIDDEN", ("none-criterion",), "value"),
    ("CALL", "call", 0x0024, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("RETURN", "return", 0x0025, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("GOTO", "goto", 0x0026, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
    ("BRANCH", "branch", 0x0027, 0, 0, "NONE", "NONE", "FORBIDDEN",
     ("control-flow-primitives",), "control"),
)

ROLE_ANY = ""
ROLE_KEY = "key"
PARAMETER_ROLES = (ROLE_ANY, ROLE_KEY)

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
    read_roles: str = ""
    write_roles: str = ""
    alias_policy: str = "FORBIDDEN"
    read_langtypes: str = ""
    write_langtypes: str = ""

DECLARED_WRITERS = {
    ("compactarray", "set"), ("compactarray", "fill"),
    ("scatterarray", "set"), ("scatterarray", "append"),
    ("string", "set"), ("xv", "set"),
    ("map", "new"), ("struct", "new"),
}

FAMILY_ROWS = (
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
    FamilyRow("scatterarray", "new", 0x0110, "scatterarray-next-index"),
    FamilyRow("scatterarray", "len", 0x0111, "scatterarray-next-index"),
    FamilyRow("scatterarray", "at", 0x0112, "scatterarray-next-index"),
    FamilyRow("scatterarray", "set", 0x0113, "scatterarray-next-index"),
    FamilyRow("scatterarray", "newscatter", 0x0114, "scatterarray-next-index"),
    FamilyRow("scatterarray", "newcompact", 0x0115, "scatterarray-next-index"),
    FamilyRow("scatterarray", "append", 0x0116, "scatterarray-next-index"),
    FamilyRow("scatterarray", "slice", 0x0117, "string-slice-bounds"),
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
    FamilyRow("xv", "at", 0x0130, "array-element-access-by-kind"),
    FamilyRow("xv", "set", 0x0131, "array-element-access-by-kind"),
    FamilyRow("xv", "reshape", 0x0132, "array-reshape-numel-mismatch"),
    FamilyRow("xv", "reinterpret", 0x0133, "array-reinterpret-raw"),
    FamilyRow("xv", "langtype", 0x0134, "xv-introspection-result-kind"),
    FamilyRow("xv", "bodylen", 0x0135, "xv-introspection-result-kind"),
    FamilyRow("xv", "parselangtype", 0x0136, "xv-introspection-result-kind"),
    FamilyRow("ndarray", "numel", 0x0140, "array-element-access-by-kind"),
    FamilyRow("ndarray", "dim", 0x0141, "array-element-access-by-kind"),
    FamilyRow("ndarray", "shape", 0x0142, "array-element-access-by-kind"),
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
    FamilyRow("pathspace", "del", 0x0172, "pathspace-set-write-scope",
              read_roles="key"),
    FamilyRow("pathspace", "deltree", 0x0173, "pathspace-set-write-scope",
              read_roles="key"),
    FamilyRow("pathspace", "cp", 0x0174, "pathspace-copy-semantics",
              read_roles="key"),
    FamilyRow("pathspace", "cpdir", 0x0175, "pathspace-copy-semantics",
              read_roles="key"),
    FamilyRow("pathspace", "cplist", 0x0176, "pathspace-copy-semantics",
              read_roles="key"),
    FamilyRow("pathspace", "listlen", 0x0177, "pathspace-list-ordering",
              read_roles="key"),
    FamilyRow("pathspace", "listat", 0x0178, "pathspace-list-ordering",
              read_roles="key,"),
    FamilyRow("pathspace", "mkindex", 0x0179, "pathspace-index-no-capacity",
              read_roles="key"),
    FamilyRow("pathspace", "extindex", 0x017A, "pathspace-extension-index",
              read_roles="key,key"),
    FamilyRow("pathspace", "delextindex", 0x017B, "pathspace-extension-index",
              read_roles="key"),
    FamilyRow("pathspace", "watch", 0x017C, "pathspace-watch-blocking",
              read_roles="key,"),
    FamilyRow("vthread", "create", 0x0180, "vthread-control-opcodes",
              read_langtypes="char/utf8", write_langtypes="char/utf8"),
    FamilyRow("vthread", "run", 0x0181, "vthread-control-opcodes",
              read_langtypes="char/utf8"),
    FamilyRow("vthread", "call", 0x0182, "vthread-control-opcodes",
              read_langtypes="char/utf8"),
    FamilyRow("vthread", "sleep", 0x0183, "vthread-control-opcodes",
              read_langtypes="time/duration"),
    FamilyRow("vthread", "setstatus", 0x0184, "vthread-control-opcodes",
              read_langtypes="char/utf8"),
    FamilyRow("vthread", "pc", 0x0185, "vthread-pc-pure-read",
              write_langtypes="int32,[2]int32"),
    FamilyRow("random", "uint64", 0x0190, "random-source-and-range",
              write_langtypes="uint64"),
    FamilyRow("random", "int63", 0x0191, "random-source-and-range",
              write_langtypes="int64"),
    FamilyRow("random", "intn", 0x0192, "random-source-and-range",
              read_langtypes="int64", write_langtypes="uint64"),
    FamilyRow("map", "new", 0x01A0, "map-literal-replaces-members",
              0, STRING_KEY_MAP),
    FamilyRow("struct", "new", 0x01A1, "struct-new-from-prototype",
              0, STRUCT_REF),
    FamilyRow("pathspace/key", "abs", 0x01A2, "address-of-yields-pointer", 0, POINTER),
    FamilyRow("pathspace/key", "tostr", 0x01A3, "float-member-key-identity",
              0, CHARACTER_UTF8),
    FamilyRow("pathlang", "resolvekey", 0x01A6, "pointer-target-key-text", 0, POINTER),
)

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
    "debugger": "DEBUGGER",
    "call": "CALL", "return": "RETURN", "goto": "GOTO",
}

KIND_SPECIALIZED_OPERATIONS = {
    "add": "ADD", "sub": "SUB", "mul": "MUL", "div": "DIV", "mod": "MOD",
    "max": "MAX", "min": "MIN", "abs": "ABSOLUTE", "neg": "NEGATION", "sign": "SIGN",
    "bitand": "BITWISE_AND", "bitor": "BITWISE_OR", "bitxor": "BITWISE_XOR",
    "shl": "SHIFT_LEFT", "shr": "SHIFT_RIGHT",
    "sqrt": "SQUARE_ROOT", "exp": "EXPONENTIAL", "log": "LOGARITHM", "pow": "POWER",
    "eq": "EQUAL", "neq": "NOT_EQUAL",
    "lt": "LESS", "le": "LESS_EQUAL", "gt": "GREATER", "ge": "GREATER_EQUAL",
}

SPECIALIZATIONS = (
    ("+", CHARACTER, CHARACTER, False, "CONCAT"),
    ("==", CHARACTER, CHARACTER, False, "CHARACTER_EQUAL"),
    ("!=", CHARACTER, CHARACTER, False, "CHARACTER_NOT_EQUAL"),
    ("==", POINTER, POINTER, False, "POINTER_EQUAL"),
    ("!=", POINTER, POINTER, False, "POINTER_NOT_EQUAL"),
    ("==", BOOLEAN, BOOLEAN, False, "BOOLEAN_EQUAL"),
    ("!=", BOOLEAN, BOOLEAN, False, "BOOLEAN_NOT_EQUAL"),
)

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
    "delextindex": "DELETE_EXTENSION_INDEX", "watch": "WATCH",
    "create": "CREATE", "run": "RUN", "call": "CALL", "sleep": "SLEEP",
    "setstatus": "SET_STATUS",
    "pc": "PC",
    "uint64": "UNSIGNED_64", "int63": "SIGNED_63", "intn": "BELOW",
    "abs": "ADDRESS_OF",
    "tostr": "TOSTRING",
    "resolvekey": "RESOLVE_POINTER",
}

FAMILY_IDENTIFIER_WORDS = {
    "compactarray": "COMPACT_ARRAY", "scatterarray": "SCATTER_ARRAY",
    "string": "STRING", "xv": "XVALUE", "ndarray": "NDARRAY", "time": "TIME",
    "time/duration": "DURATION", "pathspace": "PATHSPACE", "pathspace/key": "KEY",
    "vthread": "VTHREAD",
    "random": "RANDOM", "map": "MAP", "struct": "STRUCT", "pathlang": "PATHLANG",
}

def preprocess_registry(root: Path) -> list[str]:
    source = root / REGISTRY_SOURCE
    command = [
        "gcc", "-E", "-I", str(root / "include"), str(source),
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
    r"#define\s+BINRWIR_OPERATION_IDENTIFIER_(\w+)\s+UINT16_C\(\s*(0x[0-9A-Fa-f]+)\s*\)")

REGISTRY_ENTRY_LIMIT_PATTERN = re.compile(
    r"#define\s+BINRWIR_REGISTRY_ENTRY_LIMIT\s+(\d+)U?")

def registry_entry_limit(root: Path) -> int:
    path = root / REGISTRY_HEADER
    if not path.is_file():
        raise SystemExit(f"cannot read {REGISTRY_HEADER}")
    match = REGISTRY_ENTRY_LIMIT_PATTERN.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise SystemExit(f"{REGISTRY_HEADER}: BINRWIR_REGISTRY_ENTRY_LIMIT is required")
    return int(match.group(1))

def reserved_identifier_base(root: Path) -> int:
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

def render_roles(roles: tuple[str, ...]) -> str:
    if not roles:
        return '""'
    return '"' + ",".join(roles) + '"'

def _split_roles(text: str) -> tuple[str, ...]:
    if not text:
        return ()
    return tuple(part.strip() for part in text.split(","))

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
                        _split_langtypes(family_row.write_langtypes),
                        _split_roles(family_row.read_roles),
                        _split_roles(family_row.write_roles)))
    return rows

def surface_row(opcode: str, rows_by_suffix: dict[str, Row],
                rows_by_operator: dict[str, Row]) -> str | None:
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
    retired = retired_surface_names()
    for opcode in sorted(opcodes):
        if opcode in retired:
            continue
        target = surface_row(opcode, rows_by_suffix, rows_by_operator)
        if target is None:
            gaps.append((opcode, "no capability row"))
        else:
            aliases.append((opcode, target))
    return aliases, gaps

def validate(rows: list[Row], opcodes: list[str], aliases: list[tuple[str, str]],
             decisions: set[str], gaps: list[tuple[str, str]],
             reserved_base: int, entry_limit: int) -> list[str]:
    findings: list[str] = []
    identifiers = [row.identifier for row in rows]
    opids = [row.opid for row in rows]
    for label, values in (("identifier", identifiers), ("opid", opids)):
        duplicates = {value for value in values if values.count(value) > 1}
        if duplicates:
            findings.append(f"duplicate {label}: {sorted(duplicates)[:5]}")
    if len(rows) + len(RETIRED_ROWS) > entry_limit:
        findings.append(
            f"{len(rows)} capability rows + {len(RETIRED_ROWS)} retired rows exceed "
            f"BINRWIR_REGISTRY_ENTRY_LIMIT ({entry_limit}) in {REGISTRY_HEADER}")

    widen_slip = 0
    widen_examples: list[str] = []
    for row in rows:
        if row.result_rule != "WIDEN":
            continue
        for combination in specialization_combinations(row):
            operands, result = combination[:-1], combination[-1]
            expected = result_of("WIDEN", list(operands))
            if expected is None or expected == result:
                continue
            widen_slip += 1
            if len(widen_examples) < 3:
                widen_examples.append(f"{row.operator}({','.join(operands)}) "
                                      f"= {result}, expected {expected}")
    if widen_slip:
        findings.append(
            f"{widen_slip} WIDEN row(s) disagree with the class-priority rule "
            f"(float beats integer regardless of width): {'; '.join(widen_examples)}")

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
            if (mask & ANY_SINGLE) != 0 and mask != ANY_SINGLE:
                findings.append(
                    f"{row.identifier}: {name} mask sets any_single together with "
                    f"concrete kinds; the marker must be exclusive")
        for name in ("read", "write"):
            roles = row.read_roles if name == "read" else row.write_roles
            if not roles:
                continue
            if row.channel != "parameterized":
                findings.append(
                    f"{row.identifier}: {name} roles are only meaningful on "
                    f"parameterized rows (channel is {row.channel})")
            for position, role in enumerate(roles):
                if role not in PARAMETER_ROLES:
                    findings.append(
                        f"{row.identifier}: {name} role {position} is {role!r}, "
                        f"not one of {PARAMETER_ROLES}")
            declared = (len(row.read_langtypes) if name == "read"
                        else len(row.write_langtypes))
            if declared != 0 and len(roles) > declared:
                findings.append(
                    f"{row.identifier}: {name} declares {len(roles)} roles for "
                    f"{declared} parameters")
        for decision in row.decisions:
            if decision not in decisions:
                findings.append(f"{row.identifier}: unknown behavior decision {decision}")
        if row.channel == "value" and row.result_rule != "IDENTITY":
            if row.write_mask == 0:
                findings.append(f"{row.identifier}: value row has no write mask")
        if row.channel == "control" and (row.read_mask or row.write_mask):
            findings.append(f"{row.identifier}: control row carries a value mask")
        if (row.result_rule == "IDENTITY" and row.channel == "value"
                and row.read_mask == 0):
            findings.append(f"{row.identifier}: identity row states no source mask")
    registered = set(opcodes)
    alias_names = [name for name, _ in aliases]
    missing = (registered - set(alias_names) - {name for name, _ in gaps}
               - retired_surface_names())
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
            base = langtype
            while SHAPE_PREFIX_PATTERN.match(base):
                base = SHAPE_PREFIX_PATTERN.sub("", base, count=1)
            if base not in BASE_LANGTYPES:
                findings.append(
                    f"{row.identifier}: {langtype!r} is not a langtype layout accepts "
                    f"(base kind {base!r} is not in BASE_LANGTYPES)")
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
             [f"{langtype} #{SIGNATURE_PARAMETER_NAME}{index}"
              for index, langtype in enumerate(row.read_langtypes)],
             [f"{langtype} #{SIGNATURE_RESULT_NAME}{index}"
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
    declared = [row for row in rows if row.read_langtypes or row.write_langtypes]
    lines = [
        "// Component: specification/native_rwir_decls.kv",
        "// Role: Signature declarations for native rwir operators, embedded into layout at **build time**.",
        "// Links: Sources of truth are the name table in specification/myrwircaps.c and decisions in",
        "//        five-language-behavior-matrix.md, which generate capability rows in rwir_catalog.def",
        "//        and in turn generate this file. Edit the sources of truth rather than generated outputs.",
        "//        Generated and verified by tools/architecture/generate_rwir_catalog.py.",
        "// Constraints: Layout must neither read /lib/<op> from pathspace at runtime nor maintain a",
        "//              hand-written duplicate.",
        "// Syntax: rwir declarations matching rwfunc header syntax without a body (fulfilled by runtime).",
        "//         Every parameter and return value must carry a type; bare `#name`, `$name`, and `name:` are rejected.",
        f"// Stats: {len(declared)} declarations"
        " (remaining rows get signatures from TypeSet bits or install signatures).",
        "",
    ]
    for row in declared:
        parameters = ", ".join(
            f"{langtype} #{SIGNATURE_PARAMETER_NAME}{index}"
            for index, langtype in enumerate(row.read_langtypes))
        returns = ", ".join(
            f"{langtype} #{SIGNATURE_RESULT_NAME}{index}"
            for index, langtype in enumerate(row.write_langtypes))
        lines.append(f"rwir {row.operator}({parameters}) -> ({returns})")
    lines.append("")
    return "\n".join(lines)

SIGNATURE_PARAMETER_NAME = "argument"
SIGNATURE_RESULT_NAME = "result"

SIGNATURE_LINE_PATTERN = re.compile(
    r"^rwir (\S+)\(([^)]*)\) -> \(([^)]*)\)$")
SIGNATURE_PARAMETER_PATTERN = re.compile(
    r"^(\*{0,2})(\S.*?)\s*#([A-Za-z0-9_]+)$")

def parse_signature_parameter(text: str) -> tuple[str, str]:
    match = SIGNATURE_PARAMETER_PATTERN.match(text.strip())
    if match is None:
        raise SystemExit(
            f"formal parameter {text!r} does not match `*T #name` or `T #name`; "
            f"every parameter must declare a type (a bare `#name` is not enough)")
    stars, type_text, name_text = match.groups()
    if len(stars) > 1:
        raise SystemExit(
            f"formal parameter {text!r} has more than one '*' prefix")
    type_text = (type_text or "").strip()
    return (stars + type_text, "#" + name_text)

def read_back_signature_table(text: str) -> list[tuple[str, list[str], list[str]]]:
    recovered: list[tuple[str, list[str], list[str]]] = []
    for line in text.splitlines():
        if not line.startswith("rwir "):
            continue
        match = SIGNATURE_LINE_PATTERN.match(line)
        if match is None:
            raise SystemExit(f"generated declaration is malformed: {line!r}")
        surface, raw_parameters, raw_returns = match.groups()
        parameters = [parse_signature_parameter(item) for item in
                      raw_parameters.split(",") if item.strip()]
        returns = [parse_signature_parameter(item) for item in
                   raw_returns.split(",") if item.strip()]
        for type_text, name_text in parameters + returns:
            if name_text[1:] and not name_text[1:].replace("_", "").isalnum():
                raise SystemExit(
                    f"{surface}: formal name {name_text!r} is not an identifier")
            if type_text.endswith("#") or "$" in type_text or "#" in type_text:
                raise SystemExit(
                    f"{surface}: the name marker must precede the name, not sit in the type")
        recovered.append((surface,
                          [f"{t} {n}".strip() for t, n in parameters],
                          [f"{t} {n}".strip() for t, n in returns]))
    return recovered

def retirement_lines() -> list[str]:
    if not RETIRED_ROWS:
        return []
    lines = [" *", " * Retired (ids permanently reserved; never reused or renumbered):"]
    for identifier, operator, opid, *aliases in RETIRED_ROWS:
        detail = (f" *   {operator!r} is the canonical name for {identifier}, "
                  f"~0x{opid:04X}")
        del detail
        line = (f" *   0x{opid:04X} {identifier} — named container literals unify on map·new"
                f" (map/struct/scatterarray are key-value groups)")
        if aliases:
            line += f"; alias {', '.join(aliases)} retired alongside"
        lines.append(line)
    return lines

def render_catalog(rows: list[Row], aliases: list[tuple[str, str]],
                   opcodes: list[str], gaps: list[tuple[str, str]]) -> str:
    identity_names = {row.operator for row in rows}
    alias_count = len(aliases) - len(identity_names & {name for name, _ in aliases})
    lines = [
        "/*",
        " * Component: specification/rwir_catalog.def",
        " * Role: Canonical capability manifest for myrwircap — one row per semantic operator.",
        " * Links: Surface-name mappings to rows in this table live in rwir_alias_table.def; both",
        " *        tables are generated and verified by tools/architecture/generate_rwir_catalog.py.",
        " * Constraints: An rwir operator is uniquely identified by its operator key text; numeric",
        " *              ids below serve BinRWIR encoding only and are private runtimec details, not",
        " *              rwir identity. Ids and identifiers are assigned explicitly rather than from",
        " *              array indices, file order, or declaration order; read/write masks are",
        " *              TypeSet bitsets without wildcards or placeholders; capabilities lacking a",
        " *              matrix decision stay out of this file and are tracked in rwir_catalog_gaps.md.",
        " * Mask bits: 0 int8, 1 int16, 2 int32, 3 int64, 4 uint8, 5 uint16, 6 uint32,",
        " *            7 uint64, 8 float32, 9 float64, 10 bool, 11 char/utf8, 12 char/utf32,",
        " *            13 char/ascii, 14 array, 15 stringkeymap, 16 structref, 17 ext,",
        " *            18 pointer; bits 19 and above are reserved (except bit 19 any_single) and must be zero.",
        " * Zero mask: Row is outside the scalar union channel. Parameter and result types for",
        " *            parameterized rows are declared at install time by the def rwir slot under",
        " *            /lib/<operator> (single signature source; never duplicate a second signature",
        " *            table here); ResultRule IDENTITY takes the install signature rather than a",
        " *            wildcard. Control rows produce no value.",
        f" * Stats: {len(opcodes)} registered surface names → {len(aliases)} alias mappings;",
        f" *        {len(identity_names & {name for name, _ in aliases})} identity mappings, "
        f"{alias_count} aliases; {len(rows)} capability rows, {len(gaps)} gaps.",
    ] + retirement_lines() + [
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
        " * behavior_decisions: One or more comma-separated matrix decisions; the first is primary.",
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
            f'                        {render_roles(row.read_roles)}, '
            f'{render_roles(row.write_roles)},')
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
        " * Component: specification/rwir_alias_table.def",
        " * Role: Canonical mapping from surface name -> capability row (data, not capability rows).",
        " * Constraints: Exactly one record per registered name; identical names do not imply identical",
        " *              semantics, which are defined solely by rows in rwir_catalog.def.",
        " *              rwir_catalog.def is the single signature source; this table carries no types.",
        " * Specializations: RUNTIMEC_RWIR_SPECIALIZATION declares rows reselected by the compiler",
        " *                  based on static operand types; invert negates the result (`!=` on",
        " *                  character/pointer/boolean comparisons).",
        f" * Stats: {len(opcodes)} registered surface names, {len(aliases)} alias mappings, "
        f"{len(SPECIALIZATIONS)} specializations.",
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

def operation_suffix(identifier: str) -> str:
    prefix = "RUNTIMEC_RWIR_"
    return identifier[len(prefix):] if identifier.startswith(prefix) else identifier

TYPE_BITS = (
    (0, "int8"), (1, "int16"), (2, "int32"), (3, "int64"),
    (4, "uint8"), (5, "uint16"), (6, "uint32"), (7, "uint64"),
    (8, "float32"), (9, "float64"), (10, "bool"),
    (11, "char/utf8"), (12, "char/utf32"), (13, "char/ascii"),
    (14, "array"), (15, "stringkeymap"), (16, "structref"),
    (17, "ext"), (18, "pointer"),
)
OPERAND_CLASS = {
    "int8": "signed", "int16": "signed", "int32": "signed", "int64": "signed",
    "uint8": "unsigned", "uint16": "unsigned", "uint32": "unsigned",
    "uint64": "unsigned",
    "float32": "float", "float64": "float",
    "bool": "bool",
    "char/utf8": "character", "char/utf32": "character",
    "char/ascii": "character",
    "array": "array", "stringkeymap": "stringkeymap",
    "structref": "structref", "ext": "ext", "pointer": "pointer",
}
NUMERIC_CLASSES = ("signed", "unsigned", "float")
TYPE_WIDTH = {
    "int8": 1, "int16": 2, "int32": 4, "int64": 8,
    "uint8": 1, "uint16": 2, "uint32": 4, "uint64": 8,
    "float32": 4, "float64": 8,
}
UNARY_OPERATORS = ("abs", "neg", "sign", "sqrt", "exp", "log", "not")
CAST_OPERATOR = "cast"

def type_names(mask: int) -> list[str]:
    return [name for bit, name in TYPE_BITS if mask >> bit & 1]

def constraint_allows(constraint: str, operand_types: list[str]) -> bool:
    classes = [OPERAND_CLASS[name] for name in operand_types]
    if constraint == "NONE":
        return True
    if constraint == "SAME_CLASS":
        return all(entry == classes[0] for entry in classes)
    if constraint == "NUMERIC_COMPATIBLE":
        if not all(entry in NUMERIC_CLASSES for entry in classes):
            return False
        return not ("signed" in classes and "unsigned" in classes)
    if constraint == "NUMERIC_COMPARE":
        return all(entry in NUMERIC_CLASSES for entry in classes)
    return False

def result_of(rule: str, operand_types: list[str]) -> str | None:
    if rule == "BOOLEAN":
        return "bool"
    if rule == "FLOAT64":
        return "float64"
    if rule == "CHARACTER_UTF8":
        return "char/utf8"
    if rule == "SAME_FIRST":
        return operand_types[0]
    if rule == "SAME_SECOND" and len(operand_types) > 1:
        return operand_types[1]
    if rule in ("WIDEN", "WIDEN_INTEGER"):
        float_operands = [entry for entry in operand_types
                          if OPERAND_CLASS[entry] == "float"]
        if float_operands:
            return "float64" if "float64" in float_operands else "float32"
        widest = operand_types[0]
        for entry in operand_types[1:]:
            if TYPE_WIDTH[entry] > TYPE_WIDTH[widest]:
                widest = entry
        return widest
    return None

def specialization_combinations(row: Row) -> list[tuple[str, ...]]:
    read_types = type_names(row.read_mask)
    if not read_types:
        return []
    if row.operator == CAST_OPERATOR:
        tuples = [(source, target)
                  for source in read_types for target in read_types
                  if constraint_allows(row.constraint, [source, target])]
    elif row.operator in UNARY_OPERATORS:
        tuples = [(entry,) for entry in read_types
                  if constraint_allows(row.constraint, [entry])]
    else:
        tuples = [(left, right) for left in read_types for right in read_types
                  if constraint_allows(row.constraint, [left, right])]
    finished = []
    for operand_types in tuples:
        resolved = result_of(row.result_rule, list(operand_types))
        finished.append(operand_types if resolved is None
                        else operand_types + (resolved,))
    return sorted(set(finished))

def tuple_text(operand_types: tuple[str, ...]) -> str:
    return ",".join(operand_types)

def specialization_suffix(operand_types: tuple[str, ...]) -> str:
    parts = []
    for name in operand_types:
        parts.append(name.replace("/", "_").replace("-", "_").upper())
    return "_".join(parts)

def build_specializations(rows: list[Row]) -> list[tuple[Row, tuple[str, ...], int]]:
    entries: list[tuple[Row, tuple[str, ...]]] = []
    for row in rows:
        combinations = specialization_combinations(row)
        if len(combinations) <= 1:
            continue
        entries.extend((row, combination) for combination in combinations)
    entries.sort(key=lambda item: (item[0].opid, tuple_text(item[1])))
    return [(row, combination, 0x8000 + index)
            for index, (row, combination) in enumerate(entries)]

def render_operation_identifiers(rows: list[Row], retired: tuple) -> str:
    lines = [
        "/*",
        " * Component: binrwir/operation_identifiers",
        " * Role: Capability-row opid constants shared by `binrwir` and `vmbin`.",
        " * Links: Source of truth is `specification/rwir_catalog.def`; generated and verified",
        " *        by tools/architecture/generate_rwir_catalog.py.",
        " * Constraints: **Constants only**, no types or tables. Edit the source of truth, not this file.",
        " *",
        " * Rationale: `vmbin` and `vmkv` are sibling layers and `vmbin` must not link `vmkv`,",
        " * so handlers cannot query the manifest for their opids. Both alternatives — bare",
        " * hex literals in handlers (unreconciled drift) or linking `vmbin` to `vmkv` (layer",
        " * inversion) — are unacceptable.",
        " */",
        "",
        "#ifndef BINRWIR_OPERATION_IDENTIFIERS_H",
        "#define BINRWIR_OPERATION_IDENTIFIERS_H",
        "",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
    ]
    for row in rows:
        lines.append(
            f"#define BINRWIR_OPERATION_IDENTIFIER_{operation_suffix(row.identifier)} "
            f"UINT16_C(0x{row.opid:04X})")
    for identifier, operator, opid, note in retired:
        lines.append(
            f"/* Retired: {operator} — {note}. Permanently reserved, never reused. */")
        lines.append(
            f"#define BINRWIR_OPERATION_IDENTIFIER_{operation_suffix(identifier)} "
            f"UINT16_C(0x{opid:04X})")

    specializations = build_specializations(rows)
    lines += [
        "",
        "/*",
        " * ── Specialized opids (0x8000–0xFEFF) ────────────────────────────────────",
        " *",
        " * Each operator expands into one opid per concrete operand-type tuple:",
        " *   Cartesian product of read masks -> filter by constraint -> result_rule.",
        " *",
        " * Assignment: 0x8000 + ordinal in the list sorted by (base opid, tuple string).",
        " * Ordering must remain stable across processes.",
        " *",
        " * Tuple string = read parameters in declaration order followed by write parameters,",
        " * **spelled out in full even when identical** so unary and binary forms stay distinct.",
        f" * Stats: {len(specializations)} entries.",
        " */",
        f"#define BINRWIR_SPECIALIZED_COUNT UINT16_C({len(specializations)})",
        f"#define BINRWIR_SPECIALIZED_BASE UINT16_C(0x8000)",
    ]
    for row, operand_types, opid in specializations:
        lines.append(
            f"#define BINRWIR_OPERATION_IDENTIFIER_{operation_suffix(row.identifier)}_"
            f"{specialization_suffix(operand_types)} UINT16_C(0x{opid:04X})")

    lines += [
        "",
        "/*",
        " * Lookup table: specialization pass maps `(base opid, tuple string)` to target opid.",
        " *",
        " * Generated from the same source as the constants above; `--check` verifies parity.",
        " * Declared here so both `binrwir` and `vmbin` can read it without `vmbin` linking `vmkv`.",
        " */",
        "typedef struct binrwir_specialization {",
        "    /* Uses uint16_t rather than binrwir_operation_identifier_t to avoid an include",
        "     * cycle with binrwir/instruction.h; 16-bit opids are a wire-format invariant. */",
        "    uint16_t base_operation_identifier;",
        "    const char *operand_types;",
        "    uint16_t specialized_operation_identifier;",
        "} binrwir_specialization_t;",
        "",
        "#define BINRWIR_SPECIALIZATION_TABLE_COUNT "
        f"UINT16_C({len(specializations)})",
        "",
        "/* Defined in binrwir/specialization_table.c; header holds only declaration and count. */",
        "extern const binrwir_specialization_t "
        "binrwir_specializations[BINRWIR_SPECIALIZATION_TABLE_COUNT];",
        "",
        "#if defined(__cplusplus)",
        "}",
        "#endif",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)

def render_specialization_table(rows: list[Row]) -> str:
    specializations = build_specializations(rows)
    lines = [
        "/*",
        " * Component: binrwir/specialization_table (generated; do not edit by hand)",
        " * Role: Sole definition of the specialization lookup table. Constants live in operation_identifiers.h.",
        " * Constraints: Generated and verified by tools/architecture/generate_rwir_catalog.py;",
        " *              row count must equal BINRWIR_SPECIALIZATION_TABLE_COUNT.",
        " * Missing rows: `binrwir_binfunc_specialize` **rejects the entire Program** with",
        " *               error.code = 3 (NO_SPECIALIZATION_ROW) when (base opid, tuple string)",
        " *               is absent. Add masks in specification/rwir_catalog.def and regenerate",
        " *               rather than editing this file by hand. error.code = 1 indicates missing",
        " *               operand langtype or buffer overflow in layout/vmkv, unrelated to this table.",
        f" * Stats: {len(specializations)} rows.",
        " */",
        "",
        '#include "binrwir/operation_identifiers.h"',
        "",
        "const binrwir_specialization_t binrwir_specializations[] = {",
    ]
    for row, operand_types, opid in specializations:
        lines.append(
            f'    {{BINRWIR_OPERATION_IDENTIFIER_{operation_suffix(row.identifier)}, '
            f'"{tuple_text(operand_types)}", UINT16_C(0x{opid:04X})}},')
    lines += [
        "};",
        "",
    ]
    return "\n".join(lines)

def render_gaps(gaps: list[tuple[str, str]], opcodes: list[str],
                rows: list[Row]) -> str:
    lines = [
        "# rwir_catalog Gap List",
        "",
        "Surface names registered in `specification/myrwircaps.c` that do not yet map to",
        "a decided capability row. They are **excluded** from `rwir_alias_table.def` and",
        "cannot enter canonical BinRWIR until decided.",
        "",
        f"Registered surface names: {len(opcodes)}; capability rows: {len(rows)}; gaps: {len(gaps)}.",
        "",
        "| surface name | missing |",
        "|---|---|",
    ]
    if not gaps:
        lines.append("| — | none |")
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
    entry_limit = registry_entry_limit(root)
    findings = validate(rows, opcodes, aliases, decisions, gaps, reserved_base,
                        entry_limit)
    expected = {
        MANIFEST: render_catalog(rows, aliases, opcodes, gaps),
        ALIAS_TABLE: render_alias_table(aliases, opcodes),
        GAPS: render_gaps(gaps, opcodes, rows),
        SIGNATURE_TABLE: render_signature_table(rows),
        OPERATION_IDENTIFIERS_HEADER: render_operation_identifiers(rows,
                                                                   RETIRED_ROWS),
        SPECIALIZATION_TABLE: render_specialization_table(rows),
    }

    if not arguments.check:
        if findings:
            for finding in findings:
                print(f"[drift] {finding}")
            print(f"generate_rwir_catalog: refusing to write "
                  f"({len(findings)} finding(s) in the generated tables)")
            return 1
        for path, text in expected.items():
            destination = root / path
            descriptor, temporary = tempfile.mkstemp(
                dir=destination.parent, prefix=destination.name + ".", suffix=".tmp")
            try:
                with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                    stream.write(text)
                os.replace(temporary, destination)
            except BaseException:
                Path(temporary).unlink(missing_ok=True)
                raise
        print(f"generate_rwir_catalog: wrote {len(rows)} capability rows, "
              f"{len(aliases)} alias mappings, {len(gaps)} gaps "
              f"of {len(opcodes)} registered surface names")
        return 0

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
