#!/usr/bin/env python3
"""Prove every registered myrwircap handler has an implementation in the runtime.

Gate (non-zero exit on any finding or on any inability to check):

    python3 runtimec/tools/architecture/check_handler_definitions.py --source-root <repo>

Why this exists: `rwir/rwir_catalog.yml` claims runtimec can execute a capability.
The catalog generator proves each claimed capability descends from a registered surface
name; this checker proves each registered name lands on a handler that runtimec itself
defines. An operator whose implementation lives in another runtime (an extension such as
byteseek) must not be registered here.

This is the **second** line of defense, and it says so: it proves "runtimec has an
implementation", not "that implementation is not somebody else's semantics". A handler
called `kvlangBuiltinPrintln` sitting in `runtime/src/` satisfies it. The primary
judgment is the catalog generator's: a capability row must descend from a registered
surface name or from a declared specialization rule.

The registry is expanded with the C preprocessor, because the handler families are
produced by macros (`CAP_INT_BASE`, `DEF_I64_BIN`, …) and are invisible in the raw text.
Every resource this needs — the registry source, the runtime sources, a working
preprocessor — is mandatory: anything missing is a hard failure, never a skip.
"""

from __future__ import annotations

import argparse
import re

import yaml
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

#
# ── 路径与形状都换过一次，别再按「只改了路径」理解 ────────────────────────
#
# 本检查器原本校的是 `runtime/src/myrwircaps.c`：一张 `{名字, handler}` 两列表，
# handler 是**函数名**，于是「注册的名字有没有实现」可以靠文本匹配函数定义来答。
#
# 那张表和那个目录**都已经不存在**。现在的形状是三段，每段各管一件事：
#
#   1. `runtimec/specification/myrwircaps.c`  —— 只有 `{opcode, origin}`，**没有
#      handler 列**。它答「哪些名字由本 runtime 兑现」，不答「怎么兑现」；
#   2. `runtimec/rwir/rwir_catalog.yml` —— 每个名字对应一个**数字 opid**
#      （`RUNTIMEC_RWIR_OPERATION(SYMBOL, "名字", 0xNNNN, …)`），这是名字与 opid
#      之间的唯一桥；
#   3. `runtimec/vmbin/handlers/*.c` —— 各族的 `{opid, flags, handler}` 条目表，
#      `session.c` 把它们装进 `binrwir_registry_t`。
#
# 于是判据也跟着变：**一个名字声明的 opid，必须在真正的 registry 里查得到**
# （`binrwir_registry_find`）。这比原来的「某个同名函数存在」强——原来那版对
# 「函数在但没注册」是瞎的，而那正是会让算子静默走 FOREIGN 的形态。
REGISTRY_SOURCE = Path("runtimec/rwir/myrwircaps.yml")
CATALOG_SOURCE = Path("runtimec/rwir/rwir_catalog.yml")
ALIAS_TABLE_SOURCE = Path("runtimec/specification/rwir_alias_table.def")
SPECIALIZATION_TABLE = Path("runtimec/binrwir/specialization_table.c")
HANDLER_SOURCES = Path("runtimec/vmbin/handlers")
REGISTRY_SOURCE_GLOBS = (
    "runtimec/rwir/myrwircaps.yml",
    "*/rwir/myrwircaps.yml",
    "rwir/myrwircaps.yml",
)
# 只做**降级**检查（条目存在），不反推任何东西——见文件头「这不是计数器」。
# 名字集合本身可由 rwir_catalog.yml 直接解析，不需要它；留着它是为了在文件被
# 移走时报一句有方向的话，而不是笼统的「找不到」。
PROBE_SOURCES = (Path("runtimec/binrwir/operation_identifiers.h"),)

# ── 由解释器**直接分派**、因此不该在 registry 里的算子 ──────────────────
#
# 判据不是「我猜哪些是分派型」，是**解释器的 switch 分支**：
# `runtimec/vmbin/interpreter.c` 的 `switch (operation_identifier)` 只有五个显式
# case（copy / goto / br / call / return），其余全落 `dispatch_builtin`——而那条
# 路径要求 registry 命中，未命中即 INVALID。
#
# 所以这张表**必须与那个 switch 一一对应**。表里多写一个（把本该有 handler 的
# 算子豁免掉），缺陷就被藏起来；少写一个（分明是分派型却要求 handler），就是
# 假红。上一句的 `HANDLED_INLINE` 名字取自 switch，改 switch 要同步改这里。
HANDLED_INLINE = frozenset({
    "=",       # BINRWIR_OPERATION_IDENTIFIER_COPY
    "goto",    # BINRWIR_OPERATION_IDENTIFIER_GOTO
    "br",      # BINRWIR_OPERATION_IDENTIFIER_BRANCH
    "call",    # BINRWIR_OPERATION_IDENTIFIER_CALL
    "return",  # BINRWIR_OPERATION_IDENTIFIER_RETURN
})


@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str
    detail: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] {self.detail}"


def missing_resource(root: Path, relative: Path) -> Finding:
    return Finding(relative.as_posix(), 1, "missing-runtime-source",
                   f"{relative} is required to check handler definitions")


def locate_registry_source(root: Path) -> Path | None:
    """找到登记表；找不到就返回 None（调用方负责报**有方向**的失败）。"""
    for pattern in REGISTRY_SOURCE_GLOBS:
        for candidate in sorted(root.glob(pattern)):
            if candidate.is_file():
                return candidate
    return None


def missing_registry_finding(root: Path) -> Finding:
    """登记表缺失时的诊断：**说清去找过哪、怎么找**，不是笼统的 missing。

    原来那句 `runtime/src/myrwircaps.c is required` 在文件迁走之后只说明
    「红的」，不说明「该怎么办」——于是它红了一整段时间没人动。诊断要能直接
    指向动作。
    """
    searched = "、".join(REGISTRY_SOURCE_GLOBS)
    return Finding(
        REGISTRY_SOURCE.as_posix(), 1, "missing-registry-source",
        f"找不到登记表源（可能又迁移了）：按 {searched} 都没命中。"
        f"查找方式：`find <repo> -name myrwircaps.c -not -path '*/workspace/*'`；"
        f"找到后把本文件顶部的 REGISTRY_SOURCE / REGISTRY_SOURCE_GLOBS 指过去。",
    )


def preprocess(root: Path, source: Path, kvspace_include: Path | None = None) -> str:
    """展开一个翻译单元；行标记去掉（行号对本检查器无意义）。

    include 目录按**实际布局**给：`runtimec/` 是各组件 header 的根（`binrwir/…`、
    `vmbin/…` 都相对它），`runtimec/include` 是公共头。原来的 `runtime/` 三个
    目录已不存在，`-I` 指不到东西时 gcc 会**静默忽略**，于是宏展开不完整、
    解析出的条目变少——那正是「检查器看起来在跑，其实查了个空」的来源。
    """
    command = ["gcc", "-E"]
    for directory in ("runtimec", "runtimec/include", "include"):
        candidate = root / directory
        if candidate.is_dir():
            command.extend(["-I", str(candidate)])
    # kvspace 的头是**兄弟仓**提供的（`<kvspace/kvspace_xvalue.h>`），与本仓
    # 不同源。刻意**不写死本机路径**：写死就是把「某个开发机的目录布局」当成
    # 契约，换台机器/换 pod 就静默 gcc 失败。
    #
    # **来源是命令行参数，不是环境变量**：kvlang 只有一个环境变量 `KVSPACE`
    # （用户裁决），新增 env 会破坏这条硬约束。命令行参数天然是「每次调用的
    # 输入」，语义也更贴切——同一份源码在不同机器上由调用方给出各自的头路径。
    if kvspace_include is not None:
        command.extend(["-I", str(kvspace_include)])
    command.append(str(source))
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    if completed.returncode != 0:
        # **缺参数时要给可照抄的命令**，不是把 gcc 的原始报错端出来：
        # gcc 只说 `No such file or directory`，读的人还得自己推是哪个头、
        # 哪个参数。诊断要能直接指向动作。
        hint = ""
        if kvspace_include is None:
            hint = (
                "\n  提示：handler 源要 include <kvspace/kvspace_xvalue.h>，"
                "该头在兄弟仓。加参数重跑：\n"
                "    python3 runtimec/tools/architecture/check_handler_definitions.py "
                "--kvspace-include=<kvspace 仓的 include/>"
            )
        raise RuntimeError(
            f"cannot preprocess {source}: "
            f"{completed.stderr.strip().splitlines()[:1]}{hint}")
    return "\n".join(line for line in completed.stdout.splitlines()
                     if not line.startswith("#"))


def registered_surfaces(root: Path, registry: Path,
                        kvspace_include: Path | None) -> list[str]:
    """登记表里的 surface 名字，保持表内顺序（顺序决定 opid 分配）。"""
    del kvspace_include  # 名字表已是纯数据，不再需要预处理器
    document = yaml.safe_load(registry.read_text(encoding="utf-8"))
    return [str(entry["name"]) for entry in document.get("names", [])]


def surface_symbol(aliases: dict[str, str], surface: str) -> str | None:
    """名字 → 能力符号，**直接读别名表**，不在这里另立折算规则。

    我第一版是「把名字里的非字母数字折成下划线再大写」，结果 `||` `×` `√` `≥`
    这类**别名**全部对不上——它们在生成器里映射到的是别的符号（`×` →
    `MUL`、`≥` → `GREATER_EQUAL`），不是从字面折出来的。

    这正是「同一事实两个来源」：折算规则只有生成器一份，别名表是它的产物。
    再写一份折算，两份迟早漂开，而漂开的症状是「一堆本该通过的条目被判缺失」。
    """
    return aliases.get(surface)


def surface_aliases(root: Path) -> dict[str, str]:
    """`rwir_alias_table.def` 的 surface 名字 → 能力符号。

    这是生成器产出的**权威映射**；名字与符号之间没有第二条路。没有进别名表的
    名字会落在缺口清单里（`rwir_catalog_gaps.md`），那是待裁项，不是本检查器
    的失败——所以查不到时下面的调用方报 `surface-without-catalog-row`。
    """
    path = root / ALIAS_TABLE_SOURCE
    text = path.read_text(encoding="utf-8")
    aliases: dict[str, str] = {}
    for match in re.finditer(
            r'RUNTIMEC_RWIR_ALIAS\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*'
            r"(RUNTIMEC_RWIR_[A-Za-z0-9_]+)\s*\)", text):
        aliases[match.group(1)] = match.group(2)
    return aliases


def registered_operation_codes(root: Path, handler_directory: Path,
                               kvspace_include: Path | None) -> set[int]:
    """**真正的 registry** 里有 handler 的 opid 集合。

    全量扫一次（每个 handler 源只预处理一遍），而不是「每个名字各查一遍」——
    后者是 O(名字数 × 源文件数) 次 gcc 调用，三百个名字根本跑不完。
    """
    return _registry_scan(root, handler_directory, kvspace_include)


def _registry_scan(root: Path, handler_directory: Path,
                   kvspace_include: Path | None) -> set[int]:
    """**任一 handler 源读不了，就整体判无法检查**，不许「跳过它继续」。

    第一版是逐个 `preprocess`、失败就抛——但调用点在 try 外面，异常直接冒成
    Python 堆栈；我改成「读不了就跳过」之后更糟：`string.c` 因缺
    `kvlang/xvalue.h` 预处理失败，于是**整个 string 模块的 14 条被静默漏读**，
    报成「声明了却没 handler」。那是把「检查器读不到」伪装成「被测对象有缺陷」
    ——假红，而且比假绿更能骗人，因为它看起来像发现了问题。
    """
    # 两种写法都要认：**指定式** `{.operation_identifier = X}` 与**位置式**
    # `{X, flags, handler}`。生成物用位置式（`scalar_generated.c`、`string.c`），
    # 手写表用指定式。只认一种就会把整个模块读成「没有 handler」——而那正是
    # 我第一版把 14 条 string 算子判成缺失的原因。
    literal_pattern = re.compile(
        r"\{\s*(?:\.operation_identifier\s*=\s*)?"
        r"(?:UINT16_C\(\s*)?0x([0-9A-Fa-f]+)"
    )
    name_pattern = re.compile(
        r"\{\s*\.operation_identifier\s*=\s*BINRWIR_OPERATION_IDENTIFIER_"
        r"([A-Za-z0-9_]+)"
    )
    identifiers = operation_identifier_constants(root)
    codes: set[int] = set()
    for source in sorted(handler_directory.glob("*.c")):
        text = preprocess(root, source, kvspace_include)
        for match in literal_pattern.finditer(text):
            codes.add(int(match.group(1), 16))
        for match in name_pattern.finditer(text):
            resolved = identifiers.get(match.group(1))
            if resolved is not None:
                codes.add(resolved)
    return codes


def operation_identifier_constants(root: Path) -> dict[str, int]:
    """`operation_identifiers.h` 的符号 → opid。

    条目表里写的是 `BINRWIR_OPERATION_IDENTIFIER_ADD` 这种宏，不是裸数字；
    不解析这一层，「用宏写的条目」会被全部判成缺失。
    """
    path = root / PROBE_SOURCES[0]
    if not path.is_file():
        return {}
    text = path.read_text(encoding="utf-8")
    constants: dict[str, int] = {}
    for match in re.finditer(
            r"#define\s+BINRWIR_OPERATION_IDENTIFIER_([A-Za-z0-9_]+)\s+"
            r"UINT16_C\(0x([0-9A-Fa-f]+)\)", text):
        constants[match.group(1)] = int(match.group(2), 16)
    return constants


def catalog_operation_codes(root: Path) -> dict[str, int]:
    """`rwir_catalog.yml` 里每个能力符号对应的数字 opid。

    这是**名字 → opid** 的唯一桥。登记表只给名字，handler 表只给 opid，两边
    没有共同的键——若无此表，任何「它们对得上」的结论都是猜的。
    """
    path = root / CATALOG_SOURCE
    document = yaml.safe_load(path.read_text(encoding="utf-8"))
    codes: dict[str, int] = {}
    for family in document.get("families", []):
        for operation in family.get("operations", []):
            code = operation["opid"]
            codes[str(operation["identifier"])] = (
                code if isinstance(code, int) else int(code, 0))
    return codes


def audit(root: Path,
          kvspace_include: Path | None = None) -> tuple[list[Finding], int]:
    """`checked` 是**真的查过 registry 的名字数**，不是「解析到的名字数」。

    这两者必须分开报：如果某个环节静默返回空集合，checked 会跟着变成 0，
    于是「0 findings」看起来像「全绿」。调用方据此拒绝 0 值——见 main()。
    """
    root = root.resolve()
    registry = locate_registry_source(root)
    if registry is None:
        return [missing_registry_finding(root)], 0
    catalog = root / CATALOG_SOURCE
    if not catalog.is_file():
        return [missing_resource(root, CATALOG_SOURCE)], 0
    handler_directory = root / HANDLER_SOURCES
    if not handler_directory.is_dir():
        return [missing_resource(root, HANDLER_SOURCES)], 0

    try:
        surfaces = registered_surfaces(root, registry, kvspace_include)
        codes = catalog_operation_codes(root)
        aliases = surface_aliases(root)
        # 扫描 handler 源也在这段 try 里：任一文件预处理失败即「无法检查」，
        # 不能跳过它继续报「这条没 handler」——那是把读不到伪装成缺陷。
        available = registered_operation_codes(root, handler_directory,
                                              kvspace_include)
    except (RuntimeError, OSError) as error:
        return [Finding(REGISTRY_SOURCE.as_posix(), 1,
                        "uncheckable-handler-definition", str(error))], 0
    if not surfaces:
        return [Finding(REGISTRY_SOURCE.as_posix(), 1,
                        "uncheckable-handler-definition",
                        "登记表解析出 0 个名字——判据不能对此放行")], 0
    if not codes:
        return [Finding(CATALOG_SOURCE.as_posix(), 1,
                        "uncheckable-handler-definition",
                        f"{CATALOG_SOURCE} 解析出 0 个 opid")], 0

    findings: list[Finding] = []
    checked = 0
    inlined = 0
    specialized = 0
    specialized_base_codes = specialized_bases(root)
    for surface in surfaces:
        symbol = surface_symbol(aliases, surface)
        if symbol is None:
            findings.append(Finding(
                REGISTRY_SOURCE.as_posix(), 1, "surface-without-catalog-row",
                f"{surface!r} 在登记表里，但 {ALIAS_TABLE_SOURCE} 没有它的映射"
                f"（待裁项见 rwir_catalog_gaps.md）",
            ))
            continue
        code = codes[symbol]
        if code not in available:
            # 解释器内联分派的算子**本来就不在 registry**，不算缺陷；但要单独
            # 计数并报出来，否则「豁免了几个」永远不可见。
            if surface in HANDLED_INLINE:
                inlined += 1
                checked += 1
                continue
            # **第三条路：特化。** 基础 opid 的通用 handler 已按 p10「先删后增」
            # 删掉，改成按操作数静态类型组合改写成特化 opid（0x8000+），由
            # `binrwir_binfunc_specialize` 在编译期完成。于是基础 opid 永远不进
            # 执行路径——它「没有 handler」是**设计使然**，不是缺口。
            #
            # 实测证据：探针用例 `a + b`（opid 0x0002）跑到 executed。所以判据
            # 必须是「基础 opid 在特化表里有行」，不是「它有 handler」。
            if code in specialized_base_codes:
                specialized += 1
                checked += 1
                continue
            findings.append(Finding(
                REGISTRY_SOURCE.as_posix(), 1, "registered-without-handler",
                f"{surface!r}（opid 0x{code:04X}）声明由本 runtime 兑现，"
                f"但三条路都没有：不在 binrwir_registry、不是解释器内联分派、"
                f"也不在特化表里",
            ))
        checked += 1
    global _INLINED_COUNT, _SPECIALIZED_COUNT
    _INLINED_COUNT = inlined
    _SPECIALIZED_COUNT = specialized
    return findings, checked


# 仅供 main() 报数用：各走非 registry 路径的条数。放进模块级而不是返回值，
# 是为了不改 audit() 的签名（selftest 依赖它）。
_SPECIALIZED_COUNT = 0


def specialized_bases(root: Path) -> set[int]:
    """`specialization_table.c` 里出现过的基础 opid 集合。

    这是**编译期改写**的声明：这些基础 opid 会被就地换成 0x8000+ 的特化 opid，
    于是它们自己不需要 handler。判据只读这张表的**第一列**（基础 opid）。
    """
    path = root / SPECIALIZATION_TABLE
    text = path.read_text(encoding="utf-8")
    return {
        operation_identifier_constants(root)[match.group(1)]
        for match in re.finditer(
            r"\{\s*BINRWIR_OPERATION_IDENTIFIER_([A-Za-z0-9_]+)\s*,", text)
        if match.group(1) in operation_identifier_constants(root)
    }


# 仅供 main() 报数用：豁免了多少条内联分派算子。放进模块级而不是返回值，是为了
# 不改 audit() 的签名（selftest 依赖它）。
_INLINED_COUNT = 0



# ---------------------------------------------------------------------------------------
# Self-test: the checker must be able to fail. Fixtures are synthesized in a temporary
# tree; no product source is touched.
#
# **自测必须与被测判据同形**。原来这套自测造的是 `{名字, handler}` 两列登记表，
# 而那个形状已经不存在——留着它只会让自测红在一个与产品无关的地方（实测：五个
# 用例全报 `missing-registry-source`），而「自测红」久了就没人看，等于把自测关掉。
# ---------------------------------------------------------------------------------------
SELFTEST_REGISTRY = """\
static const struct {
    const char *opcode;
    int origin;
} runtimec_myrwircaps[] = {
    {"call", 0},
    {"add", 1},
    {"nohandler", 1},
};
"""

SELFTEST_CATALOG = """\
RUNTIMEC_RWIR_OPERATION(RUNTIMEC_RWIR_CALL, "call", 0x0001, 0)
RUNTIMEC_RWIR_OPERATION(RUNTIMEC_RWIR_ADD, "add", 0x0002, 0)
RUNTIMEC_RWIR_OPERATION(RUNTIMEC_RWIR_NOHANDLER, "nohandler", 0x0099, 0)
"""

SELFTEST_ALIASES = """\
RUNTIMEC_RWIR_ALIAS("call", RUNTIMEC_RWIR_CALL)
RUNTIMEC_RWIR_ALIAS("add", RUNTIMEC_RWIR_ADD)
RUNTIMEC_RWIR_ALIAS("nohandler", RUNTIMEC_RWIR_NOHANDLER)
"""

SELFTEST_IDENTIFIERS = """\
#define BINRWIR_OPERATION_IDENTIFIER_CALL UINT16_C(0x0001)
#define BINRWIR_OPERATION_IDENTIFIER_ADD UINT16_C(0x0002)
#define BINRWIR_OPERATION_IDENTIFIER_NOHANDLER UINT16_C(0x0099)
#define BINRWIR_OPERATION_IDENTIFIER_SPEC UINT16_C(0x0003)
"""

SELFTEST_SPECIALIZATION = """\
static const binrwir_specialization_row_t rows[] = {
    {BINRWIR_OPERATION_IDENTIFIER_ADD, "int64,int64,int64", UINT16_C(0x8000)},
};
"""


def run_selftest() -> int:
    failures: list[str] = []

    def check(label: str, rules: set[str], expected: set[str]) -> None:
        if rules != expected:
            failures.append(f"{label}: expected {sorted(expected)}, got {sorted(rules)}")

    with tempfile.TemporaryDirectory(prefix="runtimec-handler-selftest-") as directory:
        root = Path(directory)
        for name in ("runtimec/specification", "runtimec/binrwir", "runtimec/include",
                     "runtimec/vmbin/handlers"):
            (root / name).mkdir(parents=True)
        registry = root / "runtimec/rwir/myrwircaps.yml"
        catalog = root / "runtimec/rwir/rwir_catalog.yml"
        aliases = root / "runtimec/specification/rwir_alias_table.def"
        identifiers = root / "runtimec/binrwir/operation_identifiers.h"
        specialization = root / "runtimec/binrwir/specialization_table.c"
        handlers = root / "runtimec/vmbin/handlers/family.c"
        registry.write_text(SELFTEST_REGISTRY, encoding="utf-8")
        catalog.write_text(SELFTEST_CATALOG, encoding="utf-8")
        aliases.write_text(SELFTEST_ALIASES, encoding="utf-8")
        identifiers.write_text(SELFTEST_IDENTIFIERS, encoding="utf-8")
        specialization.write_text(SELFTEST_SPECIALIZATION, encoding="utf-8")

        # `call` 走内联分派、`add` 走特化——两条都不该进 registry。
        # `nohandler` 三条路都没有 → 必须报。
        handlers.write_text(
            "static const binrwir_registry_entry_t entries[] = {\n"
            "    {0x0001, 0, some_handler},\n"
            "};\n", encoding="utf-8")

        findings, checked = audit(root)
        check("three-paths",
              {finding.rule for finding in findings},
              {"registered-without-handler"})
        if checked != 3:
            failures.append(f"expected 3 checked registrations, got {checked}")
        if not any("nohandler" in finding.detail for finding in findings):
            failures.append("the finding must name 'nohandler'")
        # `call` 与 `add` 各自走一条非 registry 路径：不得被报。
        for exempt in ("'call'", "'add'"):
            if any(finding.detail.startswith(exempt) for finding in findings):
                failures.append(f"{exempt} must be exempt (inline or specialized)")

        # 登记表被搬走 → 必须报**有方向**的 missing-registry-source，且
        # 诊断里要给出查找方式（原来那句只说「红的」，红了一整段没人动）。
        registry.rename(root / "moved.c")
        findings, checked = audit(root)
        check("registry-moved", {finding.rule for finding in findings},
              {"missing-registry-source"})
        if checked != 0:
            failures.append(f"checked must be 0 when nothing was checked, got {checked}")
        if not any("find" in finding.detail for finding in findings):
            failures.append("missing-registry-source must tell how to locate the table")
        (root / "moved.c").rename(registry)

        # handler 源读不了 → uncheckable，**不得**降级成「这条没 handler」。
        handlers.unlink()
        handlers.write_text("this is not C /*\n", encoding="utf-8")
        findings, _ = audit(root)
        check("unpreprocessable-handler-source",
              {finding.rule for finding in findings},
              {"uncheckable-handler-definition"})

        # 恢复后回到同一个结论：只看得到那一处真实缺口。
        handlers.write_text(
            "static const binrwir_registry_entry_t entries[] = {\n"
            "    {0x0001, 0, some_handler},\n"
            "};\n", encoding="utf-8")
        findings, _ = audit(root)
        check("restored", {finding.rule for finding in findings},
              {"registered-without-handler"})

    for failure in failures:
        print(f"[selftest] {failure}")
    if failures:
        print(f"check_handler_definitions selftest: FAIL ({len(failures)} failure(s))")
        return 1
    print("check_handler_definitions selftest: OK (5 fixtures)")
    return 0


def main(command_arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root", type=Path, default=Path(__file__).resolve().parents[3],
        help="repository source root",
    )
    parser.add_argument("--quiet", action="store_true", help="suppress success output")
    parser.add_argument("--selftest", action="store_true",
                        help="prove the checker can fail, using synthetic fixtures")
    # **命令行参数，不是环境变量**：kvlang 只有一个环境变量 `KVSPACE`（用户裁决）。
    # 与 `kv_runner --operators=a,b` 同一个理由与同一个形状。
    parser.add_argument(
        "--kvspace-include", type=Path, default=None,
        help="kvspace 仓的 include/（提供 <kvspace/kvspace_xvalue.h>）；"
             "预处理 handler 源时必填",
    )
    arguments = parser.parse_args(command_arguments or sys.argv[1:])
    if arguments.selftest:
        return run_selftest()
    findings, checked = audit(arguments.source_root, arguments.kvspace_include)
    for finding in sorted(set(findings)):
        print(finding.render())
    if findings:
        print(f"check_handler_definitions: FAIL ({len(set(findings))} finding(s))")
        return 1
    if checked == 0:
        print(f"check_handler_definitions: FAIL (0 registration(s) were actually checked -- a checker that checks nothing must not pass)")
        return 1
    if not arguments.quiet:
        print(f"check_handler_definitions: OK ({checked} registration(s) resolve to a "
              f"definition in runtime/src)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
