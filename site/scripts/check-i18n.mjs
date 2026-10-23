#!/usr/bin/env node
/**
 * 中英内容一致性校验：
 *  1. 两份 JSON 的键路径集合相同（数组按元素 schema 抽样）；
 *  2. 同一数组在两份文件里长度相同；
 *  3. 不可翻译字段（href/code/source/image/arch/name）逐字一致；
 *  4. 可翻译必填字段非空。
 */
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const SITE = resolve(fileURLToPath(new URL("..", import.meta.url)));
const LOCALES = ["en", "zh"];
const data = Object.fromEntries(
  LOCALES.map((l) => [l, JSON.parse(readFileSync(join(SITE, "content", `${l}.json`), "utf8"))]),
);

const UNTRANSLATABLE = new Set(["href", "code", "source", "image", "arch", "name"]);
/** 这些链接按语言不同：导航的 Runtimes 入口与语言切换。 */
const LOCALE_AWARE = [/^nav\.items\[\d+\]\.href$/, /^(?:.*\.)?nav\.switch\.href$/];
const REQUIRED = new Set([
  "title", "label", "lead", "body", "notes", "description",
  "ogTitle", "ogDescription", "twitterTitle", "twitterDescription",
]);

function keyPaths(obj, prefix = "", out = new Set()) {
  if (Array.isArray(obj)) {
    out.add(`${prefix}[]`);
    if (obj.length) keyPaths(obj[0], `${prefix}[]`, out);
    return out;
  }
  if (obj && typeof obj === "object") {
    for (const [k, v] of Object.entries(obj)) {
      const p = prefix ? `${prefix}.${k}` : k;
      out.add(p);
      keyPaths(v, p, out);
    }
  }
  return out;
}

function arrayLens(obj, prefix = "", out = new Map()) {
  if (Array.isArray(obj)) {
    out.set(prefix, obj.length);
    obj.forEach((v, i) => arrayLens(v, `${prefix}[${i}]`, out));
    return out;
  }
  if (obj && typeof obj === "object") {
    for (const [k, v] of Object.entries(obj)) {
      arrayLens(v, prefix ? `${prefix}.${k}` : k, out);
    }
  }
  return out;
}

function untranslatable(obj, prefix = "", out = new Map()) {
  if (Array.isArray(obj)) {
    obj.forEach((v, i) => untranslatable(v, `${prefix}[${i}]`, out));
    return out;
  }
  if (obj && typeof obj === "object") {
    for (const [k, v] of Object.entries(obj)) {
      const p = prefix ? `${prefix}.${k}` : k;
      if (UNTRANSLATABLE.has(k)) out.set(p, JSON.stringify(v));
      untranslatable(v, p, out);
    }
  }
  return out;
}

function empties(obj, prefix = "", out = []) {
  if (Array.isArray(obj)) {
    obj.forEach((v, i) => empties(v, `${prefix}[${i}]`, out));
    return out;
  }
  if (obj && typeof obj === "object") {
    for (const [k, v] of Object.entries(obj)) {
      const p = prefix ? `${prefix}.${k}` : k;
      if (REQUIRED.has(k) && typeof v === "string" && v.trim() === "") out.push(p);
      empties(v, p, out);
    }
  }
  return out;
}

const errors = [];

const [en, zh] = LOCALES.map((l) => data[l]);
const enKeys = keyPaths(en);
const zhKeys = keyPaths(zh);
for (const k of enKeys) if (!zhKeys.has(k)) errors.push(`key only in en: ${k}`);
for (const k of zhKeys) if (!enKeys.has(k)) errors.push(`key only in zh: ${k}`);

const enLens = arrayLens(en);
const zhLens = arrayLens(zh);
for (const [k, n] of enLens) {
  if (zhLens.get(k) !== n) errors.push(`array length differs at ${k}: en=${n} zh=${zhLens.get(k)}`);
}

const enFixed = untranslatable(en);
const zhFixed = untranslatable(zh);
for (const [k, v] of enFixed) {
  if (LOCALE_AWARE.some((re) => re.test(k))) continue;
  if (zhFixed.get(k) !== v) errors.push(`untranslatable differs at ${k}: en=${v} zh=${zhFixed.get(k)}`);
}

for (const l of LOCALES) {
  for (const p of empties(data[l])) errors.push(`empty required field (${l}): ${p}`);
}

if (errors.length) {
  console.error("check-i18n FAILED:");
  for (const e of errors) console.error(`  - ${e}`);
  process.exit(1);
}
console.log(`check-i18n OK: ${enKeys.size} keys, ${enLens.size} arrays, ${enFixed.size} fixed fields`);
