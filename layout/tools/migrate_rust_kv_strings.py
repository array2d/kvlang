#!/usr/bin/env python3
"""把 Rust 测试文件里**字符串字面量中的** kv 源码迁移成新参数语法。

用法：
    migrate_rust_kv_strings.py --dry-run <文件.rs…>     # 只打 diff，不写
    migrate_rust_kv_strings.py --write <文件.rs…>       # 就地写

与 `migrate_param_syntax.py` 的区别只有一个：输入形态。

Rust 测试把 kv 源码写成字符串字面量：

    compile(&mut kv, "lib p {\\n\\trwfunc f(x:int64) -> (r:int64) { x -> r }\\n}\\n")

本工具按**字符串边界**切割，只改 `"..."` 与 `r#"..."#` 的内部；Rust 代码本体、
注释、`env!(...)` 一律不碰。

## 三条跳过规则（不满足就原样保留）

1. **整文件排除**：`EXCLUDED_FILES` 里的文件不参与迁移。语法形态测试的字符串是**样本**，
   不是待迁移的代码——改了就把负向测试变成假绿。
2. **函数级幂等**：一个 rwfunc 的签名已经是全 `*T $name` / `T $name` 形态 → **整块跳过**，
   含函数体。这样刻意写的 `p·x`（负向样本）永远不会被碰。
3. **空 kindexpr**：`f(a:)` 这种形参没有类型，新写法尚未定义。**跳过该函数并报告**，
   不发明语法。

## 迁移规则本身

复用 `migrate_param_syntax.convert_text`，不复制实现。
"""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from migrate_param_syntax import convert_text  # noqa: E402

# 语法形态测试：字符串是**样本**，不是待迁移代码。改了会把负向测试变成假绿。
EXCLUDED_FILES = {
    "wave0_parameter_syntax.rs",
}

# `\` 在 Rust 字符串里先行处理：行尾 `\` 是**续行**（吞掉换行与后续缩进），
# 不是字面反斜杠。不识别它就会把续行变成 `\\` + 换行，kv 源码里多一个反斜杠。
_RUST_ESCAPES = {
    "n": "\n",
    "t": "\t",
    "r": "\r",
    "0": "\0",
    '"': '"',
    "\\": "\\",
}

SIGNATURE = __import__("migrate_param_syntax").SIGNATURE  # 同一份签名正则


def unescape(raw: str) -> str:
    """`\\n` `\\t` `\\"` 与行尾续行 `\\<newline>` → 真实字符。

    行尾 `\\` 后跟换行时，Rust 会吞掉换行与下一行前导空白；这里照做，
    否则迁移器会把它当成字面反斜杠再转义成 `\\\\`。
    """
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
            # 续行：吞掉换行与下一行前导空白。
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
    """真实字符 → Rust 字面量形态。"""
    return (
        text.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\t", "\\t")
        .replace("\r", "\\r")
        .replace("\0", "\\0")
    )


def string_spans(source: str) -> list[tuple[int, int, bool]]:
    """`("..." / r#"..."#)` 的位置 → (内容起点, 内容终点, 是否原始串)。"""
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
    """签名是否已是全 `*T $name` / `T $name` 形态。"""
    from migrate_param_syntax import parse_item, split_params

    items = split_params(params) + split_params(returns)
    if not items:
        return False
    return all("$" in item for item in items)


def has_empty_kindexpr(params: str, returns: str) -> bool:
    """是否有空 kindexpr 形参（`f(a:)`）——新写法未定义，跳过不猜。"""
    from migrate_param_syntax import parse_item, split_params

    for item in split_params(params) + split_params(returns):
        if "$" in item:
            continue
        if ":" in item:
            _, ty = item.split(":", 1)
            if not ty.strip():
                return True
    return False


def migrate_kv(kv: str) -> tuple[str, list[str], list[str]]:
    """迁移一段 kv 源码。返回 (结果, 跳过清单, 冲突清单)。

    逐函数判定：已新形态 / 空 kindexpr 的函数整块原样保留（含函数体）。
    """
    migrated, conflicts = convert_text(kv)
    skipped: list[str] = []
    for match in SIGNATURE.finditer(kv):
        name, params, returns = match.group(2), match.group(3), match.group(4)
        if already_migrated(params, returns):
            skipped.append(f"{name}: 已是新形态")
        elif has_empty_kindexpr(params, returns):
            skipped.append(f"{name}: 空 kindexpr 未定义")
    if not skipped:
        return migrated, skipped, conflicts
    return _assemble(kv, migrated, skipped), skipped, conflicts


def _assemble(original: str, migrated: str, skipped: list[str]) -> str:
    """取 migrated 为基底，把被跳过的函数块换回原文。

    `migrated` 与 `original` 长度不同，按签名块下标对齐。
    """
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
    """迁移一份 Rust 源码。返回 (新源码, 跳过清单, 冲突清单)。"""
    if Path(path).name in EXCLUDED_FILES:
        return source, [f"{Path(path).name}: 整文件排除（语法形态样本）"], []

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
                print(f"CONFLICT {path}: {conflict} —— 局部变量与指针形参同名，手工处理",
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
