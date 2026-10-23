#!/usr/bin/env node
/**
 * 校验内容 JSON 里的每个代码块仍是其声明来源文件的子串。
 * 代码块由 { code, source } 成对描述；HTML 实体（&lt; 等）先还原再比较。
 */
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const SITE = resolve(fileURLToPath(new URL("..", import.meta.url)));
const REPO = resolve(SITE, "..");
const LOCALES = ["en", "zh"];

function unescapeHtml(s) {
  return s
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&amp;/g, "&");
}

function collect(obj, out = []) {
  if (Array.isArray(obj)) {
    obj.forEach((v) => collect(v, out));
    return out;
  }
  if (obj && typeof obj === "object") {
    if (typeof obj.code === "string" && obj.code) out.push(obj);
    for (const v of Object.values(obj)) collect(v, out);
  }
  return out;
}

let checked = 0;
let failed = 0;

for (const locale of LOCALES) {
  const data = JSON.parse(readFileSync(join(SITE, "content", `${locale}.json`), "utf8"));
  for (const item of collect(data)) {
    const snippet = unescapeHtml(item.code).replace(/^\n/, "").replace(/\n$/, "");
    let src;
    try {
      src = readFileSync(join(REPO, item.source), "utf8");
    } catch {
      console.error(`FAIL ${locale}: source file missing: ${item.source}`);
      failed++;
      continue;
    }
    checked++;
    if (src.includes(snippet)) {
      console.log(`ok   ${locale} ← ${item.source}`);
    } else {
      failed++;
      console.error(`FAIL ${locale}: snippet drifted from ${item.source}`);
      console.error(`  ${JSON.stringify(snippet.slice(0, 160))}`);
    }
  }
}

if (failed > 0) {
  console.error(`\n${failed} snippet(s) do not match their sources.`);
  process.exit(1);
}
console.log(`\nall ${checked} snippets match their sources.`);
