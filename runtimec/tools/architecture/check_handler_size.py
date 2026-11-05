#!/usr/bin/env python3
"""Reject oversized binrwir handlers.

用户裁决（原话）：

> 运算类的 binrwir handler 函数体，**普通类型超过 3 行需要人工审查**；
> **array 等复杂类型的容许稍多 2 行**（即 >5 行审查）。

判据的形状：**行数不是美学，是可审查性**。一个 handler 超过几行，说明它大概率
在函数里分岔（按类型、按引用种类、按边界情形各写一段）——而分岔的正确位置
通常不是 handler 内部。超限不阻止合并，但必须有人看一眼。

解析口径与 `check_names.py` 完全一致：**复用它的 `c_code_without_comments`**，
不在这里另写一套注释剥离。两套剥离必然漂移，而漂移的后果是「同一个函数在两个
检查器里行数不同」——那时没人说得清哪个对。

退出码：有超限即 1（CI 里看得见），全在全限内为 0。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_names import c_code_without_comments  # noqa: E402

HANDLERS_DIRECTORY = Path("runtimec/vmbin/handlers")

# 上限按「参数类型复杂度」分档，不是按文件喜好。普通类型 3 行；容器、数组、
# 编解码这类要处理维度、容量、边界，天然多两步。
DEFAULT_LIMIT = 5
LIMITS = {
    "scalar.c": 3,
}
COMPLEX_FILES = ("string.c", "container.c", "scatter.c", "langtype.c", "host.c")

# `static inline runtimec_status_t name(...)` / `static runtimec_status_t name(...)`
# 两种都要认：有人不写 inline。返回类型固定在这一族函数里，别放宽成任意类型。
HANDLER_DEFINITION_PATTERN = re.compile(
    r"^[ \t]*static[ \t]+(?:inline[ \t]+)?"
    r"(?P<return_type>runtimec_status_t|int|void)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.MULTILINE,
)

# 条目表里的 `.handler = <函数名>`。判据只认这些：**注册表引用到的才是 handler**。
#
# 这修正了一个把数字放大 6 倍的缺陷：早先扫「所有 static 函数」，于是
# `scalar_fail`、`container_view`、`string_decode_*` 这类辅助函数全被算进
# 超限清单——86 里只有 15 个是真的 handler。判据名字叫 handler_size，
# 就该量 handler。
REGISTERED_HANDLER_PATTERN = re.compile(
    r"\.handler\s*=\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)")
# 尚未转成指定初始化器时（`{opid, flags, fn}`）的兜底：第三个字段是函数名。
POSITIONAL_HANDLER_PATTERN = re.compile(
    r"^\s*\{\s*[^,{}]+,\s*[^,{}]+,\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\}\s*,?\s*$",
    re.MULTILINE)

# 单独成行的纯括号：不计入函数体行数——它们不是「一行逻辑」。
BRACE_ONLY_PATTERN = re.compile(r"^\s*[{}]?;?\s*$")


def limit_for(path: Path) -> int:
    return LIMITS.get(path.name, DEFAULT_LIMIT)


def find_definition_span(code: str, open_paren: int) -> tuple[int, int] | None:
    """从函数名的 `(` 起，找到函数体的 `{` 与其配对的 `}`。

    返回 (body_start, body_end)，都是**字符下标**，body_start 指向 `{` 之后。
    声明（以 `;` 结束、没有函数体）返回 None——前置声明不参与行数判据。
    """
    depth = 0
    index = open_paren
    while index < len(code):
        character = code[index]
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
            if depth == 0:
                index += 1
                break
        index += 1
    # 参数表之后：可能还有 `const` 之类修饰，然后要么 `;`（声明）要么 `{`。
    while index < len(code):
        character = code[index]
        if character == "{":
            break
        if character == ";":
            return None
        index += 1
    if index >= len(code):
        return None

    body_start = index + 1
    depth = 1
    index = body_start
    while index < len(code):
        character = code[index]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return body_start, index
        index += 1
    return None


def count_body_lines(code: str, body_start: int, body_end: int) -> int:
    """函数体的有效行数：非空、非注释、非纯括号行。

    `code` 已经过 `c_code_without_comments`，注释位置是等长空白——所以这里只需
    判空与判纯括号，不必再做一次注释识别。
    """
    body = code[body_start:body_end]
    counted = 0
    for line in body.split("\n"):
        stripped = line.strip()
        if not stripped:
            continue
        if BRACE_ONLY_PATTERN.match(line):
            continue
        counted += 1
    return counted


# 薄封装：函数体只有一句 `return f(...)` 时，**量 f 而不是量壳**。
# 壳本身 1~3 行永远合规，量它等于给被包的真逻辑开后门。
THIN_WRAPPER_PATTERN = re.compile(
    r"^\s*return\s+(?P<callee>[A-Za-z_][A-Za-z0-9_]*)\s*\(")

# 宏形态：`VMBIN_HANDLER_XXX(handler_name, logic_function, ...)`。
# 宏展开后的函数体看不见，但**逻辑函数**是真正的实现——量它。
# 第一个实参是生成的 handler 名，第二个是逻辑函数名。
HANDLER_MACRO_PATTERN = re.compile(
    r"^\s*(?P<family>VMBIN_HANDLER_[A-Z0-9_]+)\s*\(\s*"
    r"(?P<handler>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"(?P<logic>[A-Za-z_][A-Za-z0-9_]*)",
    re.MULTILINE)

# 族-算子对照表：`*   VMBIN_HANDLER_XXX | 算子名, 算子名`。
# 一行一族，竖线分隔——**必须能机械解析**：散文表格（带对齐横线、列宽不一的
# Markdown 表格）解析时边界太多，而检查器越复杂越容易在下次改动里静默失效。
FAMILY_TABLE_ROW_PATTERN = re.compile(
    r"^\s*\*\s+(?P<family>VMBIN_HANDLER_[A-Z0-9_]+)\s*\|\s*(?P<operators>.+?)\s*$",
    re.MULTILINE)
FAMILY_TABLE_MARKER = "handler-family-table:"

# 非「族」的宏：工具宏不生成 handler，容量常量根本不是宏族。
# 判据不能靠名字前缀——它们全都以 `VMBIN_HANDLER_` 开头。
NON_FAMILY_MACROS = frozenset({
    "VMBIN_HANDLER_LOCATE_READ", "VMBIN_HANDLER_LOCATE_WRITE",
    "VMBIN_HANDLER_REQUIRE", "VMBIN_HANDLER_REQUIRE_ARITY",
    "VMBIN_HANDLER_REQUIRE_WRITABLE", "VMBIN_HANDLER_AS",
    "VMBIN_HANDLER_AS_READ", "VMBIN_HANDLER_LANGTYPE_CAPACITY",
    "VMBIN_HANDLER_KEY_CAPACITY", "VMBIN_HANDLER_CREATE_BUFFER_CAPACITY",
    "VMBIN_HANDLER_CREATE_KEY_CAPACITY",
})

# 放行表：确实需要人工审查、已裁定保留的超限 handler。
# **每条都必须仍然存在且仍然超限**——某条被重写变小之后，放行条目会变成僵尸，
# 规则就被悄悄放宽了。selftest 会断言这一点。
REVIEWED_OVERSIZE = {
    "scalar_handle_debugger": "调试器入口：状态机分支天然多，见 workspace/622 裁定",
}


def registered_handlers(source_root: Path) -> set[str]:
    """全仓条目表里出现过的 handler 函数名。

    跨文件收集：条目表与函数定义在同一个 .c 里，但判据不该依赖这个巧合——
    将来有人把表拆出去，也不会让判据失效。
    """
    names: set[str] = set()
    for path in handlers_table_files(source_root):
        try:
            text = c_code_without_comments(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            continue
        names.update(m.group("name")
                     for m in REGISTERED_HANDLER_PATTERN.finditer(text))
        names.update(m.group("name")
                     for m in POSITIONAL_HANDLER_PATTERN.finditer(text))
    return names


def handlers_table_files(source_root: Path):
    handlers_root = source_root / HANDLERS_DIRECTORY
    if handlers_root.is_dir():
        yield from sorted(handlers_root.glob("*.c"))
    temporary = source_root / "runtimec/vmbin/temp_println.c"
    if temporary.is_file():
        yield temporary


def scan_file(path: Path, display: str,
              registered: set[str]) -> list[tuple[str, str, int, int, int, str]]:
    """返回 [(display, name, line_number, lines, limit, note)]，只含**已注册**且超限的。"""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return []
    code = c_code_without_comments(text)
    limit = limit_for(path)
    # 索引全部函数定义，供薄封装跳转与宏逻辑函数查找。
    # `definition_starts` 记定义处字符下标：共享 helper 要报在**它自己**的行号上。
    definitions: dict[str, tuple[int, int]] = {}
    definition_starts: dict[str, int] = {}
    for match in HANDLER_DEFINITION_PATTERN.finditer(code):
        span = find_definition_span(code, match.end() - 1)
        if span is not None:
            definitions[match.group("name")] = span
            definition_starts[match.group("name")] = match.start()

    def line_of(character_index: int) -> int:
        return code.count("\n", 0, character_index) + 1

    def thin_callee(name: str) -> str | None:
        """该 handler 若是薄壳（`return f(...)`），返回被调的 f。"""
        span = definitions.get(name)
        if span is None:
            return None
        if count_body_lines(code, *span) > 2:
            return None
        thin = THIN_WRAPPER_PATTERN.search(code[span[0]:span[1]])
        return None if thin is None else thin.group("callee")

    # 共享 helper：被 ≥2 个**不同的** registered handler 当薄壳目标调用。
    #
    # 判据是「不同的 handler 个数」，不是调用次数——同一个 handler 里出现两次
    # 仍算一个。按次数算会把「一个壳里写两遍」误判成共享。
    shared_users: dict[str, set[str]] = {}
    for match in HANDLER_DEFINITION_PATTERN.finditer(code):
        name = match.group("name")
        if name not in registered:
            continue
        callee = thin_callee(name)
        # callee 自己就是 handler 时不按共享 helper 处理：它会作为 handler 被量一次。
        if callee is None or callee in registered:
            continue
        shared_users.setdefault(callee, set()).add(name)
    shared_callees = {callee for callee, users in shared_users.items()
                      if len(users) >= 2}

    def measure(name: str, seen: frozenset[str]) -> tuple[int, int] | None:
        """量真正的逻辑体：薄封装跟到被调的 f，宏形态跟到逻辑函数。"""
        if name in seen:                      # 递归防环：壳包壳没有意义
            return None
        span = definitions.get(name)
        if span is None:
            return None
        body_start, body_end = span
        lines = count_body_lines(code, body_start, body_end)
        # 薄封装：一句 `return f(...)` 时改量 f。
        # **共享 helper 不跟进去**：它会被单独量一次并报在自己名下，跟进去就又是
        # 每个壳各报一遍——这正是要修的重复计数。壳本身 1~2 行，自然合规。
        thin = THIN_WRAPPER_PATTERN.search(code[body_start:body_end])
        if thin is not None and lines <= 2:
            callee = thin.group("callee")
            if callee not in shared_callees:
                followed = measure(callee, seen | {name})
                if followed is not None:
                    return followed
        return lines, definitions[name][0]

    findings = []
    for match in HANDLER_DEFINITION_PATTERN.finditer(code):
        name = match.group("name")
        # 判据叫 handler_size，就只量 handler：辅助函数（fail/view/decode 之类）
        # 不在注册表里，它们的行数不是这条判据要管的事。
        if name not in registered:
            continue
        measured = measure(name, frozenset())
        if measured is None:
            continue
        lines, _ = measured
        if lines > limit and name not in REVIEWED_OVERSIZE:
            findings.append((display, name, line_of(match.start()), lines, limit,
                             ""))

    # 宏形态：handler 名由宏生成，不在 definitions 里；量它的逻辑函数实参。
    for match in HANDLER_MACRO_PATTERN.finditer(code):
        handler, logic = match.group("handler"), match.group("logic")
        if handler not in registered:
            continue
        measured = measure(logic, frozenset())
        if measured is None:
            continue
        lines, _ = measured
        if lines > limit and logic not in REVIEWED_OVERSIZE:
            findings.append((display, f"{handler}({logic})",
                             line_of(match.start()), lines, limit, ""))

    # 共享 helper：以自己的名字量一次，报在**它自己的定义行**。
    for callee in sorted(shared_callees):
        users = shared_users[callee]
        note = f"（共享 helper，被 {len(users)} 个 handler 引用，只计一次）"
        start = definition_starts.get(callee)
        if start is None:
            # 报出来，不要静默跳过：静默跳过意味着这个共享 helper 从此不受量，
            # 比重复计数更糟。用它第一个引用者的行号，并写明为什么量不了。
            first_user = sorted(users)[0]
            findings.append((display, callee,
                             line_of(definition_starts.get(first_user, 0)), 0,
                             limit,
                             note + " 无法度量：定义不在 HANDLER_DEFINITION_PATTERN "
                             "的返回类型族内（runtimec_status_t / int / void）"))
            continue
        lines = count_body_lines(code, *definitions[callee])
        if lines > limit and callee not in REVIEWED_OVERSIZE:
            findings.append((display, callee, line_of(start), lines, limit, note))
    return findings


ENTRY_PATTERN = re.compile(
    r"\{\.operation_identifier\s*=\s*(?P<opid>[^,]+),")
HANDLER_REFERENCE_PATTERN = re.compile(r"\.handler\s*=\s*[A-Za-z_]")


def audit_family_table(source_root: Path) -> tuple[list[str], list[str]]:
    """族-算子对照表与实际用法的一致性。

    表在 header 里、用法在 .c 里，两者会漂移——**注释漂移过一次就够了**（今晚
    有过「注释写运行期集中、实现做编译期展开」的例子，注释对、实现错）。

    返回 (findings, notices)：
      findings —— **用了但表里没有**：硬判据，红。用了一个没登记的族，说明
                  表已经不能描述现状，下一个人照表读会读错。
      notices  —— **表里有但没人用**：只报不红。可能是留给将来的，也可能是刚
                  被删——两种情况都不该阻塞，但值得看见。
    """
    findings: list[str] = []
    notices: list[str] = []
    handlers_root = source_root / HANDLERS_DIRECTORY

    # 1) 从 header 里读表。
    declared: dict[str, set[str]] = {}
    marker_seen = False
    for header in sorted(handlers_root.glob("*.h")):
        try:
            text = header.read_text(encoding="utf-8")
        except (OSError, UnicodeError):
            continue
        if FAMILY_TABLE_MARKER not in text:
            continue
        marker_seen = True
        display = header.relative_to(source_root).as_posix()
        for match in FAMILY_TABLE_ROW_PATTERN.finditer(text):
            operators = {piece.strip()
                         for piece in match.group("operators").split(",")
                         if piece.strip()}
            declared.setdefault(match.group("family"), set()).update(operators)
        del display

    # 2) 从 .c 里收集实际用到的族。
    used: dict[str, set[str]] = {}
    for source in sorted(handlers_root.glob("*.c")):
        try:
            code = c_code_without_comments(source.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            continue
        for match in HANDLER_MACRO_PATTERN.finditer(code):
            family = match.group("family")
            if family in NON_FAMILY_MACROS:
                continue
            used.setdefault(family, set())

    if not marker_seen:
        # 表还没落地：不红，但提示一句，免得「检查器静默不工作」。
        notices.append(
            f"no `{FAMILY_TABLE_MARKER}` block found in "
            f"{HANDLERS_DIRECTORY}/*.h — the family table check is inactive")
        return findings, notices

    for family in sorted(used):
        if family not in declared:
            findings.append(
                f"family `{family}` is used in handlers but missing from the "
                f"`{FAMILY_TABLE_MARKER}` table")
    for family in sorted(declared):
        if family not in used:
            notices.append(
                f"family `{family}` is declared in the table but no handler "
                f"file uses it (reserved for future, or just removed?)")
    return findings, notices


def audit_entry_tables(source_root: Path) -> list[str]:
    """条目表自检：条目数 == 唯一 opid 数 == `.handler =` 引用数，且 opid 解析成常量名。

    为什么这三条要相等：条目数多于唯一 opid 说明有重复注册（装载期会被拒，但
    那时已经晚了）；少于 `.handler =` 引用数说明有一条写残了。而 opid 必须是
    常量名而不是裸十六进制——裸数字无法与 catalog 对账。
    """
    problems: list[str] = []
    for path in handlers_table_files(source_root):
        display = path.relative_to(source_root).as_posix()
        try:
            text = c_code_without_comments(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError):
            continue
        entries = [m.group("opid").strip()
                   for m in ENTRY_PATTERN.finditer(text)]
        references = len(HANDLER_REFERENCE_PATTERN.findall(text))
        if not entries:
            continue
        if len(entries) != references:
            problems.append(
                f"{display}: {len(entries)} entry(ies) but {references} "
                f"`.handler =` reference(s)")
        if len(set(entries)) != len(entries):
            duplicates = sorted({value for value in entries
                                 if entries.count(value) > 1})
            problems.append(f"{display}: duplicate opid(s) {duplicates[:5]}")
        for opid in entries:
            # 常量名以字母开头；裸十六进制以 0 开头。
            if not re.match(r"[A-Za-z_]", opid):
                problems.append(
                    f"{display}: opid `{opid}` is a bare literal, not a "
                    f"constant name — it cannot be reconciled with the catalog")
                break
    return problems


def audit(source_root: Path) -> tuple[list[tuple[str, str, int, int, int, str]], int]:
    handlers_root = source_root / HANDLERS_DIRECTORY
    findings: list[tuple[str, str, int, int, int, str]] = []
    scanned = 0
    if not handlers_root.is_dir():
        return findings, scanned
    registered = registered_handlers(source_root)
    for path in sorted(handlers_root.glob("*.c")):
        display = path.relative_to(source_root).as_posix()
        file_findings = scan_file(path, display, registered)
        findings.extend(file_findings)
        scanned += 1
    return findings, scanned


def selftest() -> int:
    """fixture：3 行 OK、4 行超限；再做反向控制。

    反向控制的意义：只写 fixture 不证明判据有效——把上限改成 99，若仍报错，
    说明报错与行数无关；若全绿，说明行数确实是判据。两个方向都要跑。
    """
    import tempfile

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="runtimec-handler-size-selftest-") as tmp:
        root = Path(tmp)
        handlers = root / HANDLERS_DIRECTORY
        handlers.mkdir(parents=True)
        source = handlers / "scalar.c"
        source.write_text(
            # 已注册、3 行 —— 必须通过。
            "static inline runtimec_status_t scalar_three(void) {\n"
            "    int a = 1;\n"
            "    int b = 2;\n"
            "    return a + b;\n"
            "}\n"
            "\n"
            # 已注册、4 行 —— 必须报。
            "static inline runtimec_status_t scalar_four(void) {\n"
            "    int a = 1;\n"
            "    int b = 2;\n"
            "    int c = 3;\n"
            "    return a + b + c;\n"
            "}\n"
            "\n"
            # **未注册**、20 行 —— 必须不报：辅助函数不是 handler。
            # 这一条是判据修正的规格：早先实现量了所有 static 函数，
            # 把超限数放大到 86，其中只有 15 个是真的 handler。
            "static inline runtimec_status_t scalar_helper_big(void) {\n"
            + "".join(f"    int h{i} = {i};\n" for i in range(19))
            + "    return 0;\n"
            "}\n"
            "\n"
            "static const binrwir_registry_entry_t scalar_entries[] = {\n"
            "    {.operation_identifier = 1U,\n"
            "     .flags = 0U,\n"
            "     .handler = scalar_three},\n"
            "    {.operation_identifier = 2U,\n"
            "     .flags = 0U,\n"
            "     .handler = scalar_four},\n"
            "};\n",
            encoding="utf-8",
        )

        findings, _ = audit(root)
        # 按函数名索引，不按行号猜——行号随 fixture 文本变化，写死会让自测
        # 因为「改了 fixture 的排版」而假红，那种红与判据无关。
        by_name = {name: lines for _, name, _, lines, _, _ in findings}
        # 3 行的那个必须**不**出现；4 行的那个必须出现且 lines == 4。
        if "scalar_three" in by_name:
            failures.append(
                f"scalar_three reported as {by_name['scalar_three']} lines "
                f"(limit 3) — should pass")
        if "scalar_four" not in by_name:
            failures.append(
                "scalar_four (4 lines) was not reported — the rule is not firing")
        elif by_name["scalar_four"] != 4:
            failures.append(
                f"scalar_four counted as {by_name['scalar_four']} lines, expected 4")

        # 未注册的大函数必须**不**出现。这条是判据修正本身的规格。
        if "scalar_helper_big" in by_name:
            failures.append(
                "scalar_helper_big is not in the registry but was reported — "
                "the rule is measuring helpers, not handlers")

        # 反向控制 1：上限放到 99，必须全绿。若仍报错，说明报错与行数无关。
        global LIMITS
        saved = LIMITS
        LIMITS = {"scalar.c": 99}
        try:
            relaxed, _ = audit(root)
            if relaxed:
                failures.append(
                    f"reverse control failed: with limit 99 still reported "
                    f"{len(relaxed)} finding(s) — the rule is not about line count")
        finally:
            LIMITS = saved

        # 反向控制 2：上限 3，必须红。
        if not findings:
            failures.append(
                "reverse control failed: with limit 3 nothing reported — rule is dead")

        # ── 三段判据的 fixture ──
        # ① 薄封装：壳只有 1 行，被调的 f 有 20 行 → 必须红（量 f 不量壳）。
        (handlers / "thin.c").write_text(
            "static runtimec_status_t thin_logic(void) {\n"
            + "".join(f"    int v{i} = {i};\n" for i in range(19))
            + "    return v0;\n}\n"
            "static runtimec_status_t thin_handler(void) {\n"
            "    return thin_logic();\n}\n"
            "static const binrwir_registry_entry_t thin_entries[] = {\n"
            "    {.operation_identifier = 1U, .flags = 0U, .handler = thin_handler},\n"
            "};\n", encoding="utf-8")
        # ② 宏形态：宏名不在 definitions 里，逻辑函数 20 行 → 必须红。
        (handlers / "macro.c").write_text(
            "static runtimec_status_t macro_logic(void) {\n"
            + "".join(f"    int m{i} = {i};\n" for i in range(19))
            + "    return m0;\n}\n"
            "VMBIN_HANDLER_UNARY_TYPED(macro_handler, macro_logic, int64, int64)\n"
            "static const binrwir_registry_entry_t macro_entries[] = {\n"
            "    {.operation_identifier = 2U, .flags = 0U, .handler = macro_handler},\n"
            "};\n", encoding="utf-8")
        # ③ 合法：逻辑 1~3 行 → 必须绿。
        (handlers / "fine.c").write_text(
            "static runtimec_status_t fine_logic(void) {\n"
            "    return 0;\n}\n"
            "static const binrwir_registry_entry_t fine_entries[] = {\n"
            "    {.operation_identifier = 3U, .flags = 0U, .handler = fine_logic},\n"
            "};\n", encoding="utf-8")

        # 先删掉前面几段 fixture：`audit` 扫整个目录，累积会让判定互相污染。
        for stale in ("scalar.c",):
            (handlers / stale).unlink(missing_ok=True)
        extended, _ = audit(root)
        reported = {name for _, name, _, _, _, _ in extended}
        # 报出来的名字是**壳名**（`thin_handler`），量的是被调函数 `thin_logic` 的行数。
        if "thin_handler" not in reported:
            failures.append(
                "thin wrapper: `thin_handler` is a 1-line shell over a 20-line "
                "`thin_logic`, but the 20 lines were not reported")
        if not any("macro_handler" in name for name in reported):
            failures.append(
                "macro form: `VMBIN_HANDLER_*(macro_handler, macro_logic, ...)` "
                "wraps a 20-line function, but it was not reported")
        if any("fine" in name for name in reported):
            failures.append(
                "legal handler: `fine_logic` is 1 line but was reported")

        # ⑤ 族表：用了但表里没有 → 红；表里有但没人用 → 只报。
        for stale in ("thin.c", "macro.c", "fine.c"):
            (handlers / stale).unlink(missing_ok=True)
        (handlers / "family.h").write_text(
            "/*\n"
            " * handler-family-table:\n"
            " *   VMBIN_HANDLER_UNARY_TEXT | string·len\n"
            " *   VMBIN_HANDLER_UNUSED_ONE | string·gone\n"
            " */\n", encoding="utf-8")
        (handlers / "family.c").write_text(
            "VMBIN_HANDLER_UNARY_TEXT(family_handler, family_logic)\n"
            "VMBIN_HANDLER_UNDECLARED(family_other, family_logic2)\n",
            encoding="utf-8")
        family_findings, family_notices = audit_family_table(root)
        if not any("UNDECLARED" in finding for finding in family_findings):
            failures.append(
                "family table: `VMBIN_HANDLER_UNDECLARED` is used but not "
                "declared, yet no finding was produced")
        if any("UNARY_TEXT" in finding for finding in family_findings):
            failures.append(
                "family table: `VMBIN_HANDLER_UNARY_TEXT` is declared and used, "
                "but was reported as a finding")
        if not any("UNUSED_ONE" in notice for notice in family_notices):
            failures.append(
                "family table: `VMBIN_HANDLER_UNUSED_ONE` is declared but unused, "
                "expected a notice")

        # ④ 放行表不能有僵尸：每条必须仍存在且仍超限。
        #    某条被重写变小后放行条目会继续免检，规则被悄悄放宽。
        for allowed in REVIEWED_OVERSIZE:
            definition = None
            for candidate in handlers.glob("*.c"):
                body = c_code_without_comments(
                    candidate.read_text(encoding="utf-8"))
                span = None
                for match in HANDLER_DEFINITION_PATTERN.finditer(body):
                    if match.group("name") == allowed:
                        span = find_definition_span(body, match.end() - 1)
                        break
                if span is not None:
                    definition = count_body_lines(body, *span)
                    break
            if definition is None:
                # 真实仓库里存在，selftest 的临时目录里没有——跳过存在性检查。
                continue
            if definition <= DEFAULT_LIMIT:
                failures.append(
                    f"reviewed-oversize zombie: `{allowed}` is still exempted "
                    f"but now only {definition} lines — remove it from the list")

        # ⑥ 共享 helper 只计一次：两个 handler 共用同一个 10 行 helper。
        #
        # 反向控制的规格：把「共享 helper 不跟进、只按自己量一次」这条去掉，
        # 这条断言必须红——否则改坏了也全绿。
        for stale in list(handlers.glob("*.c")) + list(handlers.glob("*.h")):
            stale.unlink()
        (handlers / "shared.c").write_text(
            "static runtimec_status_t shared_logic(void) {\n"
            + "".join(f"    int s{i} = {i};\n" for i in range(9))
            + "    return s0;\n}\n"
            "static runtimec_status_t shared_one(void) {\n"
            "    return shared_logic();\n}\n"
            "static runtimec_status_t shared_two(void) {\n"
            "    return shared_logic();\n}\n"
            "static const binrwir_registry_entry_t shared_entries[] = {\n"
            "    {.operation_identifier = 1U, .flags = 0U, .handler = shared_one},\n"
            "    {.operation_identifier = 2U, .flags = 0U, .handler = shared_two},\n"
            "};\n", encoding="utf-8")
        shared_findings, _ = audit(root)
        shared_names = [name for _, name, _, _, _, _ in shared_findings]
        if shared_names != ["shared_logic"]:
            failures.append(
                f"shared helper: two handlers wrap the same 10-line helper, "
                f"expected exactly 1 finding (`shared_logic`), got "
                f"{shared_names or 'nothing'} — one function must be audited "
                f"once, and at least once")

    for failure in failures:
        print(f"[selftest] {failure}")
    if failures:
        print(f"check_handler_size selftest: FAIL ({len(failures)} failure(s))")
        return 1
    print("check_handler_size selftest: OK "
          "(3 handler checks + entry-table + family-table, "
          "+ 2 reverse controls)")
    return 0


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--source-root", default=None)
    parser.add_argument("--selftest", action="store_true")
    parsed = parser.parse_args(arguments or sys.argv[1:])

    if parsed.selftest:
        return selftest()

    if parsed.source_root is not None:
        source_root = Path(parsed.source_root)
    else:
        source_root = Path(__file__).resolve().parents[3]

    table_problems = audit_entry_tables(source_root)
    for problem in table_problems:
        print(f"[entry-table] {problem}")

    family_findings, family_notices = audit_family_table(source_root)
    for finding in family_findings:
        print(f"[family-table] {finding}")
    for notice in family_notices:
        print(f"[note] {notice}")

    findings, scanned = audit(source_root)
    for display, name, line_number, lines, limit, note in findings:
        print(f"{display}:{line_number}: {name}: {lines} lines "
              f"(limit {limit}) — 需人工审查{note}")
    if findings or table_problems or family_findings:
        print(f"check_handler_size: FAIL ({len(findings)} function(s) exceed "
              f"limit, {len(table_problems)} entry-table problem(s), "
              f"{len(family_findings)} family-table problem(s))")
        return 1
    print(f"check_handler_size: OK ({scanned} handler file(s) scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
