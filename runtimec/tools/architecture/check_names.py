
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

PRODUCT_DIRECTORIES = (
    "include", "interface", "contract", "host", "binrwir", "vmkv",
    "binrwir_compiler", "vmbin", "engine", "generated",
    "specification", "tests",
)
TEXT_SUFFIXES = {".c", ".h", ".cmake", ".in", ".map", ".exports", ".def"}
OUTPUT_FREE_DIRECTORIES = (
    "include", "interface", "contract", "host", "binrwir", "vmkv",
    "binrwir_compiler", "vmbin", "engine", "generated",
)
STDOUT_WRITE_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_])(?:fprintf\s*\(\s*stderr|fprintf\s*\(\s*stdout|"
    r"f?printf\s*\(|puts\s*\(|putchar\s*\()")
DEBUG_MARKER_PATTERN = re.compile(r'"(?:[^"\\]|\\.)*\b(?:DBG|TODO|XXX|FIXME)\b')
FORBIDDEN_PATH_PARTS = {
    "abi": "execution_interface", "api": "interface", "compat": "delete",
    "compatibility": "delete", "docs": "documentation", "legacy": "delete",
    "spec": "specification",
}
FORBIDDEN_NAMES = (
    ("removed-jit-component-name", re.compile(r"vmjit", re.IGNORECASE)),
    ("removed-compiler-target", re.compile(r"runtimec_compiler")),
    ("standalone-jit-target", re.compile(r"runtimec_jit")),
    ("removed-jit-target", re.compile(r"runtimec_vmjit")),
    ("removed-jit-option", re.compile(r"RUNTIMEC_ENABLE_VMJIT")),
    ("removed-jit-configuration", re.compile(r"enable_vmjit")),
    ("old-binir-name", re.compile(r"\bbinir\b", re.IGNORECASE)),
    ("old-binir-identifier", re.compile(r"\bbinir_[A-Za-z0-9_]*\b")),
    ("old-runtimec2-name", re.compile(r"\bruntimec2\b")),
    ("old-runtime-tree", re.compile(r"(?:^|[\"<])runtime/")),
    ("old-success-name", re.compile(r"\bRUNTIMEC_STATUS_OK\b")),
    ("removed-fallback-status", re.compile(r"\bRUNTIMEC_STATUS_FALLBACK\b")),
    ("removed-jit-execution-entry", re.compile(r"\bbinrwir_jit_try_execute\b")),
    ("removed-runtime-deoptimization", re.compile(
        r"\bBINRWIR_EXECUTION_EXIT_DEOPTIMIZATION\b",
    )),
    ("removed-runtime-retry", re.compile(
        r"\bBINRWIR_EXECUTION_EXIT_FLAG_RETRY_LOGICAL_PROGRAM_COUNTER\b",
    )),
    ("removed-static-body-address-resolver", re.compile(
        r"\bresolve_static_value_body_address\b",
    )),
    ("removed-public-body-address-derivation", re.compile(
        r"\bruntimec_xvalue_address_to_value_body_address\b",
    )),
    ("alternate-jit-compilation-cache", re.compile(
        r"\b(?:binrwir_jit_code_cache|binrwir_jit_cache_key|"
        r"binrwir_jit_code_cache_get_or_compile)\b",
    )),
    ("binrwir-owned-plan-builder", re.compile(
        r"\bbinrwir_execution_plan_build\b",
    )),
    ("second-jit-plan", re.compile(r"\bbinrwir_jit_plan_[A-Za-z0-9_]*\b")),
    ("unobservable-compilation-state", re.compile(
        r"\bBINRWIR_COMPILATION_STATE_[A-Za-z0-9_]*\b",
    )),
    ("execution-time-fallback", re.compile(r"fallback", re.IGNORECASE)),
    ("binrwir-under-vmbin", re.compile(
        r"(?:^|/)vmbin/(?:value|reference|signature|instruction|program|execution_plan)"
        r"\.(?:c|h)$",
        re.IGNORECASE,
    )),
    ("compatibility-language", re.compile(
        r"\b(?:backward|compat|compatibility|legacy|forwarding)[ _-]?(?:header|layer|path|support)?\b",
        re.IGNORECASE,
    )),
    ("third-fast-kernel", re.compile(r"\bfast[ _-]?kernel\b", re.IGNORECASE)),
    ("third-executor", re.compile(
        r"\b(?:fast|quick|direct|native|optimized)[ _-]?(?:executor|interpreter|execute|runner)\b",
        re.IGNORECASE,
    )),
    ("old-fast-operation", re.compile(r"\bfast[ _-]?op(?:eration)?\b", re.IGNORECASE)),
    ("alternate-execution-window", re.compile(r"\bexecute[ _-]?window\b", re.IGNORECASE)),
    ("old-vmkv-type", re.compile(r"\bVmkv\b")),
    ("old-vmbin-type", re.compile(r"\bVmBin\b")),
    ("ambiguous-segment-type", re.compile(r"\b(?:Segment|CodeSeg|CInsn)\b")),
    ("old-native-context", re.compile(r"\b(?:NativeCtx|NativeFn)\b")),
    ("old-frame-type", re.compile(r"\b(?:FrameCtx|RunResult)\b")),
    ("removed-unobservable-memory-frame", re.compile(
        r"\bmem[ _-]?frame\b", re.IGNORECASE,
    )),
    ("removed-segment-may-stop", re.compile(
        r"\bsegment[ _-]?may[ _-]?stop\b", re.IGNORECASE,
    )),
    ("old-runtimec2-macro", re.compile(r"\b(?:RC2|KV2)_[A-Za-z0-9_]*\b")),
    ("old-runtimec2-test-name", re.compile(r"\bc2_[A-Za-z0-9_]*\b")),
    ("old-codeseg-name", re.compile(r"\bcodeseg\b", re.IGNORECASE)),
)
ABBREVIATIONS = {
    "abi": "binary_interface or interface", "api": "interface",
    "arg": "argument", "buf": "buffer", "cfg": "control_flow_graph",
    "ctx": "context", "desc": "descriptor", "dst": "destination",
    "fn": "function", "id": "identifier", "impl": "implementation",
    "len": "length", "op": "operation",
    "ptr": "pointer", "ref": "reference", "req": "request",
    "src": "source", "tmp": "temporary",
}
ABBREVIATION_PATTERNS = tuple(
    (abbreviation, replacement, re.compile(
        rf"(?<![A-Za-z0-9]){re.escape(abbreviation)}(?![A-Za-z0-9])",
        re.IGNORECASE,
    ))
    for abbreviation, replacement in ABBREVIATIONS.items()
)
C_NON_CODE_PATTERN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
VOID_LIFECYCLE_PATTERN = re.compile(
    r"\bvoid\s+([A-Za-z_][A-Za-z0-9_]*(?:destroy|close)[A-Za-z0-9_]*)"
    r"\s*\([^;{}]*\)\s*(?:;|\{)",
    re.IGNORECASE | re.DOTALL,
)
EXTERNAL_VOID_LIFECYCLE_PATTERN = re.compile(
    r"\bextern\s+void\s+(pathspace[A-Za-z0-9_]*(?:destroy|close)[A-Za-z0-9_]*)"
    r"\s*\([^;{}]*\)\s*;",
    re.IGNORECASE | re.DOTALL,
)

@dataclass(frozen=True, order=True)
class Finding:
    path: str
    line: int
    rule: str
    token: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: [{self.rule}] forbidden name {self.token!r}"

def parse_arguments(command_arguments: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root", type=Path,
        default=Path(__file__).resolve().parents[3],
        help="repository source root, or the runtimec directory itself",
    )
    parser.add_argument("--quiet", action="store_true", help="suppress success output")
    parser.add_argument("--selftest", action="store_true",
                        help="prove every judgment can fire, using synthetic fixtures")
    return parser.parse_args(command_arguments)

def locate_runtimec(source_root: Path) -> tuple[Path, Path]:
    source_root = source_root.resolve()
    return ((source_root.parent, source_root) if source_root.name == "runtimec"
            else (source_root, source_root / "runtimec"))

def relative_display(path: Path, repository_root: Path) -> str:
    try:
        return path.resolve().relative_to(repository_root).as_posix()
    except ValueError:
        return path.as_posix()

def iter_product_files(runtimec_root: Path):
    cmake_lists = runtimec_root / "CMakeLists.txt"
    if cmake_lists.is_file():
        yield cmake_lists
    for directory_name in PRODUCT_DIRECTORIES:
        directory = runtimec_root / directory_name
        if directory.is_dir():
            for path in sorted(directory.rglob("*")):
                if path.is_file() and (
                    path.suffix in TEXT_SUFFIXES or path.name == "CMakeLists.txt"
                ):
                    yield path

def audit_public_headers(runtimec_root: Path, repository_root: Path) -> list[Finding]:
    include_root = runtimec_root / "include"
    allowed_header = include_root / "pathlang" / "runtimec.h"
    return [
        Finding(relative_display(header, repository_root), 1,
                "noncanonical-public-header",
                header.relative_to(include_root).as_posix())
        for header in sorted(include_root.rglob("*.h"))
        if header != allowed_header
    ] if include_root.is_dir() else []

def c_code_without_comments_or_literals(text: str) -> str:
    return C_NON_CODE_PATTERN.sub(
        lambda match: re.sub(r"[^\n]", " ", match.group(0)), text,
    )

C_COMMENTS_ONLY_PATTERN = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)

def c_code_without_comments(text: str) -> str:
    return C_COMMENTS_ONLY_PATTERN.sub(
        lambda match: re.sub(r"[^\n]", " ", match.group(0)), text,
    )

MEMBER_ACCESS_PATTERN = re.compile(r"(?:->|\.)\s*([A-Za-z_][A-Za-z0-9_]*)")
OFFSETOF_FIELD_PATTERN = re.compile(r"\boffsetof\s*\([^,()]*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)")

FOREIGN_ABI_IDENTIFIERS = (
    "PATHSPACE_XVALUE_HEAD_LEN", "PATHSPACE_XVALUE_PTR_INLINE",
    "PATHSPACE_XVALUE_PTR_PTR",
    "PATHSPACE_REF_INLINE", "PATHSPACE_REF_PTR", "PATHSPACE_REF_EXT",
)

def mask_foreign_abi_positions(code: str) -> str:
    def blank(match: re.Match[str]) -> str:
        start, end = match.span(1)
        return match.group(0)[:start - match.start()] + " " * (end - start) + \
            match.group(0)[end - match.start():]
    masked = OFFSETOF_FIELD_PATTERN.sub(blank, code)
    masked = MEMBER_ACCESS_PATTERN.sub(blank, masked)
    for identifier in FOREIGN_ABI_IDENTIFIERS:
        masked = masked.replace(identifier, " " * len(identifier))
    return masked

def audit_output_channels(runtimec_root: Path,
                          repository_root: Path) -> list[Finding]:
    findings: list[Finding] = []
    for directory_name in OUTPUT_FREE_DIRECTORIES:
        directory = runtimec_root / directory_name
        if not directory.is_dir():
            continue
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.suffix not in {".c", ".h"}:
                continue
            display = relative_display(path, repository_root)
            try:
                text = path.read_text(encoding="utf-8")
            except (OSError, UnicodeError):
                continue
            code = c_code_without_comments_or_literals(text)
            code_without_comments = c_code_without_comments(text)
            for match in STDOUT_WRITE_PATTERN.finditer(code):
                findings.append(Finding(
                    display, code.count("\n", 0, match.start()) + 1,
                    "stdout-write-in-product-source",
                    "the shared library has no stdout; report diagnostics through the "
                    "error object",
                ))
            for match in DEBUG_MARKER_PATTERN.finditer(code_without_comments):
                findings.append(Finding(
                    display,
                    code_without_comments.count("\n", 0, match.start()) + 1,
                    "debug-marker-in-product-source",
                    "a debug marker left in a product string literal",
                ))
    return findings

def audit(repository_root: Path, runtimec_root: Path) -> tuple[list[Finding], int]:
    findings = audit_public_headers(runtimec_root, repository_root)
    findings.extend(audit_output_channels(runtimec_root, repository_root))
    scanned = 0
    forbidden_directories = {
        "api": "interface",
        "compat": "delete",
        "compiler": "binrwir_compiler",
        "docs": "documentation",
        "jit": "binrwir_compiler/jit",
        "spec": "specification",
        "vmjit": "binrwir_compiler/jit",
    }
    for forbidden_directory, replacement in forbidden_directories.items():
        path = runtimec_root / forbidden_directory
        if path.exists():
            findings.append(Finding(relative_display(path, repository_root), 1,
                                    "forbidden-directory",
                                    f"{forbidden_directory} -> {replacement}"))

    for path in iter_product_files(runtimec_root):
        scanned += 1
        display = relative_display(path, repository_root)
        relative_path = path.relative_to(runtimec_root)
        for part in relative_path.parts:
            replacement = FORBIDDEN_PATH_PARTS.get(part.lower())
            if replacement is not None:
                findings.append(Finding(display, 1, "forbidden-path-name",
                                        f"{part} -> {replacement}"))
        for rule, pattern in FORBIDDEN_NAMES:
            match = pattern.search(relative_path.as_posix())
            if match:
                findings.append(Finding(display, 1, f"{rule}-in-path", match.group(0)))
        try:
            text = path.read_text(encoding="utf-8")
            lines = text.splitlines()
        except (OSError, UnicodeError) as error:
            findings.append(Finding(display, 1, "read-error", str(error)))
            continue
        if path.suffix in {".c", ".h"}:
            code = c_code_without_comments_or_literals(text)
            exempt_spans = [
                (match.start(), match.end())
                for match in EXTERNAL_VOID_LIFECYCLE_PATTERN.finditer(code)
            ]
            for match in VOID_LIFECYCLE_PATTERN.finditer(code):
                if any(start <= match.start() and match.end() <= end
                       for start, end in exempt_spans):
                    continue
                findings.append(Finding(
                    display,
                    code.count("\n", 0, match.start()) + 1,
                    "void-lifecycle-function",
                    match.group(1),
                ))
        masked = c_code_without_comments_or_literals(text).splitlines()
        for line_number, line in enumerate(lines, 1):
            masked_line = masked[line_number - 1] if line_number <= len(masked) else ""
            for rule, pattern in FORBIDDEN_NAMES:
                for match in pattern.finditer(masked_line):
                    findings.append(Finding(display, line_number, rule, match.group(0)))
            if path.name == "CMakeLists.txt":
                continue
            abbreviation_line = mask_foreign_abi_positions(masked_line)
            for abbreviation, replacement, pattern in ABBREVIATION_PATTERNS:
                for match in pattern.finditer(abbreviation_line):
                    findings.append(Finding(
                        display, line_number,
                        f"abbreviation-{abbreviation}-use-{replacement}", match.group(0),
                    ))
    return sorted(set(findings)), scanned

SELFTEST_CASES = (
    ("foreign-member-access", "static void probe(const pathspaceXvalueHead *head) {\n"
                              "    int value = head->ptr;\n"
                              "    int other = head.ptr;\n"
                              "}\n", ()),
    ("foreign-offsetof-field", "static int probe(void) {\n"
                               "    return (int)offsetof(pathspaceXvalueHead, ptr);\n"
                               "}\n", ()),
    ("local-variable", "static int ptr;\n", ("abbreviation-ptr-use-pointer",)),
    ("parameter", "static int probe(int ptr) { return ptr; }\n",
     ("abbreviation-ptr-use-pointer",)),
    ("type-alias", "typedef int ptr;\n", ("abbreviation-ptr-use-pointer",)),
    ("foreign-macro", "static unsigned probe(void) {\n"
                      "    return (unsigned)PATHSPACE_XVALUE_PTR_INLINE;\n"
                      "}\n", ()),
    ("own-uppercase-abbreviation", "#define OUR_PTR_LIMIT 4\n",
     ("abbreviation-ptr-use-pointer",)),
    ("external-void-lifecycle-is-allowed",
     "extern void pathspaceClose(void *handle);\n", ()),
    ("local-void-lifecycle-is-still-rejected",
     "static void probe_close(void *handle) { (void)handle; }\n",
     ("void-lifecycle-function",)),
    ("extern-without-upstream-prefix-is-rejected",
     "extern void local_close(void *handle);\n",
     ("void-lifecycle-function",)),
    ("upstream-name-without-extern-is-rejected",
     "void pathspaceClose(void *handle);\n",
     ("void-lifecycle-function",)),
    ("stdout-write", "static void probe(void) {\n"
                     '    fprintf(stderr, "boom");\n'
                     "}\n", ("stdout-write-in-product-source",)),
    ("stdout-write-plain-printf", 'printf("hello");\n',
     ("stdout-write-in-product-source",)),
    ("snprintf-is-not-stdout", "static int probe(char *b, int n) {\n"
                               '    return snprintf(b, n, "%d", 1);\n'
                               "}\n", ()),
    ("debug-marker", 'static const char *message = "DBG reject";\n',
     ("debug-marker-in-product-source",)),
    ("todo-in-comment-is-fine", "/* TODO: handle later */\nstatic int probe(void) { return 0; }\n",
     ()),
    ("todo-in-comment-across-includes-is-fine",
     '#include "contract/status.h"\n'
     '#include "pathspace/pathspace.h"\n'
     "\n"
     "/*\n"
     " * Deferred (future standalone capability): reference resolution for dynamic keys. TODO noted here.\n"
     " */\n"
     "static int probe(void) { return 0; }\n",
     ()),
    ("todo-in-string-is-reported",
     'static const char *message = "TODO remove before ship";\n',
     ("debug-marker-in-product-source",)),
    ("mixed-member-and-local",
     "static int ptr;\n"
     "static void probe(const pathspaceXvalueHead *head) { (void)head->ptr; }\n",
     ("abbreviation-ptr-use-pointer",)),
)

def run_selftest() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="runtimec-names-selftest-") as directory:
        root = Path(directory)
        runtimec_root = root / "runtimec"
        source = runtimec_root / "contract" / "probe.c"
        source.parent.mkdir(parents=True)
        for name, body, expected in SELFTEST_CASES:
            source.write_text(body, encoding="utf-8")
            findings, _ = audit(root, runtimec_root)
            rules = {finding.rule for finding in findings}
            if rules != set(expected):
                failures.append(f"{name}: expected {sorted(expected)}, got {sorted(rules)}")
    for failure in failures:
        print(f"[selftest] {failure}")
    if failures:
        print(f"check_names selftest: FAIL ({len(failures)} failure(s))")
        return 1
    print(f"check_names selftest: OK ({len(SELFTEST_CASES)} fixtures)")
    return 0

def main(command_arguments: list[str] | None = None) -> int:
    arguments = parse_arguments(command_arguments or sys.argv[1:])
    if arguments.selftest:
        return run_selftest()
    repository_root, runtimec_root = locate_runtimec(arguments.source_root)
    if not runtimec_root.is_dir():
        print(f"{runtimec_root}:1: [missing-runtimec] runtimec source directory does not exist")
        return 2
    findings, scanned = audit(repository_root, runtimec_root)
    for finding in findings:
        print(finding.render())
    if findings:
        print(f"check_names: FAIL ({len(findings)} finding(s), {scanned} file(s) scanned)")
        return 1
    if not arguments.quiet:
        print(f"check_names: OK ({scanned} file(s) scanned)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
