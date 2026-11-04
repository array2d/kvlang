#!/usr/bin/env python3
"""把 `name: type` 参数声明迁移成 `*T $name` / `T $name`，并改写**指针形参**的体内引用。

用法：
    migrate_param_syntax.py --dry-run <文件…>     # 只打 diff，不写
    migrate_param_syntax.py --write <文件…>       # 就地写

两条规则（spec 04-layout语义/03-函数、05-runtime语义/04-函数调用）：

1. **签名**：`(a: int64, b: *[]int64)` → `(int64 $a, *[]int64 $b)`
2. **体内**：新模型下形参名是**指针**。带 `*` 的形参，每一处裸引用都要显式解引用：

   | 上下文 | 改写为 |
   |---|---|
   | 后面紧跟 `·` 或 `[` | `(*name)`（基座要加括号，`*arr[i]` 是 `*(arr[i])`，错） |
   | 其余（实参、右值、写槽） | `*name` |
   | 已经是 `*name` | 跳过（幂等） |
   | 字符串字面量 / 注释内 | 跳过 |

   **值复制形参（不带 `*`）一行都不改**——体内裸名本来就是读值。

失败封闭：局部变量与指针形参同名 → 报错停下并指认 `文件:函数:名字`。
静默跳过会改错语义，报错才能让人去看（spec 禁止同名，迁移期若出现必须人工处理）。
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys

SIGNATURE = re.compile(
    r"\b(rwfunc|rwir)\s+([^\s(]+)\s*\(([^)]*)\)\s*->\s*\(([^)]*)\)"
)
# 函数体：签名后第一个 `{` 到配对的 `}`。
COMMENT = re.compile(r"//[^\n]*")
STRING = re.compile(r'r#"[\s\S]*?"#|r"[\s\S]*?"|"[^"\n]*"')


def split_params(raw: str) -> list[str]:
    """按顶层逗号切分参数；`[]`/`()` 内的逗号不切。"""
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
    """→ (名字, 类型, 旧形态原文)。`$` 已存在时原样返回类型与名字。"""
    if "$" in item:
        name = item.rsplit("$", 1)[1].strip()
        ty = item.rsplit("$", 1)[0].strip()
        return name, ty, item
    if ":" in item:
        name, ty = item.split(":", 1)
        return name.strip(), ty.strip(), item
    return item.strip(), "", item


def convert_signature_item(item: str) -> str:
    """迁移一律产出 `*T $name`。

    **值复制不参与迁移**：旧模型里 `a:[]char/utf32` 是引用（调用方传 key 路径），
    改成值复制等于悄悄换掉绑定语义——少了别名、多一次拷贝；而且值复制参数只读，
    体内当累加器写会立刻非法。值复制是给新代码用的能力，不是迁移的落点。

    空 kindexpr（`a:`）的唯一合法新形态是**显式指针** `* $name`：值复制没有类型
    就没有值可拷贝，`$name` 是未定义形态（spec 03-类型系统/07-文法与合法性:61）。

    并集要加括号：`*` 作用于**整个类型表达式**，`*A|B` 会被读成 `(*A)|B`。
    逐成员写 `*A|*B` 非法（同一参数两种传递方式），所以 union 一律产出 `*(A|B)`。

    **第三种旧形态** `name:*T`（`*` 写在冒号后）：旧模型里它是「按引用绑定的、
    值类型本身是 `*T` 的参数」。新写法的传递方式是 `*`、值类型是 `*T`，合起来
    必须写 `**T $name`——**不是** `*T $name`。少写一层，写侧就会把指针当值。
    """
    name, ty, _ = parse_item(item)
    if not ty:
        return f"* ${name}"
    # 旧形态 `name:*T`：`*` 属于**值类型**，不是传递方式；再叠一层传递方式 `*`。
    if ty.startswith("*"):
        return f"*{ty} ${name}"
    if ty.startswith("@"):
        return f"{ty} ${name}"
    # `|` 在类型表达式里优先级最低，`*` 加在串首只约束第一个成员，必须加括号。
    if "|" in ty and not (ty.startswith("(") and ty.endswith(")")):
        return f"*({ty}) ${name}"
    return f"*{ty} ${name}"


def convert_params(raw: str) -> str:
    """逐项迁移，**保留每个参数原有的前导空白（含换行与缩进）**。

    迁移不该顺带改排版——重排会让 diff 核不动；超 80 字符的行由报告指出、人工决定怎么折。
    """
    out: list[str] = []
    for index, item in enumerate(split_params(raw)):
        leading = item[: len(item) - len(item.lstrip())]
        # 逗号后的空白由 `leading` 承载（原样保留），这里只补逗号本身——
        # 补 `", "` 会在原本已有换行缩进时多出一个空格。
        prefix = "" if index == 0 else ","
        out.append(prefix + leading + convert_signature_item(item))
    return "".join(out)


def body_span(text: str, start: int) -> tuple[int, int]:
    """从签名结束处找到函数体 `{…}` 的范围（花括号配对，跳过字符串与注释）。"""
    i = text.find("{", start)
    if i < 0:
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
    """把字符串与注释替换成等长空白，保留位置以便按索引改写。"""
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
    """体内**重新声明**同名局部变量（`name: type = …`）→ 冲突名单。

    `name = …` 不算冲突：那是写形参本身（写参本来就该写），迁移后变成 `*name = …`。
    只有带类型标注的才是新声明——那会遮蔽形参，语义不明，必须人工处理。
    """
    collisions: set[str] = set()
    for name in pointer_names:
        pattern = re.compile(rf"(?<![*\w.]){re.escape(name)}\s*:[^=\n]*=(?!=)")
        if pattern.search(masked):
            collisions.add(name)
    return collisions


def rewrite_body(body: str, pointer_names: set[str]) -> str:
    """指针形参在体内**任何位置**都写 `*name`，只有一个例外：基座加括号。

    kvlang 的实参是**按地址绑定**（`runtimec/vmbin/call.h`：指令中的 argaddr 是参数
    V.body 的唯一位置；spec 05-runtime语义/04-函数调用：帧槽是双层指针 `**T`，
    体内 `*name` 沿链直达调用方目标）。所以没有「读位置求值」这回事——
    传裸名会把「指向形参槽的地址」再叠一层。

    唯一需要变换的是基座：`*name·k` 会解析成 `*(name·k)`，必须写 `(*name)·k`。
    """
    masked = mask_noncode(body)
    edits: list[tuple[int, int, str]] = []
    for name in pointer_names:
        # `·` 后面的标识符是 **key 段**，不是变量：`string·lines(t)` 里的 `lines` 是
        # 算子名的第二段。动态键源码本来就写 `·*name`，工具不该替它造一个。
        pattern = re.compile(rf"(?<![*\w.$·]){re.escape(name)}\b")
        for m in pattern.finditer(masked):
            tail = masked[m.end():].lstrip()
            # 判空保持裸名：`p == None` / `p != None` 比的是指针本身，
            # 是唯一的空指针判定写法（spec 16-ptr），解引用就变成比值了。
            if tail.startswith(("== None", "!= None")):
                continue
            bracketed = tail[:1] in ("·", "[")
            replacement = f"(*{name})" if bracketed else f"*{name}"
            edits.append((m.start(), m.end(), replacement))
    for start, end, replacement in sorted(edits, reverse=True):
        body = body[:start] + replacement + body[end:]
    return body


def convert_text(text: str) -> tuple[str, list[str]]:
    """返回 (迁移后文本, 冲突清单)。冲突非空时调用方必须停下。"""
    out: list[str] = []
    pos = 0
    conflicts: list[str] = []
    for match in SIGNATURE.finditer(text):
        keyword, fname, params, returns = match.groups()
        signature = f"{keyword} {fname}({convert_params(params)}) -> ({convert_params(returns)})"
        out.append(text[pos:match.start()])
        out.append(signature)

        # 迁移后**全部**形参与返回值都是指针（值复制不参与迁移），体内的每一处
        # 引用都要按「读位置求值、写位置求址」改写。
        pointer_names = {
            parse_item(item)[0]
            for item in split_params(params) + split_params(returns)
            if parse_item(item)[0]
        }
        _, body_end = body_span(text, match.end())
        body = text[match.end():body_end]
        if pointer_names and body:
            masked = mask_noncode(body)
            collisions = find_local_collisions(masked, pointer_names)
            if collisions:
                conflicts.append(f"{fname}: {', '.join(sorted(collisions))}")
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
                print(f"CONFLICT {path}: {conflict} —— 局部变量与指针形参同名，手工处理",
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
