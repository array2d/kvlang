// .kv 文档结构化解析：把一份 .kv 源码切成有序的三类块并渲染为 HTML。
//  - comment  : 开头/独立的 # 注释块 → 作说明富文本（按 Markdown 渲染）
//  - markdown : lib{ """…""" } 里的三引号字符串 → 内嵌 Markdown 富文本
//  - code     : 其余真实 kvlang 代码 → <code language="kvlang"> 高亮块
// 顺序严格保持源码出现次序。

import { renderMarkdown } from "./markdown";
import { highlightKv } from "./highlight";

type Kind = "comment" | "code" | "markdown";

function block(kind: Kind, text: string): string {
  const t = text.replace(/^\n+|\n+$/g, "");
  if (!t.trim()) return "";
  if (kind === "code") return `<pre class="block">${highlightKv(t)}</pre>`;
  if (kind === "markdown") return `<div class="md">${renderMarkdown(t)}</div>`;
  const stripped = t
    .split("\n")
    .map((l) => l.replace(/^\s*#\s?/, ""))
    .join("\n");
  return `<div class="md doc-comment">${renderMarkdown(stripped)}</div>`;
}

export function renderKvDoc(src: string): string {
  const lines = src.split("\n");
  const out: string[] = [];
  let comment: string[] = [];
  let code: string[] = [];
  let md: string[] = [];
  let inString = false;

  const flushComment = () => {
    if (comment.length) out.push(block("comment", comment.join("\n")));
    comment = [];
  };
  const flushCode = () => {
    if (code.length) out.push(block("code", code.join("\n")));
    code = [];
  };

  for (const line of lines) {
    if (inString) {
      const idx = line.indexOf('"""');
      if (idx === -1) {
        md.push(line);
        continue;
      }
      md.push(line.slice(0, idx));
      out.push(block("markdown", md.join("\n")));
      md = [];
      inString = false;
      const post = line.slice(idx + 3);
      if (post.trim()) code.push(post);
      continue;
    }

    const open = line.indexOf('"""');
    if (open !== -1) {
      const pre = line.slice(0, open);
      if (pre.trim()) code.push(pre);
      flushComment();
      flushCode();
      inString = true;
      const after = line.slice(open + 3);
      md.push(after);
      continue;
    }

    const isComment = /^\s*#/.test(line);
    if (isComment && code.length === 0) {
      comment.push(line);
    } else {
      flushComment();
      code.push(line);
    }
  }

  if (md.length) out.push(block("markdown", md.join("\n")));
  flushComment();
  flushCode();
  return out.join("\n");
}
