
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rwir_catalog import specialization_suffix

SOURCE_ROOT = Path(__file__).resolve().parents[3]
SPECIALIZATION_TABLE = Path("runtimec/binrwir/specialization_table.c")
GENERATED_HANDLERS = Path("runtimec/vmbin/handlers/scalar_generated.c")

ROW_PATTERN = re.compile(
    r'\{(BINRWIR_OPERATION_IDENTIFIER_[A-Z0-9_]+),\s*"([^"]+)",\s*'
    r'UINT16_C\((0x[0-9A-Fa-f]+)\)\}')

C_TYPE = {
    "int8": "int8_t", "int16": "int16_t", "int32": "int32_t",
    "int64": "int64_t", "uint8": "uint8_t", "uint16": "uint16_t",
    "uint32": "uint32_t", "uint64": "uint64_t",
    "float32": "float", "float64": "double", "bool": "bool",
    "char/utf8": "uint32_t", "char/utf32": "uint32_t",
    "char/ascii": "uint8_t",
}

INTEGER = {
    "int8": (8, True), "int16": (16, True), "int32": (32, True),
    "int64": (64, True), "uint8": (8, False), "uint16": (16, False),
    "uint32": (32, False), "uint64": (64, False),
}
FLOAT = ("float32", "float64")
CHARACTER = ("char/utf8", "char/utf32", "char/ascii")

@dataclass(frozen=True)
class Row:
    base_identifier: str
    operand_types: str
    opid: str

    @property
    def operator(self) -> str:
        return self.base_identifier.removeprefix(
            "BINRWIR_OPERATION_IDENTIFIER_")

    @property
    def types(self) -> list[str]:
        return self.operand_types.split(",")

def read_specializations(root: Path) -> list[Row]:
    text = (root / SPECIALIZATION_TABLE).read_text(encoding="utf-8")
    rows = [Row(base, types, identifier)
            for base, types, identifier in ROW_PATTERN.findall(text)]
    if not rows:
        raise SystemExit(f"{SPECIALIZATION_TABLE}: no specialization rows; "
                         f"run generate_rwir_catalog.py first")
    return rows

def output_name(row: Row) -> str:
    reads, _ = ARITY[row.operator]
    return "out" if reads == 1 else "c"

def signature(row: Row, has_failure: bool = False) -> str:
    reads, writes = ARITY[row.operator]
    read_names = ["a", "b", "c"][:reads]
    write_names = [output_name(row)][:writes]
    parameters = [f"const {C_TYPE[type_name]} *{name}"
                  for name, type_name in zip(read_names, row.types[:reads])]
    parameters.extend(f"{C_TYPE[type_name]} *{name}"
                      for name, type_name in
                      zip(write_names, row.types[reads:]))
    if has_failure:
        parameters.append("binrwir_execution_exit_t *output_exit")
    return ", ".join(parameters)

def logic_name(row: Row) -> str:
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"scalar_generated_{row.operator.lower()}_{suffix}_logic"

def handler_name(row: Row) -> str:
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"scalar_generated_{row.operator.lower()}_{suffix}"

def thunk_macro(row: Row, has_failure: bool) -> str:
    reads, writes = ARITY[row.operator]
    shape = None
    if reads == 1 and writes == 1:
        shape = "UNARY"
    elif reads == 2 and writes == 1:
        shape = "BINARY"
    elif reads == 3 and writes == 1:
        shape = "TERNARY"
    if shape is None:
        raise SystemExit(f"{row.operator}: no typed handler macro for "
                         f"{reads} read(s) / {writes} write(s)")
    return (f"VMBIN_HANDLER_{shape}_TYPED" +
            ("_EXIT" if has_failure else ""))

def unused_parameter(row: Row) -> str | None:
    del row
    return None
    """`scalar_generated_add_int64_int64_int64`——名字由算子与元组给出。

    不用 opid 起名：`scalar_generated_0x81d7` 在 gdb 里看不出是哪条算子，
    而这条函数存在的全部意义就是「一眼知道它是哪一种类型组合」。
    """
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"scalar_generated_{row.operator.lower()}_{suffix}"

UNSIGNED_OF_WIDTH = {8: "uint8_t", 16: "uint16_t", 32: "uint32_t",
                     64: "uint64_t"}

def integer_arithmetic(row: Row, operator: str) -> str | None:
    output = output_name(row)
    left_type, right_type, result_type = row.types
    for type_name in (left_type, right_type, result_type):
        if type_name not in INTEGER:
            return None
    width, signed_result = INTEGER[result_type]
    left_cast = "int64_t" if INTEGER[left_type][0] < width else C_TYPE[left_type]
    right_cast = ("int64_t" if INTEGER[right_type][0] < width
                  else C_TYPE[right_type])
    if signed_result:
        carrier = UNSIGNED_OF_WIDTH[width]
        return (f"*{output} = ({C_TYPE[result_type]})"
                f"(({carrier})({left_cast})*a {operator} "
                f"({carrier})({right_cast})*b);")
    return (f"*{output} = ({C_TYPE[result_type]})"
            f"(({left_cast})*a {operator} ({right_cast})*b);")

def floating_arithmetic(row: Row, operator: str) -> str | None:
    output = output_name(row)
    result_type = row.types[2]
    if result_type not in FLOAT:
        return None
    return (f"*{output} = ({C_TYPE[result_type]})*a {operator} "
            f"({C_TYPE[result_type]})*b;")

def arithmetic_template(row: Row) -> str | None:
    operator = {"ADD": "+", "SUB": "-", "MUL": "*"}[row.operator]
    if row.types[2] in FLOAT:
        return floating_arithmetic(row, operator)
    return integer_arithmetic(row, operator)

def integer_holds(source: str, target: str) -> bool:
    source_width, source_signed = INTEGER[source]
    target_width, target_signed = INTEGER[target]
    if source_signed and target_signed:
        return source_width <= target_width
    if source_signed and not target_signed:
        return False
    if not source_signed and not target_signed:
        return source_width <= target_width
    return source_width < target_width

def cast_template(row: Row) -> str | None:
    output = output_name(row)
    source, target = row.types[0], row.types[1]
    if target in CHARACTER or source in CHARACTER:
        return None
    if source == "bool":
        return f"*{output} = *a;" if target == "bool" else None
    if target == "bool":
        return None
    if source in FLOAT:
        return f"*{output} = ({C_TYPE[target]})*a;" if target in FLOAT else None
    if source in INTEGER and target in FLOAT:
        return f"*{output} = ({C_TYPE[target]})*a;"
    if source in INTEGER and target in INTEGER:
        return (f"*{output} = ({C_TYPE[target]})*a;"
                if integer_holds(source, target) else None)
    return None

def cast_judged_template(row: Row) -> str | None:
    source, target = row.types[0], row.types[1]
    if target in CHARACTER or target == "bool" or source in CHARACTER:
        return None
    if source == "bool":
        return None
    width_target, signed_target = INTEGER.get(target, (0, False))
    if width_target == 0:
        return None
    output = output_name(row)
    if source in FLOAT:
        helper = ("runtimec_scalar_float_to_signed" if signed_target
                  else "runtimec_scalar_float_to_unsigned")
        return (f"{helper}((double)*a, {width_target}U, "
                f"({C_TYPE[target]} *){output}, output_exit);")
    if source in INTEGER:
        source_width, source_signed = INTEGER[source]
        if integer_holds(source, target):
            return None
        if signed_target:
            return (f"*{output} = ({C_TYPE[target]})"
                    f"runtimec_scalar_wrap_signed((int64_t)*a, {source_width}U, "
                    f"{width_target}U);")
        return (f"*{output} = ({C_TYPE[target]})"
                f"runtimec_scalar_wrap_unsigned((uint64_t)*a, "
                f"{width_target}U);")
    return None

def pow_template(row: Row) -> str | None:
    output = output_name(row)
    return f"*{output} = pow((double)*a, (double)*b);"

TEMPLATES = {
    "ADD": arithmetic_template,
    "SUB": arithmetic_template,
    "MUL": arithmetic_template,
    "CAST": cast_template,
    "POWER": pow_template,
}

COMPARISON_OPERATOR = {
    "EQUAL": "==", "NOT_EQUAL": "!=", "LESS": "<", "LESS_EQUAL": "<=",
    "GREATER": ">", "GREATER_EQUAL": ">=",
}

def comparison_class(type_name: str) -> str:
    if type_name in FLOAT:
        return "float"
    if type_name in ("int8", "int16", "int32", "int64"):
        return "signed"
    if type_name in ("uint8", "uint16", "uint32", "uint64"):
        return "unsigned"
    return "other"

def comparison_template(row: Row) -> str | None:
    operator = COMPARISON_OPERATOR[row.operator]
    left_type, right_type = row.types[0], row.types[1]
    left_class = comparison_class(left_type)
    right_class = comparison_class(right_type)
    if "other" in (left_class, right_class):
        return None
    if left_class == "float" or right_class == "float":
        return (f"*{output_name(row)} = "
                f"(bool)((double)*a {operator} (double)*b);")
    if left_class == right_class:
        wider = C_TYPE[left_type]
        if INTEGER[right_type][0] > INTEGER[left_type][0]:
            wider = C_TYPE[right_type]
        return (f"*{output_name(row)} = "
                f"(bool)(({wider})*a {operator} ({wider})*b);")
    if left_class == "signed":
        call = "runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b)"
    else:
        call = "runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b)"
    return f"*{output_name(row)} = (bool)({call} {operator} 0);"

for _operator in COMPARISON_OPERATOR:
    TEMPLATES[_operator] = comparison_template

LOGICAL_OPERANDS = frozenset(
    ("int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64",
     "float32", "float64", "bool"))

def truthy_expression(type_name: str, name: str) -> str:
    if type_name in FLOAT:
        return f"({name} != 0.0)"
    if type_name == "bool":
        return f"{name}"
    return f"({name} != 0)"

def logical_template(row: Row) -> str | None:
    output = output_name(row)
    operator = row.operator
    if operator == "LOGICAL_NOT":
        source = row.types[0]
        if source not in LOGICAL_OPERANDS:
            return None
        return f"*{output} = (bool)!{truthy_expression(source, '*a')};"
    left_type, right_type = row.types[0], row.types[1]
    if left_type not in LOGICAL_OPERANDS or right_type not in LOGICAL_OPERANDS:
        return None
    glue = "&&" if operator == "LOGICAL_AND" else "||"
    return (f"*{output} = (bool)"
            f"({truthy_expression(left_type, '*a')} {glue} "
            f"{truthy_expression(right_type, '*b')});")

TEMPLATES["LOGICAL_AND"] = logical_template
TEMPLATES["LOGICAL_OR"] = logical_template
TEMPLATES["LOGICAL_NOT"] = logical_template

def bitwise_template(row: Row) -> str | None:
    operator = {"BITWISE_AND": "&", "BITWISE_OR": "|",
                "BITWISE_XOR": "^"}[row.operator]
    left_type, right_type, result_type = row.types
    for type_name in (left_type, right_type, result_type):
        if type_name not in INTEGER:
            return None
    wider = C_TYPE[left_type]
    if INTEGER[right_type][0] > INTEGER[left_type][0]:
        wider = C_TYPE[right_type]
    return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
            f"(({wider})*a {operator} ({wider})*b);")

TEMPLATES["BITWISE_AND"] = bitwise_template
TEMPLATES["BITWISE_OR"] = bitwise_template
TEMPLATES["BITWISE_XOR"] = bitwise_template

def min_max_template(row: Row) -> str | None:
    want_maximum = row.operator == "MAX"
    left_type, right_type, result_type = row.types
    for type_name in (left_type, right_type, result_type):
        if type_name not in INTEGER and type_name not in FLOAT:
            return None
    if result_type in FLOAT:
        helper = ("runtimec_scalar_float_max" if want_maximum
                  else "runtimec_scalar_float_min")
        return f"*{output_name(row)} = {helper}((double)*a, (double)*b);"
    wider = C_TYPE[result_type]
    operator = ">" if want_maximum else "<"
    return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
            f"(({wider})*a {operator} ({wider})*b ? *a : *b);")

TEMPLATES["MIN"] = min_max_template
TEMPLATES["MAX"] = min_max_template

TRANSCENDENTAL_CALL = {
    "SQUARE_ROOT": "sqrt", "EXPONENTIAL": "exp", "LOGARITHM": "log",
}

def transcendental_template(row: Row) -> str | None:
    function = TRANSCENDENTAL_CALL[row.operator]
    return f"*{output_name(row)} = {function}((double)*a);"

TEMPLATES["SQUARE_ROOT"] = transcendental_template
TEMPLATES["EXPONENTIAL"] = transcendental_template
TEMPLATES["LOGARITHM"] = transcendental_template

def unary_sign_template(row: Row) -> str | None:
    source_type, result_type = row.types
    output = output_name(row)
    if source_type in FLOAT:
        if row.operator == "NEGATION":
            return f"*{output} = -*a;"
        if row.operator == "ABSOLUTE":
            return f"*{output} = fabs(*a);"
        return f"*{output} = ({C_TYPE[result_type]})((*a > 0) - (*a < 0));"
    if source_type not in INTEGER:
        return None
    width, signed_source = INTEGER[source_type]
    if row.operator == "NEGATION":
        carrier = UNSIGNED_OF_WIDTH[width]
        return (f"*{output} = ({C_TYPE[result_type]})"
                f"(0 - ({carrier})*a);")
    if row.operator == "ABSOLUTE":
        if not signed_source:
            return f"*{output} = *a;"
        carrier = UNSIGNED_OF_WIDTH[width]
        return (f"*{output} = ({C_TYPE[result_type]})"
                f"(*a < 0 ? 0 - ({carrier})*a : ({carrier})*a);")
    if not signed_source:
        return f"*{output} = ({C_TYPE[result_type]})(*a > 0);"
    return (f"*{output} = ({C_TYPE[result_type]})"
            f"((*a > 0) - (*a < 0));")

TEMPLATES["ABSOLUTE"] = unary_sign_template
TEMPLATES["NEGATION"] = unary_sign_template
TEMPLATES["SIGN"] = unary_sign_template

def shift_template(row: Row) -> str | None:
    left_type, right_type, result_type = row.types
    if left_type not in INTEGER or right_type not in INTEGER:
        return None
    if result_type not in INTEGER:
        return None
    width, signed_source = INTEGER[left_type]
    count_argument = "(uint64_t)*b"
    if row.operator == "SHIFT_LEFT":
        helper = ("runtimec_scalar_shift_left_signed" if signed_source
                  else "runtimec_scalar_shift_left_unsigned")
        argument = f"({C_TYPE[left_type]})*a" if signed_source else f"*a"
        return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
                f"{helper}({argument}, {count_argument}, {width}U);")
    helper = ("runtimec_scalar_shift_right_signed" if signed_source
              else "runtimec_scalar_shift_right_unsigned")
    argument = f"({C_TYPE[left_type]})*a" if signed_source else f"*a"
    return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
            f"{helper}({argument}, {count_argument}, {width}U);")

TEMPLATES["SHIFT_LEFT"] = shift_template
TEMPLATES["SHIFT_RIGHT"] = shift_template

def cast_wide_template(row: Row) -> str | None:
    if row.operator != "CAST":
        return None
    source, target = row.types[0], row.types[1]
    if target in CHARACTER or target == "bool" or source in CHARACTER:
        return None
    if source == "bool" or source in FLOAT:
        return None
    width_target, signed_target = INTEGER.get(target, (0, False))
    if width_target == 0:
        return None
    if integer_holds(source, target):
        return None
    source_width, source_signed = INTEGER[source]
    output = output_name(row)
    if signed_target:
        return (f"*{output} = ({C_TYPE[target]})"
                f"runtimec_scalar_wrap_signed((int64_t)*a, {source_width}U, "
                f"{width_target}U);")
    return (f"*{output} = ({C_TYPE[target]})"
            f"runtimec_scalar_wrap_unsigned((uint64_t)*a, "
            f"{width_target}U);")

def cast_guard_template(row: Row) -> str | None:
    source, target = row.types[0], row.types[1]
    if target.startswith("char/"):
        return None
    if target == "bool":
        if source == "bool":
            return None
        return ("(void)a; (void)out; "
                "runtimec_scalar_cast_to_bool_invalid(output_exit);")
    if source == "bool":
        return None
    if source in FLOAT and target in INTEGER:
        width, signed_target = INTEGER[target]
        helper = ("runtimec_scalar_float_to_signed" if signed_target
                  else "runtimec_scalar_float_to_unsigned")
        cast = "int64_t" if signed_target else "uint64_t"
        return (f"{{{cast} converted_; "
                f"{helper}((double)*a, {width}U, &converted_, output_exit); "
                f"if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) "
                f"{{ return; }} "
                f"*{output_name(row)} = ({C_TYPE[target]})converted_;}}")
    return None

def cast_from_bool_template(row: Row) -> str | None:
    source, target = row.types[0], row.types[1]
    if source != "bool" or target == "bool":
        return None
    if target.startswith("char/"):
        return None
    if target not in C_TYPE:
        return None
    return (f"*{output_name(row)} = ({C_TYPE[target]})(*a ? 1 : 0);")

def cast_all_template(row: Row) -> str | None:
    for template in (cast_template, cast_from_bool_template,
                     cast_guard_template, cast_wide_template):
        body = template(row)
        if body is not None:
            return body
    return None

TEMPLATES["CAST"] = cast_all_template

def div_template(row: Row) -> str | None:
    return division_template(row, "DIV")

def mod_template(row: Row) -> str | None:
    return division_template(row, "MOD")

def division_template(row: Row, operator: str) -> str | None:
    del operator
    left, right, result_type = row.types
    output = output_name(row)
    if result_type in FLOAT:
        return (f"*{output} = ({C_TYPE[result_type]})*a / "
                f"({C_TYPE[result_type]})*b;")
    if left not in INTEGER or right not in INTEGER:
        return None
    width, signed_operand = INTEGER[left]
    if row.operator == "DIV":
        helper = ("runtimec_scalar_divide_signed" if signed_operand
                  else "runtimec_scalar_divide_unsigned")
    else:
        helper = ("runtimec_scalar_remainder_signed" if signed_operand
                  else "runtimec_scalar_remainder_unsigned")
    if signed_operand:
        return (f"{{{C_TYPE['int64']} quotient_; "
                f"{helper}((int64_t)*a, (int64_t)*b, {width}U, "
                f"&quotient_, output_exit); "
                f"if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {{ return; }} "
                f"*{output} = ({C_TYPE[result_type]})"
                f"runtimec_scalar_wrap_signed(quotient_, {width}U, "
                f"{INTEGER[result_type][0]}U);}}")
    return (f"{{uint64_t remainder_; "
            f"{helper}((uint64_t)*a, (uint64_t)*b, "
            f"&remainder_, output_exit); "
            f"if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {{ return; }} "
            f"*{output} = ({C_TYPE[result_type]})"
            f"runtimec_scalar_wrap_unsigned(remainder_, "
            f"{INTEGER[result_type][0]}U);}}")

TEMPLATES["DIV"] = div_template
TEMPLATES["MOD"] = mod_template

CHARACTER_DECODER = {
    "char/utf8": "string_text_decode_utf8",
    "char/utf32": "string_text_decode_utf32",
    "char/ascii": "string_text_decode_ascii",
}

CHARACTER_ENCODING = {
    "char/utf8": "RUNTIMEC_CHARACTER_ENCODING_UTF8",
    "char/utf32": "RUNTIMEC_CHARACTER_ENCODING_UTF32",
    "char/ascii": "RUNTIMEC_CHARACTER_ENCODING_ASCII",
}

def character_thunk(row: Row) -> str | None:
    if row.operator in ("CONCAT",):
        left, right = row.types[0], row.types[1]
        if left not in CHARACTER_DECODER or right not in CHARACTER_DECODER:
            return None
        return (f"VMBIN_HANDLER_CHARACTER_CONCAT({handler_name(row)}, "
                f"runtimec_scalar_concat, {CHARACTER_DECODER[left]}, "
                f"{CHARACTER_DECODER[right]})\n")

    if row.operator in ("CHARACTER_EQUAL", "CHARACTER_NOT_EQUAL"):
        left, right = row.types[0], row.types[1]
        if left not in CHARACTER_DECODER or right not in CHARACTER_DECODER:
            return None
        invert = ("false" if row.operator == "CHARACTER_EQUAL" else "true")
        return (f"VMBIN_HANDLER_CHARACTER_EQUAL({handler_name(row)}, "
                f"runtimec_scalar_character_equal, "
                f"{CHARACTER_DECODER[left]}, {CHARACTER_DECODER[right]}, "
                f"{invert})\n")

    if row.operator != "CAST":
        return None
    source, target = row.types[0], row.types[1]

    if source in CHARACTER_DECODER and target in CHARACTER_ENCODING:
        return (f"VMBIN_HANDLER_CHARACTER_TO_CHARACTER({handler_name(row)}, "
                f"runtimec_scalar_character_to_value, "
                f"{CHARACTER_DECODER[source]}, {CHARACTER_ENCODING[target]})\n")

    if source in CHARACTER_DECODER and target in INTEGER:
        width, signed_target = INTEGER[target]
        return (f"VMBIN_HANDLER_CHARACTER_TO_NUMBER({handler_name(row)}, "
                f"runtimec_scalar_character_to_number, "
                f"{CHARACTER_DECODER[source]}, {width}U, "
                f"{str(signed_target).lower()}, false)\n")
    if source in CHARACTER_DECODER and target in FLOAT:
        width = 32 if target == "float32" else 64
        return (f"VMBIN_HANDLER_CHARACTER_TO_NUMBER({handler_name(row)}, "
                f"runtimec_scalar_character_to_number, "
                f"{CHARACTER_DECODER[source]}, {width}U, false, true)\n")

    if target in CHARACTER_ENCODING and not source.startswith("char/"):
        source_type = C_TYPE[source]
        if source == "bool":
            to_code_point = "(*source_ ? 1U : 0U)"
        else:
            to_code_point = "(uint32_t)(int64_t)*source_"
        return (f"VMBIN_HANDLER_UNARY_TO_CHARACTER({handler_name(row)}, "
                f"runtimec_scalar_value_to_character, {source_type}, "
                f"{to_code_point}, {CHARACTER_ENCODING[target]})\n")

    return None

def _emit_character_thunk(row: Row, call: str) -> str:
    name = handler_name(row)
    return (f"static runtimec_status_t {name}(\n"
            f"    binrwir_binvthread_frame_t *frame,\n"
            f"    const binrwir_instruction_view_t *instruction,\n"
            f"    binrwir_execution_exit_t *output_exit) {{\n"
            f"    {call}\n"
            f"    return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR\n"
            f"               ? output_exit->status\n"
            f"               : RUNTIMEC_STATUS_SUCCESS;\n"
            f"}}\n")

def uses_exit(row: Row, body: str) -> bool:
    del row
    return "output_exit" in body

ARITY = {
    "ADD": (2, 1), "SUB": (2, 1), "MUL": (2, 1), "DIV": (2, 1),
    "MOD": (2, 1), "MIN": (2, 1), "MAX": (2, 1), "POWER": (2, 1),
    "BITWISE_AND": (2, 1), "BITWISE_OR": (2, 1), "BITWISE_XOR": (2, 1),
    "SHIFT_LEFT": (2, 1), "SHIFT_RIGHT": (2, 1),
    "EQUAL": (2, 1), "NOT_EQUAL": (2, 1), "LESS": (2, 1),
    "LESS_EQUAL": (2, 1), "GREATER": (2, 1), "GREATER_EQUAL": (2, 1),
    "LOGICAL_AND": (2, 1), "LOGICAL_OR": (2, 1),
    "CAST": (1, 1), "ABSOLUTE": (1, 1), "NEGATION": (1, 1), "SIGN": (1, 1),
    "SQUARE_ROOT": (1, 1), "EXPONENTIAL": (1, 1), "LOGARITHM": (1, 1),
    "LOGICAL_NOT": (1, 1),
    "CONCAT": (2, 1), "CHARACTER_EQUAL": (2, 1), "CHARACTER_NOT_EQUAL": (2, 1),
}

HAND_WRITTEN = {
    "DIV": "整数除零与 INT64_MIN/-1 溢出；浮点侧才可模板化",
    "MOD": "取余零与溢出",
    "MIN": "浮点 NaN 让位",
    "MAX": "浮点 NaN 让位",
    "ABSOLUTE": "最小值的回绕",
    "NEGATION": "最小值的回绕",
    "SIGN": "三分支",
    "BITWISE_AND": "混合宽度的符号处理",
    "BITWISE_OR": "混合宽度的符号处理",
    "BITWISE_XOR": "混合宽度的符号处理",
    "SHIFT_LEFT": "计数掩码与溢出",
    "SHIFT_RIGHT": "算术/逻辑右移与计数掩码",
    "EQUAL": "跨符号比较要先换域",
    "NOT_EQUAL": "跨符号比较要先换域",
    "LESS": "跨符号比较要先换域",
    "LESS_EQUAL": "跨符号比较要先换域",
    "GREATER": "跨符号比较要先换域",
    "GREATER_EQUAL": "跨符号比较要先换域",
    "LOGICAL_AND": "非零即真按操作数种类取判据",
    "LOGICAL_OR": "非零即真按操作数种类取判据",
    "LOGICAL_NOT": "非零即真按操作数种类取判据",
    "SQUARE_ROOT": "结果恒 float64，可模板化（下一批）",
    "EXPONENTIAL": "结果恒 float64，可模板化（下一批）",
    "LOGARITHM": "结果恒 float64，可模板化（下一批）",
    "CONCAT": "字符解码",
    "CHARACTER_EQUAL": "字符解码",
    "CHARACTER_NOT_EQUAL": "字符解码",
}

GENERATED_HEADER = '''/*
 * 组件：vmbin/handlers/scalar_generated（生成物，勿手改）
 * 作用：特化标量 handler——一条特化 opid 一个 inline 逻辑函数 + 一个 thunk。
 * 联系：由 tools/architecture/generate_scalar_handlers.py 从
 *       binrwir/specialization_table.c 生成；条目由 session 装进注册表。
 * 约束：逻辑函数体是一次转换 + 一次运算 + 一次赋值，没有类型判断、没有 status
 *       返回、没有 exit 写入。需要判据的算子不进本文件（分工见生成器头注释）。
 *       thunk 由 `handler_call` 的宏产出——定位、宽度核对、失败出口都在那里，
 *       这里是**同一套样板**的第二份实现会立刻漂移。
 * 统计：{count} 个特化 handler，{skipped} 条特化行归 scalar.c 的手写特例。
 */

#include "vmbin/handlers/scalar_generated.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "vmbin/handlers/handler_call.h"
#include "vmbin/handlers/scalar_character.h"
#include "vmbin/handlers/scalar_special.h"

'''

GENERATED_FOOTER = '''
const binrwir_registry_entry_t scalar_generated_entries[] = {{
{entries}}};

const size_t scalar_generated_entry_count =
    sizeof(scalar_generated_entries) / sizeof(scalar_generated_entries[0]);

/*
 * ── 测试探针：同一批逻辑函数的可见副本 ────────────────────────────────
 *
 * 逻辑函数是 `static inline`，测试翻译单元看不见，于是「生成的东西算得对不对」
 * 就只能靠条目表的形状来间接猜——那等于没测。第一版测试正是这么写的，用
 * 「把 min 的 `? *a : *b` 换成 `? *b : *a`」做变异，**测试照样全绿**。
 *
 * 所以这里把**同一份模板表达式**再产出一次，只改可见性。不是第二份实现：
 * 两处用的是同一条模板函数，文本由同一次渲染产生；改模板两边一起变。
 *
 * 只导出测试实际调用的那几条，不是全部 1404 个——全导出会让目标文件多出
 * 上千个符号，且没人会去写那么多断言，反而把「导出清单」变成第三处真相。
 */
{probes}
'''

PROBE_ROWS = (
    ("ADD", "int64,int32,int64"),
    ("MUL", "int32,int32,int32"),
    ("MIN", "int64,int32,int64"),
    ("MAX", "int32,int32,int32"),
    ("LESS", "int64,uint64,bool"),
    ("EQUAL", "float64,int8,bool"),
    ("LOGICAL_AND", "uint8,float64,bool"),
    ("BITWISE_OR", "int8,int64,int64"),
    ("SQUARE_ROOT", "uint64,float64"),
    ("NEGATION", "int64,int64"),
    ("ABSOLUTE", "int8,int8"),
    ("SIGN", "float64,float64"),
    ("SHIFT_LEFT", "int8,int8,int8"),
    ("SHIFT_RIGHT", "int64,int8,int64"),
    ("CAST", "int64,int8,int8"),
    ("CAST", "uint8,int8,int8"),
    ("CAST", "int8,uint8,uint8"),
    ("DIV", "int16,int16,int16"),
    ("DIV", "int64,int64,int64"),
    ("DIV", "float64,float64,float64"),
    ("MOD", "int64,int64,int64"),
    ("MOD", "uint8,uint8,uint8"),
    ("ADD", "float32,int64,float32"),
    ("MIN", "int32,float32,float32"),
    ("MUL", "float32,uint64,float32"),
    ("SUB", "int8,int8,int8"),
    ("ADD", "int8,int8,int8"),
    ("SUB", "uint8,uint8,uint8"),
    ("ADD", "float32,float32,float32"),
    ("LESS", "int32,int32,bool"),
    ("EQUAL", "int64,int64,bool"),
    ("BITWISE_AND", "uint8,uint8,uint8"),
    ("LOGICAL_OR", "float64,float64,bool"),
    ("LOGICAL_NOT", "int64,bool"),
    ("SHIFT_LEFT", "uint8,uint8,uint8"),
    ("SHIFT_RIGHT", "int8,int8,int8"),
    ("CAST", "float64,int8,int8"),
    ("CAST", "float32,uint8,uint8"),
)

def probe_name(row: Row) -> str:
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"runtimec_scalar_probe_{row.operator.lower()}_{suffix}"

def render_probes(rows: list[Row]) -> str:
    by_key = {(row.operator, row.operand_types): row for row in rows}
    rendered: list[str] = []
    for operator, operand_types in PROBE_ROWS:
        row = by_key.get((operator, operand_types))
        if row is None:
            raise SystemExit(
                f"probe row {operator}({operand_types}) is not in the "
                f"specialization table; the table no longer has this "
                f"combination")
        template = TEMPLATES.get(operator)
        body = template(row) if template is not None else None
        if body is None:
            raise SystemExit(
                f"probe row {operator}({operand_types}) is not produced by a "
                f"template; a probe can only mirror what the generator writes")
        parameters = signature(row, uses_exit(row, body))
        reads, writes = ARITY[row.operator]
        read_names = ["a", "b", "c"][:reads]
        write_names = [output_name(row)][:writes]
        arguments = ", ".join(read_names + write_names)
        if uses_exit(row, body):
            arguments += ", output_exit"
        rendered.append(
            f"void {probe_name(row)}("
            f"{parameters.replace('static', '')}) {{\n"
            f"    {logic_name(row)}({arguments});\n"
            f"}}\n")
    return "\n".join(rendered)

def render(root: Path) -> tuple[str, int, int]:
    rows = read_specializations(root)
    functions: list[str] = []
    entries: list[str] = []
    skipped = 0
    for row in rows:
        template = TEMPLATES.get(row.operator)
        character = character_thunk(row)
        if character is not None:
            functions.append(character)
            entries.append(
                f"    {{{row.opid}U, BINRWIR_REGISTRY_FLAG_NONE, "
                f"{handler_name(row)}}},\n")
            continue
        body = template(row) if template is not None else None
        if body is None:
            skipped += 1
            continue
        failure = uses_exit(row, body)
        functions.append(
            f"static inline void {logic_name(row)}({signature(row, failure)}) {{\n"
            f"    {body}\n"
            f"}}\n")
        read_types = ", ".join(C_TYPE[type_name] for type_name in
                               row.types[:ARITY[row.operator][0]])
        write_type = C_TYPE[row.types[-1]]
        functions.append(
            f"{thunk_macro(row, failure)}"
            f"({handler_name(row)}, {logic_name(row)}, "
            f"{read_types}, {write_type})\n")
        entries.append(
            f"    {{{row.opid}U, BINRWIR_REGISTRY_FLAG_NONE, "
            f"{handler_name(row)}}},\n")
    text = GENERATED_HEADER.format(count=len(entries), skipped=skipped)
    text += "\n".join(functions)
    text += GENERATED_FOOTER.format(entries="".join(entries),
                                    probes=render_probes(rows))
    return text, len(entries), skipped

def report(root: Path) -> int:
    rows = read_specializations(root)
    counts: dict[str, list[int]] = {}
    for row in rows:
        bucket = counts.setdefault(row.operator, [0, 0])
        if character_thunk(row) is not None:
            bucket[0] += 1
            continue
        template = TEMPLATES.get(row.operator)
        if template is not None and template(row) is not None:
            bucket[0] += 1
        else:
            bucket[1] += 1
    generated = sum(bucket[0] for bucket in counts.values())
    hand = sum(bucket[1] for bucket in counts.values())
    for operator in sorted(counts):
        produced, manual = counts[operator]
        reason = HAND_WRITTEN.get(operator, "暂未模板化")
        print(f"{operator.lower():22s} 生成 {produced:4d}  手写 {manual:4d}"
              f"  {reason if manual else ''}")
    print(f"{'合计':20s} 生成 {generated:4d}  手写 {hand:4d}  （共 {len(rows)} 行）")
    return 0

def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=SOURCE_ROOT)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--report", action="store_true")
    parsed = parser.parse_args(arguments or sys.argv[1:])
    root = parsed.root.resolve()

    if parsed.report:
        return report(root)

    rows = read_specializations(root)
    expected_identifiers = {row.opid.upper() for row in rows}
    expected, generated, _ = render(root)
    produced_identifiers = {
        match.upper() for match in
        re.findall(r"^\s*\{(0x[0-9A-Fa-f]+)U, BINRWIR_REGISTRY_FLAG_NONE",
                   expected, re.MULTILINE)}
    if produced_identifiers != expected_identifiers:
        missing = sorted(expected_identifiers - produced_identifiers)
        extra = sorted(produced_identifiers - expected_identifiers)
        print(f"generate_scalar_handlers: FAIL — table has "
              f"{len(expected_identifiers)} opid(s), generated "
              f"{len(produced_identifiers)}; missing {missing[:8]}, "
              f"unexpected {extra[:8]}")
        return 1
    if generated != len(expected_identifiers):
        print(f"generate_scalar_handlers: FAIL — {generated} handler(s) for "
              f"{len(expected_identifiers)} opid(s)")
        return 1

    expected, generated, skipped = render(root)
    destination = root / GENERATED_HANDLERS
    if parsed.check:
        actual = (destination.read_text(encoding="utf-8")
                  if destination.is_file() else "")
        if actual != expected:
            print(f"{GENERATED_HANDLERS}: out of date; re-run without --check")
            print("generate_scalar_handlers: FAIL")
            return 1
        print(f"generate_scalar_handlers: OK ({generated} handler(s), "
              f"{skipped} row(s) hand-written)")
        return 0

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(expected, encoding="utf-8")
    print(f"generate_scalar_handlers: wrote {generated} handler(s) into "
          f"{GENERATED_HANDLERS}; {skipped} row(s) stay hand-written")
    return 0

if __name__ == "__main__":
    sys.exit(main())
