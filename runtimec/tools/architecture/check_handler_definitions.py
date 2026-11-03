#!/usr/bin/env python3
"""Prove every registered myrwircap handler has an implementation in the runtime.

Gate (non-zero exit on any finding or on any inability to check):

    python3 runtimec/tools/architecture/check_handler_definitions.py --source-root <repo>

Why this exists: `specification/rwir_catalog.def` claims runtimec can execute a capability.
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
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

REGISTRY_SOURCE = Path("runtime/src/myrwircaps.c")
RUNTIME_DIRECTORIES = (Path("runtime/src"), Path("runtime/include"), Path("include"))
HANDLER_SENTINELS = frozenset({"kvlangKvcpuRoute"})


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


def preprocess(root: Path, source: Path) -> str:
    """Expand one translation unit; return the code with line markers removed."""
    command = ["gcc", "-E"]
    for directory in RUNTIME_DIRECTORIES:
        command.extend(["-I", str(root / directory)])
    command.append(str(source))
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    if completed.returncode != 0:
        raise RuntimeError(
            f"cannot preprocess {source}: {completed.stderr.strip().splitlines()[:1]}")
    return "\n".join(line for line in completed.stdout.splitlines()
                     if not line.startswith("#"))


def registered_handlers(root: Path) -> list[tuple[str, str]]:
    """Return (surface name, handler identifier) for every registry entry."""
    text = preprocess(root, root / REGISTRY_SOURCE)
    try:
        start = text.index("myrwircaps[] = {")
        end = text.index("\n};", start)
    except ValueError as error:
        raise RuntimeError(f"{REGISTRY_SOURCE}: capability table not found") from error
    entries = re.findall(
        r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)',
        text[start:end],
    )
    if not entries:
        raise RuntimeError(f"{REGISTRY_SOURCE}: capability table is empty")
    handlers: list[tuple[str, str]] = []
    for literal, handler in entries:
        name = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', literal))
        name = name.replace("\\xC2\\xB7", "·")
        handlers.append((name, handler))
    return handlers


def defined_functions(code: str) -> set[str]:
    """Identifiers followed by a balanced parameter list and then a body.

    A declaration ends at `;` before any top-level `{`, so it is not a definition. The
    scan ignores comments and literals because the C preprocessor has already removed
    comments and the parameter list never contains a brace.
    """
    definitions: set[str] = set()
    for match in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", code):
        name = match.group(1)
        depth = 1
        index = match.end()
        while index < len(code):
            character = code[index]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    break
            elif character in ";{" and depth == 1:
                break
            index += 1
        else:
            continue
        if depth != 0:
            continue
        index += 1
        while index < len(code) and code[index] in " \t\r\n":
            index += 1
        if index < len(code) and code[index] == "{":
            definitions.add(name)
    return definitions


def audit(root: Path) -> tuple[list[Finding], int]:
    root = root.resolve()
    registry = root / REGISTRY_SOURCE
    if not registry.is_file():
        return [missing_resource(root, REGISTRY_SOURCE)], 0
    for directory in (root / "runtime" / "src", root / "runtime" / "include",
                      root / "include"):
        if not directory.is_dir():
            return [missing_resource(root, directory.relative_to(root))], 0

    try:
        handlers = registered_handlers(root)
        definitions: set[str] = set()
        sources = sorted((root / "runtime" / "src").glob("*.c"))
        if not sources:
            return [missing_resource(root, Path("runtime/src/*.c"))], 0
        for source in sources:
            definitions |= defined_functions(preprocess(root, source))
    except (RuntimeError, OSError) as error:
        return [Finding(REGISTRY_SOURCE.as_posix(), 1, "uncheckable-handler-definition",
                        str(error))], 0
    if not definitions:
        return [Finding(REGISTRY_SOURCE.as_posix(), 1, "uncheckable-handler-definition",
                        "no function definitions were found in runtime/src")], 0

    findings: list[Finding] = []
    checked = 0
    for surface, handler in handlers:
        if handler in HANDLER_SENTINELS:
            continue
        checked += 1
        if handler not in definitions:
            findings.append(Finding(
                REGISTRY_SOURCE.as_posix(), 1, "handler-without-implementation",
                f"{surface!r} registers {handler}, which runtime/src does not define",
            ))
    return findings, checked


# ---------------------------------------------------------------------------------------
# Self-test: the checker must be able to fail. Fixtures are synthesized in a temporary
# tree; no product source is touched.
# ---------------------------------------------------------------------------------------
SELFTEST_REGISTRY = """\
static const kvlangRwirCap myrwircaps[] = {
    {"add", kvlangBuiltinAdd, false},
    {"println", kvlangBuiltinPrintln, false},
    {NULL, kvlangKvcpuRoute, false},
};
"""

SELFTEST_SOURCE = """\
int kvlangBuiltinAdd(kvlangFrame_t *f) {
    return 0;
}

static int declared_only(kvlangFrame_t *f);
"""

def run_selftest() -> int:
    failures: list[str] = []

    def check(label: str, rules: set[str], expected: set[str]) -> None:
        if rules != expected:
            failures.append(f"{label}: expected {sorted(expected)}, got {sorted(rules)}")

    with tempfile.TemporaryDirectory(prefix="runtimec-handler-selftest-") as directory:
        root = Path(directory)
        for name in ("runtime/src", "runtime/include", "include"):
            (root / name).mkdir(parents=True)
        registry = root / "runtime" / "src" / "myrwircaps.c"
        implementations = root / "runtime" / "src" / "handlers.c"
        registry.write_text(SELFTEST_REGISTRY, encoding="utf-8")
        implementations.write_text(SELFTEST_SOURCE, encoding="utf-8")

        # 声明不是定义：只有 kvlangBuiltinAdd 带函数体。
        defined = defined_functions(preprocess(root, implementations))
        if defined != {"kvlangBuiltinAdd"}:
            failures.append(
                f"definition scan: expected only kvlangBuiltinAdd, got {sorted(defined)}")

        # println 已注册但无实现 → 必须报；声明式原型不得被当成实现。
        findings, checked = audit(root)
        check("registered-without-implementation",
              {finding.rule for finding in findings},
              {"handler-without-implementation"})
        if checked != 2:
            failures.append(f"expected 2 checked registrations, got {checked}")

        # 实现源整个消失 → 定义集为空 → 必须报 uncheckable，不得静默通过。
        implementations.unlink()
        findings, _ = audit(root)
        check("implementation-source-missing",
              {finding.rule for finding in findings},
              {"uncheckable-handler-definition"})
        implementations.write_text(SELFTEST_SOURCE, encoding="utf-8")

        # 资源目录缺失 → 硬失败。
        include_directory = root / "runtime" / "include"
        saved = root / "include_saved"
        include_directory.rename(saved)
        findings, _ = audit(root)
        check("include-directory-missing",
              {finding.rule for finding in findings},
              {"missing-runtime-source"})
        saved.rename(include_directory)

        # 恢复后回到同一个结论：只看得到一条真实缺陷。
        findings, _ = audit(root)
        check("restored",
              {finding.rule for finding in findings},
              {"handler-without-implementation"})

    for failure in failures:
        print(f"[selftest] {failure}")
    if failures:
        print(f"check_handler_definitions selftest: FAIL ({len(failures)} failure(s))")
        return 1
    print("check_handler_definitions selftest: OK (4 fixtures)")
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
    arguments = parser.parse_args(command_arguments or sys.argv[1:])
    if arguments.selftest:
        return run_selftest()
    findings, checked = audit(arguments.source_root)
    for finding in sorted(set(findings)):
        print(finding.render())
    if findings:
        print(f"check_handler_definitions: FAIL ({len(set(findings))} finding(s))")
        return 1
    if not arguments.quiet:
        print(f"check_handler_definitions: OK ({checked} registration(s) resolve to a "
              f"definition in runtime/src)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
