
from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from migrate_param_syntax import convert_text

EXCLUDED_FILES = {
    "wave0_parameter_syntax.rs",
}

_RUST_ESCAPES = {
    "n": "\n",
    "t": "\t",
    "r": "\r",
    "0": "\0",
    '"': '"',
    "\\": "\\",
}

SIGNATURE = __import__("migrate_param_syntax").SIGNATURE

def unescape(raw: str) -> str:
    out: list[str] = []
    index = 0
    while index < len(raw):
        ch = raw[index]
        if ch != "\\" or index + 1 >= len(raw):
            out.append(ch)
            index += 1
            continue
        nxt = raw[index + 1]
        if nxt == "\n":
            index += 2
            while index < len(raw) and raw[index] in " \t":
                index += 1
            continue
        if nxt in _RUST_ESCAPES:
            out.append(_RUST_ESCAPES[nxt])
            index += 2
            continue
        out.append(ch)
        index += 1
    return "".join(out)

def escape(text: str) -> str:
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\t", "\\t")
        .replace("\r", "\\r")
        .replace("\0", "\\0")
    )

def string_spans(source: str) -> list[tuple[int, int, bool]]:
    spans: list[tuple[int, int, bool]] = []
    index = 0
    length = len(source)
    while index < length:
        if source.startswith("//", index):
            newline = source.find("\n", index)
            index = length if newline < 0 else newline + 1
            continue
        if source.startswith('r#"', index):
            start = index + 3
            end = source.find('"#', start)
            if end < 0:
                break
            spans.append((start, end, True))
            index = end + 2
            continue
        if source.startswith('r"', index):
            start = index + 2
            end = source.find('"', start)
            if end < 0:
                break
            spans.append((start, end, True))
            index = end + 1
            continue
        if source[index] == '"':
            start = index + 1
            cursor = start
            while cursor < length:
                if source[cursor] == "\\":
                    cursor += 2
                    continue
                if source[cursor] == '"':
                    break
                cursor += 1
            spans.append((start, cursor, False))
            index = cursor + 1
            continue
        index += 1
    return spans

def already_migrated(params: str, returns: str) -> bool:
    from migrate_param_syntax import parse_item, split_params

    items = split_params(params) + split_params(returns)
    if not items:
        return False
    return all("$" in item for item in items)

def migrate_kv(kv: str) -> tuple[str, list[str], list[str]]:
    migrated, conflicts = convert_text(kv)
    skipped: list[str] = []
    for match in SIGNATURE.finditer(kv):
        name, params, returns = match.group(2), match.group(3), match.group(4)
        if already_migrated(params, returns):
            skipped.append(f"{name}: already in new syntax")
    if not skipped:
        return migrated, skipped, conflicts
    return _assemble(kv, migrated, skipped), skipped, conflicts

def _assemble(original: str, migrated: str, skipped: list[str]) -> str:
    if not skipped:
        return migrated
    orig_blocks = list(SIGNATURE.finditer(original))
    mig_blocks = list(SIGNATURE.finditer(migrated))
    if len(orig_blocks) != len(mig_blocks):
        return migrated
    pieces: list[str] = []
    pos_mig = 0
    pos_orig = 0
    skip_names = {entry.split(":")[0] for entry in skipped}
    for orig_match, mig_match in zip(orig_blocks, mig_blocks):
        name = orig_match.group(2)
        pieces.append(migrated[pos_mig:mig_match.start()])
        if name in skip_names:
            pieces.append(original[orig_match.start():orig_match.end()])
        else:
            pieces.append(migrated[mig_match.start():mig_match.end()])
        pos_mig = mig_match.end()
        pos_orig = orig_match.end()
    pieces.append(migrated[pos_mig:])
    return "".join(pieces)

def convert_source(source: str, path: str) -> tuple[str, list[str], list[str]]:
    if Path(path).name in EXCLUDED_FILES:
        return source, [f"{Path(path).name}: whole file excluded (syntax specimen)"], []

    spans = string_spans(source)
    pieces: list[str] = []
    skipped: list[str] = []
    conflicts: list[str] = []
    pos = 0
    for start, end, raw in spans:
        pieces.append(source[pos:start])
        body = source[start:end]
        kv = body if raw else unescape(body)
        if "rwfunc" not in kv and "rwir" not in kv:
            pieces.append(body)
            pos = end
            continue
        migrated, kv_skipped, kv_conflicts = migrate_kv(kv)
        skipped.extend(kv_skipped)
        conflicts.extend(kv_conflicts)
        if kv_conflicts or migrated == kv:
            pieces.append(body)
        else:
            pieces.append(migrated if raw else escape(migrated))
        pos = end
    pieces.append(source[pos:])
    return "".join(pieces), skipped, conflicts

def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--dry-run", action="store_true")
    group.add_argument("--write", action="store_true")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    failed = False
    for path in args.files:
        original = Path(path).read_text(encoding="utf-8")
        migrated, skipped, conflicts = convert_source(original, path)
        if conflicts:
            failed = True
            for conflict in conflicts:
                print(f"CONFLICT {path}: {conflict} — local variable collides with pointer parameter; handle manually",
                      file=sys.stderr)
            continue
        for entry in skipped:
            print(f"SKIP {path}: {entry}", file=sys.stderr)
        if migrated == original:
            continue
        if args.dry_run:
            sys.stdout.writelines(difflib.unified_diff(
                original.splitlines(True),
                migrated.splitlines(True),
                fromfile=path,
                tofile=path + " (migrated)",
            ))
        else:
            Path(path).write_text(migrated, encoding="utf-8")
            print(f"migrated: {path}", file=sys.stderr)
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
