#!/usr/bin/env node
/**
 * 校验主页里的每个代码块仍是其声明来源文件的子串。
 * 代码块形如：
 *   <pre class="code"><code><!-- source: README.md -->
 *   ...原文...
 *   </code></pre>
 * 不一致时以非 0 退出，阻止构建。
 */
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const SITE = resolve(fileURLToPath(new URL("..", import.meta.url)));
const REPO = resolve(SITE, "..");
const PAGES = ["index.html", "zh/index.html"];
const RE =
  /<pre class="code"><code><!--\s*source:\s*([^\s]+)\s*-->([\s\S]*?)<\/code><\/pre>/g;

function unescapeHtml(s) {
  return s
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&amp;/g, "&");
}

let checked = 0;
let failed = 0;

for (const page of PAGES) {
  const html = readFileSync(join(SITE, page), "utf8");
  RE.lastIndex = 0;
  let m;
  while ((m = RE.exec(html)) !== null) {
    const rel = m[1];
    const snippet = unescapeHtml(m[2]).replace(/^\n/, "").replace(/\n$/, "");
    let src;
    try {
      src = readFileSync(join(REPO, rel), "utf8");
    } catch {
      console.error(`FAIL ${page}: source file missing: ${rel}`);
      failed++;
      continue;
    }
    checked++;
    if (src.includes(snippet)) {
      console.log(`ok   ${page} ← ${rel}`);
    } else {
      failed++;
      console.error(`FAIL ${page}: snippet drifted from ${rel}`);
      console.error(`  ${JSON.stringify(snippet.slice(0, 160))}`);
    }
  }
}

if (failed > 0) {
  console.error(`\n${failed} snippet(s) do not match their sources.`);
  process.exit(1);
}
console.log(`\nall ${checked} snippets match their sources.`);
