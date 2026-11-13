
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REGISTRY_SOURCE = Path("runtimec/specification/myrwircaps.c")
CATALOG_SOURCE = Path("runtimec/specification/rwir_catalog.def")
ALIAS_TABLE_SOURCE = Path("runtimec/specification/rwir_alias_table.def")
SPECIALIZATION_TABLE = Path("runtimec/binrwir/specialization_table.c")
HANDLER_SOURCES = Path("runtimec/vmbin/handlers")
REGISTRY_SOURCE_GLOBS = (
    "runtimec/specification/myrwircaps.c",
    "*/specification/myrwircaps.c",
    "specification/myrwircaps.c",
)
PROBE_SOURCES = (Path("runtimec/binrwir/operation_identifiers.h"),)

HANDLED_INLINE = frozenset({
    "=",
    "goto",
    "br",
    "call",
    "return",
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
    for pattern in REGISTRY_SOURCE_GLOBS:
        for candidate in sorted(root.glob(pattern)):
            if candidate.is_file():
                return candidate
    return None

def missing_registry_finding(root: Path) -> Finding:
    searched = "、".join(REGISTRY_SOURCE_GLOBS)
    return Finding(
        REGISTRY_SOURCE.as_posix(), 1, "missing-registry-source",
        f"找不到登记表源（可能又迁移了）：按 {searched} 都没命中。"
        f"查找方式：`find <repo> -name myrwircaps.c -not -path '*/workspace/*'`；"
        f"找到后把本文件顶部的 REGISTRY_SOURCE / REGISTRY_SOURCE_GLOBS 指过去。",
    )

def preprocess(root: Path, source: Path, pathspace_include: Path | None = None) -> str:
    command = ["gcc", "-E"]
    for directory in ("runtimec", "runtimec/include", "include"):
        candidate = root / directory
        if candidate.is_dir():
            command.extend(["-I", str(candidate)])
    if pathspace_include is not None:
        command.extend(["-I", str(pathspace_include)])
    command.append(str(source))
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    if completed.returncode != 0:
        hint = ""
        if pathspace_include is None:
            hint = (
                "\n  提示：handler 源要 include <pathspace/pathspace_xvalue.h>，"
                "该头在兄弟仓。加参数重跑：\n"
                "    python3 runtimec/tools/architecture/check_handler_definitions.py "
                "--pathspace-include=<pathspace 仓的 include/>"
            )
        raise RuntimeError(
            f"cannot preprocess {source}: "
            f"{completed.stderr.strip().splitlines()[:1]}{hint}")
    return "\n".join(line for line in completed.stdout.splitlines()
                     if not line.startswith("#"))

def registered_surfaces(root: Path, registry: Path,
                        pathspace_include: Path | None) -> list[str]:
    text = preprocess(root, registry, pathspace_include)
    try:
        start = text.index("runtimec_myrwircaps[] = {")
        end = text.index("\n};", start)
    except ValueError as error:
        raise RuntimeError(
            f"{registry}: 找不到 runtimec_myrwircaps[] 表——表名改了就同步改这里"
        ) from error
    names: list[str] = []
    for literal in re.findall(
            r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\s*,', text[start:end]):
        name = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', literal))
        names.append(name.replace("\\xC2\\xB7", "·"))
    return names

def surface_symbol(aliases: dict[str, str], surface: str) -> str | None:
    return aliases.get(surface)

def surface_aliases(root: Path) -> dict[str, str]:
    path = root / ALIAS_TABLE_SOURCE
    text = path.read_text(encoding="utf-8")
    aliases: dict[str, str] = {}
    for match in re.finditer(
            r'RUNTIMEC_RWIR_ALIAS\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*'
            r"(RUNTIMEC_RWIR_[A-Za-z0-9_]+)\s*\)", text):
        aliases[match.group(1)] = match.group(2)
    return aliases

def registered_operation_codes(root: Path, handler_directory: Path,
                               pathspace_include: Path | None) -> set[int]:
    return _registry_scan(root, handler_directory, pathspace_include)

def _registry_scan(root: Path, handler_directory: Path,
                   pathspace_include: Path | None) -> set[int]:
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
        text = preprocess(root, source, pathspace_include)
        for match in literal_pattern.finditer(text):
            codes.add(int(match.group(1), 16))
        for match in name_pattern.finditer(text):
            resolved = identifiers.get(match.group(1))
            if resolved is not None:
                codes.add(resolved)
    return codes

def operation_identifier_constants(root: Path) -> dict[str, int]:
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
    path = root / CATALOG_SOURCE
    text = path.read_text(encoding="utf-8")
    codes: dict[str, int] = {}
    for match in re.finditer(
            r"RUNTIMEC_RWIR_OPERATION\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,"
            r"\s*(?:\"(?:[^\"\\]|\\.)*\"\s*,\s*)?0x([0-9A-Fa-f]+)", text):
        symbol, code = match.group(1), int(match.group(2), 16)
        codes[symbol] = code
    return codes

def audit(root: Path,
          pathspace_include: Path | None = None) -> tuple[list[Finding], int]:
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
        surfaces = registered_surfaces(root, registry, pathspace_include)
        codes = catalog_operation_codes(root)
        aliases = surface_aliases(root)
        available = registered_operation_codes(root, handler_directory,
                                              pathspace_include)
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
            if surface in HANDLED_INLINE:
                inlined += 1
                checked += 1
                continue
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

_SPECIALIZED_COUNT = 0

def specialized_bases(root: Path) -> set[int]:
    path = root / SPECIALIZATION_TABLE
    text = path.read_text(encoding="utf-8")
    return {
        operation_identifier_constants(root)[match.group(1)]
        for match in re.finditer(
            r"\{\s*BINRWIR_OPERATION_IDENTIFIER_([A-Za-z0-9_]+)\s*,", text)
        if match.group(1) in operation_identifier_constants(root)
    }

_INLINED_COUNT = 0

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
        registry = root / "runtimec/specification/myrwircaps.c"
        catalog = root / "runtimec/specification/rwir_catalog.def"
        aliases = root / "runtimec/specification/rwir_alias_table.def"
        identifiers = root / "runtimec/binrwir/operation_identifiers.h"
        specialization = root / "runtimec/binrwir/specialization_table.c"
        handlers = root / "runtimec/vmbin/handlers/family.c"
        registry.write_text(SELFTEST_REGISTRY, encoding="utf-8")
        catalog.write_text(SELFTEST_CATALOG, encoding="utf-8")
        aliases.write_text(SELFTEST_ALIASES, encoding="utf-8")
        identifiers.write_text(SELFTEST_IDENTIFIERS, encoding="utf-8")
        specialization.write_text(SELFTEST_SPECIALIZATION, encoding="utf-8")

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
        for exempt in ("'call'", "'add'"):
            if any(finding.detail.startswith(exempt) for finding in findings):
                failures.append(f"{exempt} must be exempt (inline or specialized)")

        registry.rename(root / "moved.c")
        findings, checked = audit(root)
        check("registry-moved", {finding.rule for finding in findings},
              {"missing-registry-source"})
        if checked != 0:
            failures.append(f"checked must be 0 when nothing was checked, got {checked}")
        if not any("find" in finding.detail for finding in findings):
            failures.append("missing-registry-source must tell how to locate the table")
        (root / "moved.c").rename(registry)

        handlers.unlink()
        handlers.write_text("this is not C /*\n", encoding="utf-8")
        findings, _ = audit(root)
        check("unpreprocessable-handler-source",
              {finding.rule for finding in findings},
              {"uncheckable-handler-definition"})

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
    parser.add_argument(
        "--pathspace-include", type=Path, default=None,
        help="pathspace 仓的 include/（提供 <pathspace/pathspace_xvalue.h>）；"
             "预处理 handler 源时必填",
    )
    arguments = parser.parse_args(command_arguments or sys.argv[1:])
    if arguments.selftest:
        return run_selftest()
    findings, checked = audit(arguments.source_root, arguments.pathspace_include)
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
