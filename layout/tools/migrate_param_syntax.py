
from __future__ import annotations

import argparse
import difflib
import re
import sys

SIGNATURE = re.compile(
    r"\b(rwfunc|rwir)\s+([^\s(]+)\s*\(([^)]*)\)\s*->\s*\(([^)]*)\)"
)
COMMENT = re.compile(r"//[^\n]*")
STRING = re.compile(r'r#"[\s\S]*?"#|r"[\s\S]*?"|"[^"\n]*"')

def split_params(raw: str) -> list[str]:
    items: list[str] = []
    depth = 0
    cur: list[str] = []
    for ch in raw:
        if ch in "[(":
            depth += 1
        elif ch in "])":
            depth -= 1
        if ch == "," and depth == 0:
            items.append("".join(cur))
            cur = []
            continue
        cur.append(ch)
    if cur:
        items.append("".join(cur))
    return [item for item in items if item.strip()]

def parse_item(item: str) -> tuple[str, str, str]:
    if "$" in item:
        name = item.rsplit("$", 1)[1].strip()
        ty = item.rsplit("$", 1)[0].strip()
        return name, ty, item
    if ":" in item:
        name, ty = item.split(":", 1)
        return name.strip(), ty.strip(), item
    return item.strip(), "", item

def convert_signature_item(item: str) -> str:
    name, ty, original = parse_item(item)
    if "$" in item:
        return original.lstrip()
    if not ty:
        raise ValueError(f"参数/返回值 {name!r} 没有类型：现行 spec 禁止空 kindexpr，需人工处理")
    if ty.startswith("*"):
        return f"*{ty} ${name}"
    if ty.startswith("@"):
        return f"{ty} ${name}"
    if "|" in ty and not (ty.startswith("(") and ty.endswith(")")):
        return f"*({ty}) ${name}"
    return f"*{ty} ${name}"

def convert_params(raw: str) -> str:
    out: list[str] = []
    for index, item in enumerate(split_params(raw)):
        leading = item[: len(item) - len(item.lstrip())]
        prefix = "" if index == 0 else ","
        out.append(prefix + leading + convert_signature_item(item))
    return "".join(out)

def body_span(text: str, start: int) -> tuple[int, int]:
    i = start
    while i < len(text) and text[i].isspace():
        i += 1
    if i >= len(text) or text[i] != "{":
        return start, start
    depth = 0
    while i < len(text):
        m = STRING.match(text, i) or COMMENT.match(text, i)
        if m:
            i = m.end()
            continue
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i + 1
        i += 1
    return start, len(text)

def mask_noncode(body: str) -> str:
    masked = list(body)

    def blank(m: re.Match[str]) -> str:
        for k in range(m.start(), m.end()):
            if masked[k] != "\n":
                masked[k] = " "
        return m.group(0)

    for pattern in (STRING, COMMENT):
        for m in pattern.finditer(body):
            blank(m)
    return "".join(masked)

def find_local_collisions(masked: str, pointer_names: set[str]) -> set[str]:
    collisions: set[str] = set()
    for name in pointer_names:
        pattern = re.compile(rf"(?<![*\w.]){re.escape(name)}\s*:[^=\n]*=(?!=)")
        if pattern.search(masked):
            collisions.add(name)
    return collisions

def rewrite_body(body: str, pointer_names: set[str]) -> str:
    masked = mask_noncode(body)
    edits: list[tuple[int, int, str]] = []
    for name in pointer_names:
        pattern = re.compile(rf"(?<![*\w.$·]){re.escape(name)}\b")
        for m in pattern.finditer(masked):
            tail = masked[m.end():].lstrip()
            if tail.startswith(("== None", "!= None")):
                continue
            bracketed = tail[:1] in ("·", "[")
            replacement = f"(*{name})" if bracketed else f"*{name}"
            edits.append((m.start(), m.end(), replacement))
    for start, end, replacement in sorted(edits, reverse=True):
        body = body[:start] + replacement + body[end:]
    return body

def convert_text(text: str) -> tuple[str, list[str]]:
    out: list[str] = []
    pos = 0
    conflicts: list[str] = []
    for match in SIGNATURE.finditer(text):
        keyword, fname, params, returns = match.groups()
        try:
            signature = f"{keyword} {fname}({convert_params(params)}) -> ({convert_params(returns)})"
        except ValueError as error:
            conflicts.append(f"{fname}: {error}")
            continue
        out.append(text[pos:match.start()])
        out.append(signature)

        pointer_names = {
            parse_item(item)[0]
            for item in split_params(params) + split_params(returns)
            if parse_item(item)[0]
        }
        _, body_end = body_span(text, match.end())
        body = text[match.end():body_end]
        if signature == text[match.start():match.end()]:
            out.append(body)
            pos = body_end
            continue
        if pointer_names and body:
            masked = mask_noncode(body)
            collisions = find_local_collisions(masked, pointer_names)
            if collisions:
                conflicts.append(
                    f"{fname}: {', '.join(sorted(collisions))} —— 局部变量与指针形参同名，手工处理"
                )
                out.append(body)
            else:
                out.append(rewrite_body(body, pointer_names))
        else:
            out.append(body)
        pos = body_end
    out.append(text[pos:])
    return "".join(out), conflicts

def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--dry-run", action="store_true")
    group.add_argument("--write", action="store_true")
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    failed = False
    for path in args.files:
        original = open(path, encoding="utf-8").read()
        migrated, conflicts = convert_text(original)
        if conflicts:
            failed = True
            for conflict in conflicts:
                print(f"CONFLICT {path}: {conflict}",
                      file=sys.stderr)
            continue
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
            open(path, "w", encoding="utf-8").write(migrated)
            print(f"migrated: {path}", file=sys.stderr)
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
