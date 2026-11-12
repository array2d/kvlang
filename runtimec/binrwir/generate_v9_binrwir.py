#!/usr/bin/env python3
"""从 rwir 能力清单生成 v9 的 binrwir 特化指令全集，按算子族拆成几个文件。

输入：`../rwir/runtimec_rwircap.kv`（rwir 层唯一能力清单）
输出：`runtimec_v9_binrwir_*.kv`（本目录），分组见 FILES

binrwir 没有 langtype：操作数按所指值的字节宽度写（uint8 / [N]uint8 / []uint8），
精度由 opcode 特化，特化靠「基础算子 + 类型元组」展开——本脚本就是那条展开规则。

类型宇宙一律取自能力清单的并集，脚本只补两件清单表达不了的事：

- 组合规则：哪些类型组合允许出现（数值同类提升、比较全积、逻辑含 bool、cast 全积）。
- 结果规则：结果类型由什么派生（同一规则、bool、float64、目标类型、char/utf8）。

能力清单里出现没有组合规则的算子直接报错——宁可红，不猜。
"""

import re
import sys
from pathlib import Path

SIGNED = ["int8", "int16", "int32", "int64"]
UNSIGNED = ["uint8", "uint16", "uint32", "uint64"]
RANK = {}
for group in (SIGNED, UNSIGNED):
    for index, token in enumerate(group, 1):
        RANK[token] = index
RANK.update({"float32": 5, "float64": 6})

ARITHMETIC = ["add", "sub", "mul", "div", "max", "min"]
INTEGER = ["mod", "bitand", "bitor", "bitxor", "shl", "shr"]
COMPARE = ["eq", "neq", "lt", "le", "gt", "ge"]
LOGICAL_BINARY = ["and", "or"]
SAME_UNARY = ["abs", "neg", "sign"]
FLOAT64_UNARY = ["sqrt", "exp", "log"]
CAST = ["cast"]

# rwir 里其余算子不按类型展开，一条声明就是一整行；操作数宽度仍按所指值写。
# 这些算子不在 binrwir 里保留同名指令，由 v9 编译成右边那条：
#   concat、copy、string·slice → C·memcpy（字节拷贝；copy 的宽度由 v9 从 langtype 算出）
#   string·cmp    → C·memcmp
#   各种 new      → kvspace·realloc（唯一写原语：key 不存在就创建）
MAPPED_AWAY = {
    "concat": "C·memcpy",
    "copy": "C·memcpy",
    "string·slice": "C·memcpy",
    "string·cmp": "C·memcmp",
    # key·abs 不在 binrwir 保留同名指令：它就是一发 kvspace·resolve
    # （帧锚点 + 相对 key → 64 位真实地址）。
    "key·abs": "kvspace·resolve",
    "compactarray·new": "kvspace·realloc",
    "struct·new": "kvspace·realloc",
    "map·new": "kvspace·realloc",
}
VERBATIM_FILES = [
    ("runtimec_v9_binrwir_control.kv", "调用、返回与分支",
     ["call", "return", "goto", "branch"], []),
    ("runtimec_v9_binrwir_string.kv", "字符串",
     ["string·set", "string·char", "string·ord", "string·find",
      "string·len", "string·formatint", "string·formatuint",
      "string·parseint", "string·parseuint", "string·parsefloat"], []),
    ("runtimec_v9_binrwir_storage.kv", "存储",
     ["kvspace·del", "kvspace·deltree", "kvspace·cp", "kvspace·cpdir", "kvspace·cplist",
      "kvspace·listlen", "kvspace·listat", "kvspace·mkindex", "kvspace·delextindex",
      "kvspace·watch"], []),
    # kvspace·resolve 是键族的底层定位：key 文本 → 值区偏移。
    ("runtimec_v9_binrwir_key.kv", "键族",
     ["key·tostr", "key·resolve"], ["kvspace·resolve"]),
    # time/duration 退化为 int64：只有取时钟不可退化。
    ("runtimec_v9_binrwir_time.kv", "时间", ["time·now"], []),
    # 布尔相等独立成组：它不参与数值提升。
    ("runtimec_v9_binrwir_bool.kv", "布尔相等", ["bool·eq", "bool·neq"], []),
]

# 容器整体不出现在 binrwir：new 走 kvspace·realloc（见 MAPPED_AWAY），
# 其余由 rwfunc 实现（kvlang 函数组合基础 rwir），没有单条 bin 指令。
RWFUNC_IMPLEMENTED = {
    "compactarray·copy", "compactarray·fill", "compactarray·numel",
    "compactarray·dim", "compactarray·bodylen", "compactarray·parselangtype",
    "compactarray·at", "compactarray·set", "compactarray·newreshape",
}

WIDTH = {
    "int8": "uint8", "uint8": "uint8", "bool": "uint8",
    "int16": "[2]uint8", "uint16": "[2]uint8",
    "int32": "[4]uint8", "uint32": "[4]uint8", "float32": "[4]uint8",
    "int64": "[8]uint8", "uint64": "[8]uint8", "float64": "[8]uint8",
    # time 与 time/duration 到 binrwir 层退化为 int64（纳秒）。
    "time": "[8]uint8", "time/duration": "[8]uint8",
    # pcs0：指向 pc 的 s0 分量的指针。s0 是 uint32；写 8 字节是因为它是地址。
    # 对 pcs0 的运算一律选 uint32 类算子（pcs0++ = add_uint32_uint32_uint32）。
    "pcs0": "[8]uint8",
    # stack：帧栈锚点，与 pcs0 同级的常用 key。相对 key 靠它补成绝对位置。
    "stack": "[8]uint8",
    "char/utf8": "[]uint8", "char/utf32": "[]uint8", "char/ascii": "[]uint8",
}

HEADER = """\
// 组件：binrwir/{stem}
// 作用：v9 的 binrwir 指令——{purpose}，一行一条。
// 生成：由 generate_v9_binrwir.py 从 ../rwir/runtimec_rwircap.kv 展开，勿手改。
// 操作数：二进制指针，按所指值的宽度写 uint8（1 字节）/ [N]uint8（N=2、4、8）/
//       []uint8（变长字节）；不写 *，binrwir 没有 langtype。
// 命名：<算子>_<读参类型>_<写参类型>，同型也写全；opid 见 runtimec_v9_binrwir_opids.yml。
//       opid 是 runtimec 私有编号，从 1 按产物顺序递增。
// 规模：{count} 条。
"""


def normalize(token):
    return token[2:] if token.startswith("[]") else token


def same_class(a, b):
    return (a in SIGNED and b in SIGNED) or (a in UNSIGNED and b in UNSIGNED)


def promote(a, b):
    if "float64" in (a, b):
        return "float64"
    if "float32" in (a, b):
        return "float32"
    return a if RANK[a] >= RANK[b] else b


def parse_catalog(path):
    declarations = {}
    # `return` 是文法关键字，声明不写括号：`rwir return -> ()`。
    pattern = re.compile(r"^rwir (\S+?)(?:\((.*)\))? -> \((.*)\)$")
    for line in path.read_text().splitlines():
        match = pattern.match(line.strip())
        if not match:
            continue
        name, read_text, write_text = match.groups()
        read_text = read_text or ""
        declarations[name] = (
            [parse_param(text) for text in read_text.split(",") if text.strip()],
            [parse_param(text) for text in write_text.split(",") if text.strip()],
        )
    return declarations


def parse_param(text):
    """`int8|int16 #a` -> ('a', {'int8', 'int16'})；`#arguments...` 保留省略号。"""
    type_text, name = text.strip().rsplit(" #", 1)
    return name, {normalize(token) for token in type_text.split("|")}


def expand(name, read_unions, write_unions):
    result = next(iter(write_unions[0])) if len(write_unions) == 1 else None
    rows = []
    if name in ARITHMETIC or name in INTEGER:
        a_types, b_types = read_unions[0], read_unions[1]
        for a in a_types:
            for b in b_types:
                if name in ARITHMETIC:
                    allowed = same_class(a, b) or "float" in a or "float" in b
                else:
                    allowed = same_class(a, b)
                if allowed:
                    rows.append((a, b, promote(a, b)))
    elif name in COMPARE or name in LOGICAL_BINARY:
        for a in read_unions[0]:
            for b in read_unions[1]:
                rows.append((a, b, result))
    elif name == "not":
        rows = [(a, result) for a in read_unions[0]]
    elif name in SAME_UNARY:
        rows = [(a, a) for a in read_unions[0]]
    elif name in FLOAT64_UNARY:
        rows = [(a, result) for a in read_unions[0]]
    elif name == "pow":
        rows = [(a, b, result) for a in read_unions[0] for b in read_unions[1]]
    elif name in CAST:
        rows = [(a, b) for a in read_unions[0] for b in write_unions[0]]
    else:
        raise SystemExit(f"算子 {name} 没有展开规则：先在 RULES 里给它分类")
    return rows


RULES = set(ARITHMETIC + INTEGER + COMPARE + LOGICAL_BINARY + SAME_UNARY
            + FLOAT64_UNARY + CAST + ["not", "pow"])

# 只在 binrwir 存在的指令：rwir 层没有同名算子，由 v9 编译生成。
# kvspace 的三条逐字对齐 `kvspace/include/kvspace/kvspace.h`（唯一公共存储 ABI）：
#   kvspaceResolveOffset(key, parent_key_anchor, *offset)
#   kvspaceRealloc(key, body_length, attr_length, *offset)  —— 唯一写原语：不存在则创建，存在则调整容量
#   kvspaceFree(key)
# 没有 malloc/load/store：创建走 realloc（新 key），读写在拿到 offset 后由上层直接访存，
# 不另设按地址读写的算子。地址/长度一律 [8]uint8（64 位二进制数）。
BIN_ONLY_NOTES = {
    "C·memcpy": "rwir 的 concat、copy、string·slice 编译到此，都是字节拷贝",
    "C·memmove": "重叠拷贝：inplace 写自身、容器内搬移元素",
    "C·memset": "填充：新槽清零、单字节重复填充",
    "C·memcmp": "rwir 的 string·cmp 编译到此",
    "kvspace·resolve": "key 文本 + stack 帧栈锚点 → 64 位真实地址（kvspace.h 的 kvspaceResolveOffset）",
    "kvspace·realloc": "唯一写原语：key 不存在就创建，存在就调整容量",
    "kvspace·free": "删除 key 的值并释放 box",
}

BIN_ONLY = {
    # C 标准：void *memcpy(void *dest, const void *src, size_t n);
    # 读写参风格：src 与字节数是读参，dest 是写参。src/dest 都是 8 字节地址。
    "C·memcpy": ([("src", "[8]uint8"), ("byte_count", "[8]uint8")],
                 [("dest", "[8]uint8")]),
    # void *memmove(void *dest, const void *src, size_t n);
    "C·memmove": ([("src", "[8]uint8"), ("byte_count", "[8]uint8")],
                  [("dest", "[8]uint8")]),
    # void *memset(void *dest, int value, size_t n);
    "C·memset": ([("value", "uint8"), ("byte_count", "[8]uint8")],
                 [("dest", "[8]uint8")]),
    # int memcmp(const void *left, const void *right, size_t n);
    "C·memcmp": ([("left", "[8]uint8"), ("right", "[8]uint8"),
                  ("byte_count", "[8]uint8")],
                 [("order", "[4]uint8")]),
    # 直接对应 kvspaceResolveOffset，但返回**真实地址**（映射基址 + 值区偏移）：
    # 相对 key 由帧栈锚点 stack 定位；缺 key 返回 UINT64_MAX 哨兵。
    "kvspace·resolve": ([("key", "[]uint8"), ("stack", "stack")],
                        [("address", "[8]uint8")]),
    "kvspace·realloc": ([("key", "[]uint8"), ("body_length", "[8]uint8"),
                         ("attr_length", "[8]uint8")],
                        [("value_offset", "[8]uint8")]),
    "kvspace·free": ([("key", "[]uint8")], []),
}

# 产物分组：每个算子恰好落在一个文件里，漏放或重复放都报错。
FILES = [
    ("runtimec_v9_binrwir_arithmetic.kv", "加、减、乘、除、最大、最小", ARITHMETIC, []),
    ("runtimec_v9_binrwir_unary.kv", "绝对值、取反、符号、平方根、指数、对数、幂",
     SAME_UNARY + FLOAT64_UNARY + ["pow"], []),
    ("runtimec_v9_binrwir_integer.kv", "整除、位运算与移位", INTEGER, []),
    ("runtimec_v9_binrwir_equality.kv", "相等比较", ["eq", "neq"], []),
    ("runtimec_v9_binrwir_order.kv", "大小比较", ["lt", "le", "gt", "ge"], []),
    ("runtimec_v9_binrwir_logic.kv", "逻辑与、或、非", LOGICAL_BINARY + ["not"], []),
    ("runtimec_v9_binrwir_cast.kv", "类型转换", CAST, []),
    ("runtimec_v9_binrwir_memory.kv", "字节拷贝与 key→偏移、创建/调整、释放",
     [], ["C·memcpy", "C·memmove", "C·memset", "C·memcmp",
          "kvspace·realloc", "kvspace·free"]),
]


def instruction_name(op, row):
    return "_".join([op] + [token.replace("/", "_") for token in row])


def render(op, row, read_names, write_names):
    reads, writes = row[:-1], row[-1:]
    params = ", ".join(f"{WIDTH[t]} #{name}" for t, name in zip(reads, read_names))
    results = ", ".join(f"{WIDTH[t]} #{name}" for t, name in zip(writes, write_names))
    return f"rwir {instruction_name(op, row)}({params}) -> ({results})"


def render_direct(name, read_params, write_params):
    params = ", ".join(f"{t} #{param}" for param, t in read_params)
    results = ", ".join(f"{t} #{param}" for param, t in write_params)
    return f"rwir {name}({params}) -> ({results})"


def operand_width(union):
    if len(union) == 1:
        token = next(iter(union))
        if token in WIDTH:
            return WIDTH[token]
    return "[]uint8"


def render_verbatim(name, read_params, write_params):
    params = ", ".join(f"{operand_width(union)} #{param}" for param, union in read_params)
    results = ", ".join(f"{operand_width(union)} #{param}" for param, union in write_params)
    return f"rwir {name}({params}) -> ({results})"


def render_operand(operand):
    return f'{{name: {operand["name"]}, type: "{operand["type"]}"}}'


def render_opid_yaml(records):
    lines = [
        "# 组件：binrwir/runtimec_v9_binrwir_opids",
        "# 作用：v9 的 opid 表——runtimec 私有编号，从 1 按产物顺序递增；含参数名与参数类型。",
        "# 生成：由 generate_v9_binrwir.py 生成，勿手改。",
        f"count: {len(records)}",
        "instructions:",
    ]
    for record in records:
        parts = [f"opid: {record['opid']}", f"name: {record['name']}"]
        parts.append("read: [" + ", ".join(render_operand(o) for o in record["read"]) + "]")
        parts.append("write: [" + ", ".join(render_operand(o) for o in record["write"]) + "]")
        parts.append(f"file: {record['file']}")
        lines.append("  - {" + ", ".join(parts) + "}")
    return "\n".join(lines) + "\n"


def main():
    here = Path(__file__).resolve().parent
    stale = here / "runtimec_v9_binrwir.kv"
    if stale.exists():
        raise SystemExit(f"{stale} 是拆分前的合并文件，先删掉它")
    catalog = parse_catalog(here.parent / "rwir" / "runtimec_rwircap.kv")

    verbatim_ops = {op: filename for filename, _, ops, _ in VERBATIM_FILES for op in ops}

    for name in RULES:
        if name not in catalog:
            raise SystemExit(f"展开规则里的 {name} 不在 rwir 能力清单中")
    for name, filename in verbatim_ops.items():
        if name not in catalog:
            raise SystemExit(f"{filename} 里的 {name} 不在 rwir 能力清单中")
    for name, target in MAPPED_AWAY.items():
        if name not in catalog:
            raise SystemExit(f"映射掉的 {name} 不在 rwir 能力清单中")
        if target not in BIN_ONLY:
            raise SystemExit(f"{name} 映射到 {target}，但 {target} 不在 BIN_ONLY 中")
    for name in RWFUNC_IMPLEMENTED:
        if name not in catalog:
            raise SystemExit(f"rwfunc 实现的 {name} 不在 rwir 能力清单中")
    for name in catalog:
        if name in RULES or name in verbatim_ops or name in MAPPED_AWAY \
                or name in RWFUNC_IMPLEMENTED:
            continue
        raise SystemExit(f"能力清单里的 {name} 既没有展开规则，也不在逐条声明清单中")

    rows_by_op = {}
    for op in sorted(RULES):
        read_params, write_params = catalog[op]
        read_unions = [union for _, union in read_params]
        write_unions = [union for _, union in write_params]
        op_rows = []
        for row in expand(op, read_unions, write_unions):
            if row[-1] not in write_unions[0]:
                raise SystemExit(f"{op}: 结果类型 {row[-1]} 不在能力清单的写参并集里")
            if len(row[:-1]) != len(read_params) or len(row[-1:]) != len(write_params):
                raise SystemExit(f"{op}: 展开出的元组长度与能力清单参数个数不符")
            op_rows.append(row)
        rows_by_op[op] = sorted(op_rows, key=lambda row: ",".join(row))

    assigned = {}
    for filename, _, ops, bin_only in FILES + VERBATIM_FILES:
        for op in ops:
            if op in assigned:
                raise SystemExit(f"{op} 同时落在 {assigned[op]} 与 {filename}")
            if op not in RULES and op not in verbatim_ops:
                raise SystemExit(f"{filename} 里的 {op} 既不是展开算子也不是逐条算子")
            assigned[op] = filename
        for name in bin_only:
            if name in assigned:
                raise SystemExit(f"{name} 同时落在 {assigned[name]} 与 {filename}")
            if name not in BIN_ONLY:
                raise SystemExit(f"{filename} 里的 {name} 不在 BIN_ONLY 中")
            if name not in BIN_ONLY_NOTES:
                raise SystemExit(f"{name} 缺少 BIN_ONLY_NOTES 说明")
            assigned[name] = filename
    for op in sorted(RULES):
        if op not in assigned:
            raise SystemExit(f"算子 {op} 没有落在任何产物文件里")

    def write_bin_only(lines, bin_only):
        if not bin_only:
            return
        lines.append("")
        lines.append("// 只在 binrwir 存在的指令：")
        for name in bin_only:
            lines.append(f"//   {name} —— {BIN_ONLY_NOTES[name]}")
        for name in bin_only:
            lines.append(render_direct(name, *BIN_ONLY[name]))

    records = []

    def bin_only_record(name, filename):
        read_params, write_params = BIN_ONLY[name]
        return {
            "name": name,
            "read": [{"name": param, "type": type_text} for param, type_text in read_params],
            "write": [{"name": param, "type": type_text} for param, type_text in write_params],
            "file": filename,
        }

    for filename, purpose, ops, bin_only in FILES:
        entries = [(op, row) for op in ops for row in rows_by_op[op]]
        lines = [HEADER.format(stem=filename[:-3], purpose=purpose,
                               count=len(entries) + len(bin_only)).rstrip("\n")]
        previous = None
        for op, row in entries:
            if previous is not None and op != previous:
                lines.append("")
            previous = op
            read_params, write_params = catalog[op]
            lines.append(render(op, row,
                                [name for name, _ in read_params],
                                [name for name, _ in write_params]))
            records.append({
                "name": instruction_name(op, row),
                "read": [{"name": name, "type": WIDTH[token]}
                         for (name, _), token in zip(read_params, row[:-1])],
                "write": [{"name": name, "type": WIDTH[token]}
                          for (name, _), token in zip(write_params, row[-1:])],
                "file": filename,
            })
        write_bin_only(lines, bin_only)
        for name in bin_only:
            records.append(bin_only_record(name, filename))
        target = here / filename
        target.write_text("\n".join(lines) + "\n")
        print(f"{filename}: {len(entries) + len(bin_only)} 条")

    for filename, purpose, ops, bin_only in VERBATIM_FILES:
        lines = [HEADER.format(stem=filename[:-3], purpose=purpose,
                               count=len(ops) + len(bin_only)).rstrip("\n")]
        for op in ops:
            read_params, write_params = catalog[op]
            lines.append(render_verbatim(op, read_params, write_params))
            records.append({
                "name": op,
                "read": [{"name": name, "type": operand_width(union)}
                         for name, union in read_params],
                "write": [{"name": name, "type": operand_width(union)}
                          for name, union in write_params],
                "file": filename,
            })
        write_bin_only(lines, bin_only)
        for name in bin_only:
            records.append(bin_only_record(name, filename))
        target = here / filename
        target.write_text("\n".join(lines) + "\n")
        print(f"{filename}: {len(ops) + len(bin_only)} 条")

    names = [record["name"] for record in records]
    if len(names) != len(set(names)):
        duplicated = sorted({name for name in names if names.count(name) > 1})
        raise SystemExit(f"指令名重复：{duplicated}")

    # opid 是 runtimec 私有编号：从 1 起按产物顺序递增，不跨仓库共享。
    records = [{"opid": ordinal, **record} for ordinal, record in enumerate(records, 1)]
    id_target = here / "runtimec_v9_binrwir_opids.yml"
    id_target.write_text(render_opid_yaml(records))
    print(f"{id_target.name}: {len(records)} 条，opid 1..{len(records)}")

    # opid → 组 handler 的分派表：下标就是 opid，0 号不用。
    dispatch = [
        "/*",
        " * 组件：binrwir/dispatch_table（生成物，勿手改）",
        " * 作用：opid → 组 handler。下标是 opid，0 号不用。",
        " * 生成：由 generate_v9_binrwir.py 从产物分组生成。",
        " */",
        "",
        '#include "binrwir/handler.h"',
        "",
        "const binrwir_handler_t binrwir_handler_table[] = {",
    ]
    for record in records:
        group = record["file"][len("runtimec_v9_binrwir_"):-len(".kv")]
        dispatch.append(
            f"    [{record['opid']}] = runtimec_binrwir_{group}_execute,")
    dispatch.append("};")
    dispatch.append("")
    dispatch.append(f"const uint32_t binrwir_handler_count = {len(records) + 1}U;")
    (here / "dispatch_table.c").write_text("\n".join(dispatch) + "\n")
    print(f"dispatch_table.c: {len(records)} 条，binrwir_handler_count="
          f"{len(records) + 1}")


if __name__ == "__main__":
    sys.exit(main())
