// 极简 Markdown → HTML：标题 / 列表 / 段落 / 行内 code、粗体、行内 ` ` /
// 围栏代码块（```kv 走 kvlang 高亮，其余走普通 <pre><code>）。
// 仅覆盖 stdlib 文档实际用到的语法子集。

import { highlightKv } from "./highlight";

function esc(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function inline(s: string): string {
  // 先按行内 `code` 切分，code 段内不再处理其它标记。
  const parts = s.split(/(`[^`]+`)/g);
  return parts
    .map((p) => {
      if (p.startsWith("`") && p.endsWith("`") && p.length >= 2) {
        return `<code class="inline">${esc(p.slice(1, -1))}</code>`;
      }
      let h = esc(p);
      h = h.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
      return h;
    })
    .join("");
}

export function renderMarkdown(src: string): string {
  const lines = src.split("\n");
  const out: string[] = [];
  let i = 0;
  let listOpen = false;

  const closeList = () => {
    if (listOpen) {
      out.push("</ul>");
      listOpen = false;
    }
  };

  while (i < lines.length) {
    const line = lines[i];

    // 围栏代码块
    const fence = line.match(/^```(\w*)\s*$/);
    if (fence) {
      closeList();
      const lang = fence[1];
      const body: string[] = [];
      i++;
      while (i < lines.length && !/^```\s*$/.test(lines[i])) {
        body.push(lines[i]);
        i++;
      }
      i++; // 跳过收尾 ```
      const code = body.join("\n");
      if (lang === "kv" || lang === "kvlang") {
        out.push(`<pre class="block">${highlightKv(code)}</pre>`);
      } else {
        out.push(`<pre class="block"><code>${esc(code)}</code></pre>`);
      }
      continue;
    }

    // 标题
    const h = line.match(/^(#{1,4})\s+(.*)$/);
    if (h) {
      closeList();
      const level = h[1].length;
      out.push(`<h${level}>${inline(h[2])}</h${level}>`);
      i++;
      continue;
    }

    // 列表项
    const li = line.match(/^\s*[-*]\s+(.*)$/);
    if (li) {
      if (!listOpen) {
        out.push("<ul>");
        listOpen = true;
      }
      out.push(`<li>${inline(li[1])}</li>`);
      i++;
      continue;
    }

    // 空行
    if (/^\s*$/.test(line)) {
      closeList();
      i++;
      continue;
    }

    // 段落（合并连续非空、非特殊行）
    closeList();
    const para: string[] = [line];
    i++;
    while (
      i < lines.length &&
      !/^\s*$/.test(lines[i]) &&
      !/^```/.test(lines[i]) &&
      !/^#{1,4}\s/.test(lines[i]) &&
      !/^\s*[-*]\s+/.test(lines[i])
    ) {
      para.push(lines[i]);
      i++;
    }
    out.push(`<p>${inline(para.join(" "))}</p>`);
  }

  closeList();
  return out.join("\n");
}
