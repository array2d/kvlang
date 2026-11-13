
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_names import c_code_without_comments

HANDLERS_DIRECTORY = Path("runtimec/vmbin/handlers")

DEFAULT_LIMIT = 5
LIMITS = {
    "scalar.c": 3,
    "host.c": 30,
}

HANDLER_DEFINITION_PATTERN = re.compile(
    r"^[ \t]*static[ \t]+(?:inline[ \t]+)?"
    r"(?P<return_type>runtimec_status_t|int|void)\s+"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(",
    re.MULTILINE,
)

REGISTERED_HANDLER_PATTERN = re.compile(
    r"\.handler\s*=\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)")
POSITIONAL_HANDLER_PATTERN = re.compile(
    r"^\s*\{\s*[^,{}]+,\s*[^,{}]+,\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\}\s*,?\s*$",
    re.MULTILINE)

BRACE_ONLY_PATTERN = re.compile(r"^\s*[{}]?;?\s*$")

def limit_for(path: Path) -> int:
    return LIMITS.get(path.name, DEFAULT_LIMIT)

def find_definition_span(code: str, open_paren: int) -> tuple[int, int] | None:
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

THIN_WRAPPER_PATTERN = re.compile(
    r"^\s*return\s+(?P<callee>[A-Za-z_][A-Za-z0-9_]*)\s*\(")

HANDLER_MACRO_PATTERN = re.compile(
    r"^\s*(?P<family>VMBIN_HANDLER_[A-Z0-9_]+)\s*\(\s*"
    r"(?P<handler>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r"(?P<logic>[A-Za-z_][A-Za-z0-9_]*)",
    re.MULTILINE)

FAMILY_TABLE_ROW_PATTERN = re.compile(
    r"^\s*\*\s+(?P<family>VMBIN_HANDLER_[A-Z0-9_]+)\s*\|\s*(?P<operators>.+?)\s*$",
    re.MULTILINE)
FAMILY_TABLE_MARKER = "handler-family-table:"

NON_FAMILY_MACROS = frozenset({
    "VMBIN_HANDLER_LOCATE_READ", "VMBIN_HANDLER_LOCATE_WRITE",
    "VMBIN_HANDLER_REQUIRE", "VMBIN_HANDLER_REQUIRE_ARITY",
    "VMBIN_HANDLER_REQUIRE_WRITABLE", "VMBIN_HANDLER_AS",
    "VMBIN_HANDLER_AS_READ", "VMBIN_HANDLER_LANGTYPE_CAPACITY",
    "VMBIN_HANDLER_KEY_CAPACITY", "VMBIN_HANDLER_CREATE_BUFFER_CAPACITY",
    "VMBIN_HANDLER_CREATE_KEY_CAPACITY",
})

REVIEWED_OVERSIZE = {
    "scalar_handle_debugger": "debugger entry point: inherently multi-branch state machine (see workspace/622 decision)",
    "runtimec_host_pathspace_watch":
        "37 lines, above the 30-line ABI adapter limit: beyond reading key/expected value and "
        "calling Watch, **timeout is not an error** (when ABI returns 0 with out=NULL, the "
        "write operand stays untouched) — a return convention unique to this entry",
    "runtimec_host_pathspace_list_at":
        "42 lines, above the 30-line ABI adapter limit: beyond reading path/index and calling "
        "ListAt, it **writes into a caller buffer** (buffer+capacity ABI rather than returning "
        "a pointer) — a buffer convention unique to this entry",
}

def registered_handlers(source_root: Path) -> set[str]:
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
    temporary = source_root / "runtimec/vmbin/temp_output.c"
    if temporary.is_file():
        yield temporary

def scan_file(path: Path, display: str,
              registered: set[str]) -> list[tuple[str, str, int, int, int, str]]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return []
    code = c_code_without_comments(text)
    limit = limit_for(path)
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
        span = definitions.get(name)
        if span is None:
            return None
        if count_body_lines(code, *span) > 2:
            return None
        thin = THIN_WRAPPER_PATTERN.search(code[span[0]:span[1]])
        return None if thin is None else thin.group("callee")

    shared_users: dict[str, set[str]] = {}
    for match in HANDLER_DEFINITION_PATTERN.finditer(code):
        name = match.group("name")
        if name not in registered:
            continue
        callee = thin_callee(name)
        if callee is None or callee in registered:
            continue
        shared_users.setdefault(callee, set()).add(name)
    shared_callees = {callee for callee, users in shared_users.items()
                      if len(users) >= 2}

    def measure(name: str, seen: frozenset[str]) -> tuple[int, int] | None:
        if name in seen:
            return None
        span = definitions.get(name)
        if span is None:
            return None
        body_start, body_end = span
        lines = count_body_lines(code, body_start, body_end)
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
        if name not in registered:
            continue
        measured = measure(name, frozenset())
        if measured is None:
            continue
        lines, _ = measured
        if lines > limit and name not in REVIEWED_OVERSIZE:
            findings.append((display, name, line_of(match.start()), lines, limit,
                             ""))

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

    for callee in sorted(shared_callees):
        users = shared_users[callee]
        note = f" (shared helper referenced by {len(users)} handlers, counted once)"
        start = definition_starts.get(callee)
        if start is None:
            first_user = sorted(users)[0]
            findings.append((display, callee,
                             line_of(definition_starts.get(first_user, 0)), 0,
                             limit,
                             note + " unmeasurable: definition is outside the "
                             "HANDLER_DEFINITION_PATTERN return-type set "
                             "(runtimec_status_t / int / void)"))
            continue
        lines = count_body_lines(code, *definitions[callee])
        if lines > limit and callee not in REVIEWED_OVERSIZE:
            findings.append((display, callee, line_of(start), lines, limit, note))
    return findings

ENTRY_PATTERN = re.compile(
    r"\{\.operation_identifier\s*=\s*(?P<opid>[^,]+),")
HANDLER_REFERENCE_PATTERN = re.compile(r"\.handler\s*=\s*[A-Za-z_]")

def audit_family_table(source_root: Path) -> tuple[list[str], list[str]]:
    findings: list[str] = []
    notices: list[str] = []
    handlers_root = source_root / HANDLERS_DIRECTORY

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
    import tempfile

    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="runtimec-handler-size-selftest-") as tmp:
        root = Path(tmp)
        handlers = root / HANDLERS_DIRECTORY
        handlers.mkdir(parents=True)
        source = handlers / "scalar.c"
        source.write_text(
            "static inline runtimec_status_t scalar_three(void) {\n"
            "    int a = 1;\n"
            "    int b = 2;\n"
            "    return a + b;\n"
            "}\n"
            "\n"
            "static inline runtimec_status_t scalar_four(void) {\n"
            "    int a = 1;\n"
            "    int b = 2;\n"
            "    int c = 3;\n"
            "    return a + b + c;\n"
            "}\n"
            "\n"
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
        by_name = {name: lines for _, name, _, lines, _, _ in findings}
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

        if "scalar_helper_big" in by_name:
            failures.append(
                "scalar_helper_big is not in the registry but was reported — "
                "the rule is measuring helpers, not handlers")

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

        if not findings:
            failures.append(
                "reverse control failed: with limit 3 nothing reported — rule is dead")

        (handlers / "thin.c").write_text(
            "static runtimec_status_t thin_logic(void) {\n"
            + "".join(f"    int v{i} = {i};\n" for i in range(19))
            + "    return v0;\n}\n"
            "static runtimec_status_t thin_handler(void) {\n"
            "    return thin_logic();\n}\n"
            "static const binrwir_registry_entry_t thin_entries[] = {\n"
            "    {.operation_identifier = 1U, .flags = 0U, .handler = thin_handler},\n"
            "};\n", encoding="utf-8")
        (handlers / "macro.c").write_text(
            "static runtimec_status_t macro_logic(void) {\n"
            + "".join(f"    int m{i} = {i};\n" for i in range(19))
            + "    return m0;\n}\n"
            "VMBIN_HANDLER_UNARY_TYPED(macro_handler, macro_logic, int64, int64)\n"
            "static const binrwir_registry_entry_t macro_entries[] = {\n"
            "    {.operation_identifier = 2U, .flags = 0U, .handler = macro_handler},\n"
            "};\n", encoding="utf-8")
        (handlers / "fine.c").write_text(
            "static runtimec_status_t fine_logic(void) {\n"
            "    return 0;\n}\n"
            "static const binrwir_registry_entry_t fine_entries[] = {\n"
            "    {.operation_identifier = 3U, .flags = 0U, .handler = fine_logic},\n"
            "};\n", encoding="utf-8")

        for stale in ("scalar.c",):
            (handlers / stale).unlink(missing_ok=True)
        extended, _ = audit(root)
        reported = {name for _, name, _, _, _, _ in extended}
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
                continue
            if definition <= DEFAULT_LIMIT:
                failures.append(
                    f"reviewed-oversize zombie: `{allowed}` is still exempted "
                    f"but now only {definition} lines — remove it from the list")

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
              f"(limit {limit}) — requires manual review{note}")
    if findings or table_problems or family_findings:
        print(f"check_handler_size: FAIL ({len(findings)} function(s) exceed "
              f"limit, {len(table_problems)} entry-table problem(s), "
              f"{len(family_findings)} family-table problem(s))")
        return 1
    print(f"check_handler_size: OK ({scanned} handler file(s) scanned)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
