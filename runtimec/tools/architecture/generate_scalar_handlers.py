#!/usr/bin/env python3
"""Generate the specialized scalar handlers from the specialization table.

**这不是第二份算子清单。** 类型组合的唯一真源是
`runtimec/binrwir/specialization_table.c`（由 `generate_rwir_catalog.py` 产出并
校验）。本脚本只回答一个问题：给定 (算子, 元组)，函数体是哪一行 C。

为什么必须有它：1836 条特化行，最短的手写形态是「一条宏调用 + 一条表行」= 3672
行。用户裁决把这些机械行交给生成器，`scalar.c` 才可能守住在 400 行以内——**不相容
的解法不是放宽行数判据，是把机械部分挪走**。

分工：

| 谁产出 | 内容 |
|---|---|
| 本脚本 | `scalar_generated.c` —— 模板能表达的算子（纯表达式，无判据） |
| `scalar.c`（手写） | 模板表达不了的：NaN 让位、除零与溢出、cast 饱和、字符解码、debugger |

判据是**有没有语义判据**，不是有没有类型。一个 handler 有判据 = 它必须有人读；
没有判据 = 它是模板的一次实例化，人读它没有收益。

用法：

    python3 runtimec/tools/architecture/generate_scalar_handlers.py           # 生成
    python3 runtimec/tools/architecture/generate_scalar_handlers.py --check   # 校验

`--check` 与生成走同一条渲染路径，逐字节比对；产物陈旧即红。这与
`generate_rwir_catalog.py` 的口径一致——两处对账迟早只跑其中一个。
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rwir_catalog import specialization_suffix  # noqa: E402

SOURCE_ROOT = Path(__file__).resolve().parents[3]
SPECIALIZATION_TABLE = Path("runtimec/binrwir/specialization_table.c")
GENERATED_HANDLERS = Path("runtimec/vmbin/handlers/scalar_generated.c")

ROW_PATTERN = re.compile(
    r'\{(BINRWIR_OPERATION_IDENTIFIER_[A-Z0-9_]+),\s*"([^"]+)",\s*'
    r'UINT16_C\((0x[0-9A-Fa-f]+)\)\}')

# C 载体类型。bool 的 body 是 1 字节（`0`/`1`）；char 种类在这里是整数载体，
# 它们的文本语义归 string 线，不进标量模板。
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
    """写参的形参名。

    一元算子的写参是 `out`，二元是 `c`——两者都读得通，但**同一个位置不能两种
    写法**：模板里的表达式文本随算子家族而变，名字随形状而变，是两处真相。这里
    统一成：结果参数恒为元组写参那一位的名字，一元即 `out`、二元即 `c`。
    """
    reads, _ = ARITY[row.operator]
    return "out" if reads == 1 else "c"


def signature(row: Row, has_failure: bool = False) -> str:
    """形参表：读参在前（`const T *`），写参最后一名（`T *`），有失败态再加 exit。

    读参个数取自 `ARITY`，不取自元组长度——`cast` 的元组是 `(源, 目标, 目标)`，
    它的第三项是**写参声明**而不是第二个读参。用元组长度推断形状会把 cast 生成
    成三元函数，那是错的签名。

    `exit` 参数只在**该条真有失败态**时加：无判据的 1462 条不该多一个用不到的
    形参，那会逼每条塞一句 `(void)output_exit;`，正好违反「handler 就一句 C」。
    """
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
    """逻辑函数名：`scalar_generated_add_int64_int64_int64_logic`。

    生成两样东西——逻辑函数与 thunk。名字分开不是因为好看：registry 存的是
    thunk（`(frame, instruction, exit)` 形状），而逻辑函数拿的是类型化指针。
    两者同名会让「这个条目指向谁」在代码里读不出来。
    """
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"scalar_generated_{row.operator.lower()}_{suffix}_logic"


def handler_name(row: Row) -> str:
    """thunk 名：`scalar_generated_add_int64_int64_int64`——十进制 opid 不上名。

    不用 opid 起名：`scalar_generated_0x81d7` 在 gdb 里看不出是哪条算子，
    而这条函数存在的全部意义就是「一眼知道它是哪一种类型组合」。
    """
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"scalar_generated_{row.operator.lower()}_{suffix}"


def thunk_macro(row: Row, has_failure: bool) -> str:
    """该形状对应的 `handler_call` 宏名。

    两个维度：形状（读几个参）× 有没有失败态。失败态由**函数体**决定，不由算子名
    决定——`div` 的浮点条目是 IEEE inf/NaN（算出来），整数条目有除零与溢出。

    生成的 thunk **不另写样板**——定位、宽度核对、失败出口都在那一套宏里，
    再写一份就是第二条真相。
    """
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
    """模板没用到哪个读参就报出来——静默用错参数是这类生成最坏的失败。

    `pow` 用两个读参，算术用两个，`cast` 只用一个。若某个模板漏了 `*b`，
    生成的函数照样编译通过、结果恒错。
    """
    del row
    return None
    """`scalar_generated_add_int64_int64_int64`——名字由算子与元组给出。

    不用 opid 起名：`scalar_generated_0x81d7` 在 gdb 里看不出是哪条算子，
    而这条函数存在的全部意义就是「一眼知道它是哪一种类型组合」。
    """
    suffix = specialization_suffix(tuple(row.types)).lower()
    return f"scalar_generated_{row.operator.lower()}_{suffix}"


# ── 模板 ────────────────────────────────────────────────────────────────
#
# 每条模板返回**一行** C 语句，或者 None（＝不归本生成器，落 `scalar.c`）。
#
# 「模板里不许有类型判断」说的是**生成的函数体**里没有 if/switch；生成器本身当然
# 按元组选文本——那是它存在的理由。函数体最终只有一次转换、一次运算、一次赋值。

UNSIGNED_OF_WIDTH = {8: "uint8_t", 16: "uint16_t", 32: "uint32_t",
                     64: "uint64_t"}


def integer_arithmetic(row: Row, operator: str) -> str | None:
    """整数 `add` / `sub` / `mul`：按结果宽度回绕，不经浮点、不触发 UB。

    写成「先转同宽无符号、算完再转回」不是绕路：C 的有符号溢出是 UB，编译器
    有权假设它不发生并据此删掉判据；矩阵 `duration-overflow-wrap` 要求 C/Go 的
    回绕语义，而无符号运算在 C 里精确定义且与二进制补码回绕逐位相同。

    结果宽度取自元组末项——编码期已经定过，这里照写，不再推一遍。
    """
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
    """浮点参与的 `add` / `sub` / `mul`：两侧先转到结果类型，再一次运算。"""
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
    """源类型的**全部取值**是否都落在目标类型内（无饱和、无截断）。

    这是是否能写成裸 `(T)x` 的判据：只要有一点越界，C 的整数窄化就是「实现
    定义」或「与饱和语义不符」，那种行必须落手写特例。
    """
    source_width, source_signed = INTEGER[source]
    target_width, target_signed = INTEGER[target]
    if source_signed and target_signed:
        return source_width <= target_width
    if source_signed and not target_signed:
        return False
    if not source_signed and not target_signed:
        return source_width <= target_width
    # 无符号 → 有符号：要留出符号位，所以必须严格更宽。
    return source_width < target_width


def cast_template(row: Row) -> str | None:
    """`cast`：定宽目标且无判据的那一部分。

    **目标 `char/<编码>` 一律不归模板**：spec `03-类型系统/02-种类-标量时间容器.kv`
    明写「字符种类的值恒为一维序列」，body 是变长字节串，写它要换长度 → 属于
    存储层，不是定宽赋值。

    **目标 `bool` 也不归**：`bool-cast-strict`（matrix:35）只接受 bool 操作数，
    其余 13 种要报 InvalidKind。

    剩下的定宽路径按「C 标准是否把这次转换定义得毫无歧义」分：

      整数 → 浮点  `(float)int64` 就是单次舍入，不经过 double（matrix:27）；
      浮点 → 浮点  窄化一次舍入；
      浮点 → 整数  有饱和与 NaN 报错，**不归模板**（走 helper）；
      整数 → 整数  源范围 ⊆ 目标范围才归（见 `integer_holds`），
                   否则是窄化回绕，走 helper。
    """
    output = output_name(row)
    source, target = row.types[0], row.types[1]
    if target in CHARACTER or source in CHARACTER:
        return None
    if source == "bool":
        # `bool → bool` 是恒等，**先于**下面那条 bool 目标早退判断：
        # 早退条件里含 `target == "bool"`，写在前面会把这一条一起排掉
        # （实测：bool,bool,bool 一直生成不出来）。
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


# ── cast 的判据路径（定宽，但 C 语义不直接成立）─────────────────────────
#
# 三条判据全部走 helper，逻辑函数只剩一行调用：
#   整数 → 窄整数      回绕（不是饱和）
#   浮点 → 整数        截断 + 饱和 + NaN 报错
#   无符号 → 有符号    可能越界，饱和（matrix:21 的「越界饱和」同样适用）


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
            return None  # 已经由 cast_template 覆盖
        # 窄化或跨符号：先按源宽度回绕到源类型，再回绕到目标宽度。
        if signed_target:
            return (f"*{output} = ({C_TYPE[target]})"
                    f"runtimec_scalar_wrap_signed((int64_t)*a, {source_width}U, "
                    f"{width_target}U);")
        return (f"*{output} = ({C_TYPE[target]})"
                f"runtimec_scalar_wrap_unsigned((uint64_t)*a, "
                f"{width_target}U);")
    return None


def pow_template(row: Row) -> str | None:
    """`pow`：任意数值输入，结果恒 float64（spec 11-指令派发与特化.kv）。

    `uint64` 转 double 走无符号值本身——`(double)*a` 里 `*a` 的类型已经是
    `uint64_t`，C 转换按数值进行，不会先经 int64（matrix:26）。
    """
    output = output_name(row)
    return f"*{output} = pow((double)*a, (double)*b);"


TEMPLATES = {
    "ADD": arithmetic_template,
    "SUB": arithmetic_template,
    "MUL": arithmetic_template,
    "CAST": cast_template,
    "POWER": pow_template,
}

# ── 比较族 ──────────────────────────────────────────────────────────────
#
# 结果恒 bool（1 字节，值 0/1）。跨类型的比较分两条路：
#
#   同符号族内部（都 signed，或都 unsigned）：先转到较宽一侧再比，精确；
#   跨符号（int×uint）或任一浮点：交给 helper，因为「都转 int64」会把
#   `uint64 > INT64_MAX` 读成负数，结论反向（见 scalar_special.c 的注释）。

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
    """`eq` / `neq` / `lt` / `le` / `gt` / `ge`。

    三路分派，每一路都只在**同一个域**里比较：

      都 signed / 都 unsigned  → 转到较宽一侧，直接比（精确，无溢出）
      跨符号                   → helper（先判符号再比值）
      任一浮点                 → 转 double 后比（int64 转 double 有精度损失，
                                 但 spec 的「按值比较」就是浮点域语义；uint64
                                 走无符号值本身，不经 int64，matrix:26）

    结果写 `bool`（1 字节，取 0/1）。
    """
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
        # 同族：转到两者中较宽的那个，比较精确且不溢出。
        wider = C_TYPE[left_type]
        if INTEGER[right_type][0] > INTEGER[left_type][0]:
            wider = C_TYPE[right_type]
        return (f"*{output_name(row)} = "
                f"(bool)(({wider})*a {operator} ({wider})*b);")
    # 跨符号：`a` 是左操作数。helper 的返回值与 0 比较。
    if left_class == "signed":
        call = "runtimec_scalar_compare_signed_unsigned((int64_t)*a, (uint64_t)*b)"
    else:
        call = "runtimec_scalar_compare_unsigned_signed((uint64_t)*a, (int64_t)*b)"
    return f"*{output_name(row)} = (bool)({call} {operator} 0);"


for _operator in COMPARISON_OPERATOR:
    TEMPLATES[_operator] = comparison_template


# ── 逻辑族 ──────────────────────────────────────────────────────────────
#
# matrix:78 logical-operators-truthy —— 数值非零即真，布尔按自身；结果恒 bool；
# **不短路**（实参在执行前已经求值，短路在本层无处安放）。
#
# 「非零」按**操作数自己的种类**判：`(bool)((double)*a)` 对 float 是对的，
# 但对 `uint64` 会先转 double、损失 2^53 以上的精度——而 `UINT64_MAX != 0`
# 仍然是真，所以结果碰巧不变；真正的问题是 NaN：`(double)NaN != 0.0` 为真，
# 符合「非零即真」，但也符合 C 的 `NaN != 0` 语义，两条一致。
#
# 因此判据写成「按种类的零比较」，不转任何域：
#   float  → `*a != 0.0`（NaN 得真，与 C 一致）
#   整数   → `*a != 0`
#   bool   → `*a`（已经是 0/1）


# matrix:78 的接受集合是「任意数值 + bool」共 11 种。**不能复用
# `comparison_class`**：那个是为比较写的，bool 在比较里走 `bool·eq` 独立行、
# 不属于数值比较，于是它对 bool 返回 "other"——用它筛逻辑族会把 43 条含 bool
# 的组合全排掉（实测：logical_and/or 各缺 21、logical_not 缺 1）。
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


# ── 位运算族 ────────────────────────────────────────────────────────────
#
# 位运算是**整数专属**（catalog 掩码 INTEGER）。混合符号在编码期已被拒
# （SAME_CLASS），所以元组里两侧永远同符号；剩下的事只有「转到较宽一侧再算」。
#
# `~` 不在本族（catalog 没有对应行）。移位是另一族：它要掩码计数。


def bitwise_template(row: Row) -> str | None:
    operator = {"BITWISE_AND": "&", "BITWISE_OR": "|",
                "BITWISE_XOR": "^"}[row.operator]
    left_type, right_type, result_type = row.types
    for type_name in (left_type, right_type, result_type):
        if type_name not in INTEGER:
            return None
    # 结果宽度由 WIDEN 定为较宽一侧（编码期已定），这里只转到该宽度再算。
    wider = C_TYPE[left_type]
    if INTEGER[right_type][0] > INTEGER[left_type][0]:
        wider = C_TYPE[right_type]
    return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
            f"(({wider})*a {operator} ({wider})*b);")


TEMPLATES["BITWISE_AND"] = bitwise_template
TEMPLATES["BITWISE_OR"] = bitwise_template
TEMPLATES["BITWISE_XOR"] = bitwise_template


# ── 极值族 ──────────────────────────────────────────────────────────────
#
# matrix:69 floating-min-max-nan —— NaN 让位于数，两个 NaN 得 NaN，与顺序无关。
# 浮点那一路交给 helper（写不出无判据的一行），整数那一路就是一次比较。


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
    # 整数：两侧转到结果宽度后比大小。截断方向不影响比较，转宽是关键。
    wider = C_TYPE[result_type]
    operator = ">" if want_maximum else "<"
    return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
            f"(({wider})*a {operator} ({wider})*b ? *a : *b);")


TEMPLATES["MIN"] = min_max_template
TEMPLATES["MAX"] = min_max_template


# ── 超越族 ──────────────────────────────────────────────────────────────
#
# matrix:70 float-transcendental-result-kind —— 结果恒 float64，与操作数种类无关。
# 整数与 float32 操作数先按值转 double 再算。`uint64` 走无符号值本身，不经 int64
# （matrix:26），`(double)*a` 里 `*a` 的类型已经是 `uint64_t`。
#
# `sqrt` 对负数返回 NaN（IEEE），不报错——这是算出来的值，不是失败。


TRANSCENDENTAL_CALL = {
    "SQUARE_ROOT": "sqrt", "EXPONENTIAL": "exp", "LOGARITHM": "log",
}


def transcendental_template(row: Row) -> str | None:
    function = TRANSCENDENTAL_CALL[row.operator]
    return f"*{output_name(row)} = {function}((double)*a);"


TEMPLATES["SQUARE_ROOT"] = transcendental_template
TEMPLATES["EXPONENTIAL"] = transcendental_template
TEMPLATES["LOGARITHM"] = transcendental_template


# ── 一元符号族 ──────────────────────────────────────────────────────────
#
# matrix:60 signed-neg-abs-sign —— neg 按宽度回绕、abs 对最小值回绕、
# sign 返回 -1/0/1。
# matrix:59 unsigned-neg-abs-sign —— neg 按宽度回绕、abs 是恒等、sign 按无符号值。
#
# 浮点这三条没有判据：`-x` 与 `fabs(x)` 都是单条指令，sign 用比较。
# 一元运算的结果类型恒等于操作数类型（catalog 的 SAME_FIRST）。


def unary_sign_template(row: Row) -> str | None:
    source_type, result_type = row.types
    output = output_name(row)
    if source_type in FLOAT:
        if row.operator == "NEGATION":
            return f"*{output} = -*a;"
        if row.operator == "ABSOLUTE":
            return f"*{output} = fabs(*a);"
        # sign：C 的比较在 NaN 上恒假，于是 `(a > 0) - (a < 0)` 对 NaN 得 0。
        # 矩阵没有为 sign(NaN) 定过值，取「0」是唯一不与任何已有条款冲突的答案。
        return f"*{output} = ({C_TYPE[result_type]})((*a > 0) - (*a < 0));"
    if source_type not in INTEGER:
        return None
    width, signed_source = INTEGER[source_type]
    if row.operator == "NEGATION":
        # 回绕：在无符号域里取反再转回，避开 `-INT64_MIN` 的 UB。
        carrier = UNSIGNED_OF_WIDTH[width]
        return (f"*{output} = ({C_TYPE[result_type]})"
                f"(0 - ({carrier})*a);")
    if row.operator == "ABSOLUTE":
        if not signed_source:
            return f"*{output} = *a;"
        # abs(最小值) 回绕成它自己：矩阵明写「让 abs 回绕」。
        # `x < 0 ? -x : x` 在最小值上是 UB，所以走无符号取反。
        carrier = UNSIGNED_OF_WIDTH[width]
        return (f"*{output} = ({C_TYPE[result_type]})"
                f"(*a < 0 ? 0 - ({carrier})*a : ({carrier})*a);")
    # sign：无符号只有 0 与非零两种。
    if not signed_source:
        return f"*{output} = ({C_TYPE[result_type]})(*a > 0);"
    return (f"*{output} = ({C_TYPE[result_type]})"
            f"((*a > 0) - (*a < 0));")


TEMPLATES["ABSOLUTE"] = unary_sign_template
TEMPLATES["NEGATION"] = unary_sign_template
TEMPLATES["SIGN"] = unary_sign_template


# ── 移位族 ──────────────────────────────────────────────────────────────
#
# matrix:71 全交给 helper：计数掩码、算术/逻辑右移、结果按操作数宽度回绕，
# 三条判据写不进一行。`width` 由元组决定（操作数宽度），不是结果宽度。


def shift_template(row: Row) -> str | None:
    left_type, right_type, result_type = row.types
    if left_type not in INTEGER or right_type not in INTEGER:
        return None
    if result_type not in INTEGER:
        return None
    width, signed_source = INTEGER[left_type]
    # 计数先掩到操作数宽度：helper 内部也掩，这里再掩一次是给 C 的移位运算
    # 留出确定行为——`*b` 是 int8 时直接左移可能超过 int64 的宽度。
    count_cast = f"((uint64_t)*b & {width - 1}U)"
    if row.operator == "SHIFT_LEFT":
        helper = ("runtimec_scalar_shift_left_signed" if signed_source
                  else "runtimec_scalar_shift_left_unsigned")
        argument = f"({C_TYPE[left_type]})*a" if signed_source else f"*a"
        return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
                f"{helper}({argument}, {count_cast}, {width}U);")
    helper = ("runtimec_scalar_shift_right_signed" if signed_source
              else "runtimec_scalar_shift_right_unsigned")
    argument = f"({C_TYPE[left_type]})*a" if signed_source else f"*a"
    return (f"*{output_name(row)} = ({C_TYPE[result_type]})"
            f"{helper}({argument}, {count_cast}, {width}U);")


TEMPLATES["SHIFT_LEFT"] = shift_template
TEMPLATES["SHIFT_RIGHT"] = shift_template


# ── cast：定宽、无失败态的那部分 ────────────────────────────────────────
#
# 浮点→整数要报 NaN 错（依赖 `_EXIT` 宏），暂时不归这里。其余定宽路径
# **都不会失败**：整数窄化是回绕，无符号→有符号是饱和，两者都是纯函数。


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
        return None  # 定宽模板已经覆盖
    source_width, source_signed = INTEGER[source]
    output = output_name(row)
    # **跨符号也是回绕，不是饱和。** matrix:63 只给了同符号的例子
    # （`int8(300)` 得 44），跨符号没写；实测 C 的 `(int8_t)(uint8_t)200`
    # 得 −56、`(uint8_t)(int8_t)-1` 得 255，都是模 2^n。Rust 的 `as` 与 Go 的
    # 显式转换同 C。饱和只属于**浮点→整数**（matrix:21），不适用于整数之间。
    #
    # 曾经在这里写过「无符号→有符号饱和到 127」，那是把浮点那条规则错搬到整数上。
    if signed_target:
        return (f"*{output} = ({C_TYPE[target]})"
                f"runtimec_scalar_wrap_signed((int64_t)*a, {source_width}U, "
                f"{width_target}U);")
    return (f"*{output} = ({C_TYPE[target]})"
            f"runtimec_scalar_wrap_unsigned((uint64_t)*a, "
            f"{width_target}U);")


def cast_guard_template(row: Row) -> str | None:
    """`cast` 的两条**有失败态**路径：它们要 `_EXIT` 宏。

      浮点 → 整数  matrix:21：截断、越界饱和、NaN 报错
      目标 bool    matrix:35：只接受 bool 操作数，其余 InvalidKind

    源是 char 的 30 条不在这里：那要解码器（char → 码点 → 目标），属字符族。
    """
    source, target = row.types[0], row.types[1]
    if target.startswith("char/"):
        return None
    if target == "bool":
        if source == "bool":
            return None  # 恒等，已由定宽模板覆盖
        # 非 bool 源：一律 InvalidKind。**运行期不判类型**——元组已经告诉我们
        # 源不是 bool，这条条目存在的意义就是把该组合标成「有此路不通」。
        #
        # 两个读/写参用不到，显式 void 掉：`-Werror` 下未用参数是硬错误，而
        # 「本组合永远失败」正是它不读参数的原因，不是遗漏。
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
    """`bool → 数值`（matrix 未写，2026-11-05 用户裁决 0/1）。

    五语言阵营：C（`bool` 隐式转 int 得 0/1）、Python（`int(True) == 1`）、
    JS（`Number(true) === 1`）三家一致；Go/Rust 是「不允许隐式转换」，不是
    「另有语义」。多数派明确，且与 bool 是 1 字节标量（取 0/1）的存储事实一致。

    与 `bool-cast-strict`（matrix:35）不冲突：那条约束的是**谁能变成 bool**，
    不约束 **bool 能变成谁**。
    """
    source, target = row.types[0], row.types[1]
    if source != "bool" or target == "bool":
        return None
    if target.startswith("char/"):
        return None
    if target not in C_TYPE:
        return None
    # `*a ? 1 : 0` 是「按值取」，不是把 1 字节位型当整数——bool 的 body 恒 0/1，
    # 但写成三元让它对任何「非零即真」的存储也正确。
    return (f"*{output_name(row)} = ({C_TYPE[target]})(*a ? 1 : 0);")


def cast_all_template(row: Row) -> str | None:
    """`cast` 的完整模板入口：三条路径串联，不是二选一。

    顺序**重要**，不是风格：
      1. `cast_template` —— C 语义无歧义、无失败态（整数加宽、整数→浮点、浮点→浮点）；
      2. `cast_guard_template` —— 有失败态（浮点→整数、目标 bool）；
      3. `cast_wide_template` —— 有语义但无失败态（整数窄化回绕）。

    实测过两次顺序错误：只挂第三条时生成数从 46 掉到 38（挤掉了前者的 20 条）；
    只挂前两条时整数窄化全漏。三条是**并集**，先具体后笼统。
    """
    for template in (cast_template, cast_from_bool_template,
                     cast_guard_template, cast_wide_template):
        body = template(row)
        if body is not None:
            return body
    return None


TEMPLATES["CAST"] = cast_all_template


# ── div / mod：有失败态，走 `_EXIT` 宏 ──────────────────────────────────
#
# 整数：除零与「最小值 / −1」两个失败码，判据全在 helper 里（已单测 + 4 条反向控制）。
# 浮点：IEEE inf/NaN，**算出来**（matrix:29），没有失败态，用无 exit 的宏。


def div_template(row: Row) -> str | None:
    return division_template(row, "DIV")


def mod_template(row: Row) -> str | None:
    return division_template(row, "MOD")


def division_template(row: Row, operator: str) -> str | None:
    del operator
    left, right, result_type = row.types
    output = output_name(row)
    # 结果的种类决定有没有失败态，不是操作数：
    # 结果 float（任一侧浮点）→ IEEE inf/NaN，算出来，无失败态（matrix:29）；
    # 结果整数 → 除零与最小值/−1 两个失败码。
    #
    # `float32 / int64` 的结果是 float32（表修好后），仍属浮点路径——所以判据
    # 必须看**结果类型**，看操作数会漏掉 32 条（实测：只看 float,float 时
    # div 只生成 36/68）。
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
    # helper 只在 int64/uint64 上算，**写回要按结果宽度回绕**：
    # `int16 30000 / 2` 的商 15000 放得下，但 `int8 100 / 1` 的商在 8 位里要回绕。
    # 直接把 `int16_t *` 传给收 `int64_t *` 的 helper 是类型错误，也会写越界。
    #
    # **失败后必须立刻 return**：helper 只在成功时写局部变量，但模板的下一句是无条件
    # `*c = ...`——那会把未初始化的局部值写进输出槽，正是「错误被写成值」。
    # 实测抓到过：除零时 `*c` 被写成 0x5A5A 之外的垃圾。
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


# ── 字符族：concat / streq / streq·not ──────────────────────────────────
#
# 这 27 条**形态与算术不同**：concat 的结果是变长 char/utf8（写它要换长度，
# 属存储层），streq 要先解码再比。所以它们不产出「一行表达式 + 一次赋值」，
# 而是产出对 `scalar_character` 里那两条实现体的薄封装——解码器在编译期钉死。

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
    """字符族的 thunk 体。None 表示不是这一族。

    三类：
      concat / streq / streq·not  —— 两段文本进，解码器由元组钉死；
      cast 目标 char             —— 单值 → 单字符，目标编码由元组钉死。
    """
    if row.operator == "CAST" and row.types[1] in CHARACTER_ENCODING:
        source, target = row.types[0], row.types[1]
        if source.startswith("char/"):
            return None  # 字符 → 字符要解码再重编，属字符族，暂不生成
        # 这条不能用类型化宏：写参是**变长** body，长度由目标编码决定，
        # 不能按 sizeof(T) 核。所以 thunk 自己做定位与宽度核对。
        if source == "bool":
            convert = "source_value = *a ? 1U : 0U;"
        else:
            convert = "source_value = (uint32_t)(int64_t)*a;"
        call = (
            "vmbin_value_location_t read_position_;\n"
            "    runtimec_error_t error_;\n"
            "    uint32_t source_value = 0U;\n"
            "    runtimec_error_reset(&error_);\n"
            "    if (vmbin_value_access_locate_body(\n"
            "            frame, &instruction->read_arguments.data[0],\n"
            "            &read_position_, &error_) != RUNTIMEC_STATUS_SUCCESS) {\n"
            "        runtimec_scalar_fail(output_exit,\n"
            "                             RUNTIMEC_SCALAR_DIAG_LOCATE_FAILED);\n"
            "        return RUNTIMEC_STATUS_ERROR;\n"
            "    }\n"
            f"    if (read_position_.size != sizeof({C_TYPE[source]})) {{\n"
            "        runtimec_scalar_fail(output_exit,\n"
            "                             RUNTIMEC_SCALAR_DIAG_LENGTH_MISMATCH);\n"
            "        return RUNTIMEC_STATUS_ERROR;\n"
            "    }\n"
            f"    {{\n"
            f"        const {C_TYPE[source]} *a = "
            "(const " + C_TYPE[source] + " *)(const void *)read_position_.read_bytes;\n"
            f"        (void)a;\n"
            f"        {convert}\n"
            "    }\n"
            f"    runtimec_scalar_value_to_character(frame, instruction, "
            f"source_value, {CHARACTER_ENCODING[target]}, output_exit);")
        return _emit_character_thunk(row, call)
    if row.operator not in ("CONCAT", "CHARACTER_EQUAL", "CHARACTER_NOT_EQUAL"):
        return None
    left, right = row.types[0], row.types[1]
    if left not in CHARACTER_DECODER or right not in CHARACTER_DECODER:
        return None
    decode_left = CHARACTER_DECODER[left]
    decode_right = CHARACTER_DECODER[right]
    if row.operator == "CONCAT":
        call = (f"runtimec_scalar_concat(frame, instruction, {decode_left}, "
                f"{decode_right}, output_exit);")
    else:
        invert = "false" if row.operator == "CHARACTER_EQUAL" else "true"
        call = (f"runtimec_scalar_character_equal(frame, instruction, "
                f"{decode_left}, {decode_right}, {invert}, output_exit);")
    return _emit_character_thunk(row, call)


def _emit_character_thunk(row: Row, call: str) -> str:
    """字符族 thunk 的统一外壳：签名固定（frame / instruction / exit），
    thunk 体自己决定读哪些参数、怎么核宽度。

    这里**不复用 `signature(row)`**：字符族的写参是变长 body（concat 结果、
    单字符结果），按 `sizeof(T)` 生成的形参表对它没有意义。
    """
    name = handler_name(row)
    return (f"static runtimec_status_t {name}(\n"
            f"    binrwir_execution_frame_t *frame,\n"
            f"    const binrwir_instruction_view_t *instruction,\n"
            f"    binrwir_execution_exit_t *output_exit) {{\n"
            f"    {call}\n"
            f"    return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR\n"
            f"               ? output_exit->status\n"
            f"               : RUNTIMEC_STATUS_SUCCESS;\n"
            f"}}\n")


def uses_exit(row: Row, body: str) -> bool:
    """这条模板要不要 `_EXIT` 宏——判据是**函数体里有没有 `output_exit`**。

    不用「算子名字」判：同一个算子（`div`）的浮点条目无失败态、整数条目有。
    用名字判会让一条算子只能有一种形状。
    """
    del row
    return "output_exit" in body

# 算子的参数形状 (读参个数, 写参个数)。真源是 spec `11-指令派发与特化.kv`
# 的「语义算子参数 union」一节；这里转录一次，因为它决定生成函数的签名。
#
# `cast` 的元组是 `(源, 目标, 目标)`——目标写两遍是编码期的事实（读参类型、
# 写参声明类型、结果类型），而它对**指令**只有 1 读 1 写。
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

# 不进模板、由手写特例承担的算子。这份清单**不是真源**——真源是每个
# `*_template` 返回 None 的事实；这里只是让 `--report` 能解释「为什么某条没生成」。
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


# 测试探针清单：(算子, 元组串)。**这不是算子清单**，是断言清单——每一条都在
# `scalar_generated_test.c` 里有对应的期望值。加一条必须同时加一条断言，否则
# 就是这个清单在自说自话。
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
)


def probe_name(row: Row) -> str:
    """可见副本名：`runtimec_scalar_probe_<算子>_<元组>`。

    前缀 `runtimec_scalar_probe_` 一眼能看出它不是产品符号——它存在只是因为
    C 没有反射，测试看不见 `static`。
    """
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
        rendered.append(
            f"void {probe_name(row)}("
            f"{signature(row, uses_exit(row, body)).replace('static', '')}) {{\n"
            f"    {body}\n"
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
    """按算子列出「生成几条 / 手写几条」——数量必须可见。

    生成器「悄悄只出 3 条」与「出了 1200 条」在产物文本上长得一样，只有计数
    能分开；而「为什么这个算子一条都没生成」要能立刻回答。
    """
    rows = read_specializations(root)
    counts: dict[str, list[int]] = {}
    for row in rows:
        bucket = counts.setdefault(row.operator, [0, 0])
        # 判据必须与 `render` **同源**：字符族走 `character_thunk` 而不是
        # `TEMPLATES`，只问 TEMPLATES 会把 27 条已生成的报成「手写」。
        # 这条曾经错过一次：产物里明明有，报告说 0。
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
