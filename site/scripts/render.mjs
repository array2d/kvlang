#!/usr/bin/env node
/**
 * 从 content/{en,zh}.json + templates/*.html 生成四张静态页。
 * 模板语法只有三种：
 *   {{ key.path }}      转义替换
 *   {{{ key.path }}}    原样插入（文案里含 HTML 时用）
 *   {{#each key.path}} … {{/each}}   循环；体内 {{ field }} / {{ this }}
 * 不引入任何模板依赖。
 */
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { readdirSync } from "node:fs";
import { fileURLToPath } from "node:url";
import {
  buildChartSvg,
  buildMetaHtml,
  buildTableHtml,
  buildVersionsMetaHtml,
  buildVersionsTableHtml,
  csvMeta,
  fullResultsLabel,
  normalizeRows,
  parseCsv,
  pickLatest,
  rowCount,
  versionOf,
} from "../src/benchmark/core.js";

const SITE = resolve(fileURLToPath(new URL("..", import.meta.url)));
const REPO = resolve(SITE, "..");
const CONTENT = join(SITE, "content");
const TEMPLATES = join(SITE, "templates");

const PAGES = [
  { template: "home.html", data: "en.json", out: "index.html" },
  { template: "home.html", data: "zh.json", out: "zh/index.html" },
  { template: "runtimes.html", data: "en.json", out: "runtimes/index.html" },
  { template: "runtimes.html", data: "zh.json", out: "zh/runtimes/index.html" },
  { template: "benchmarks.html", data: "en.json", out: "benchmarks/index.html" },
  { template: "benchmarks.html", data: "zh.json", out: "zh/benchmarks/index.html" },
];

function get(obj, path) {
  return path.split(".").reduce((o, k) => (o == null ? undefined : o[k]), obj);
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/** 全大写占位符（{{VERSION}} 等）留给 Vite 的 kvInfo 插件。 */
const VITE_PLACEHOLDER = /^[A-Z_]+$/;

function interpolate(tpl, data) {
  return tpl
    .replace(/\{\{\{\s*([^}]+?)\s*\}\}\}/g, (m, p) =>
      VITE_PLACEHOLDER.test(p) ? m : String(get(data, p) ?? ""),
    )
    .replace(/\{\{\s*([^}]+?)\s*\}\}/g, (m, p) =>
      VITE_PLACEHOLDER.test(p) ? m : escapeHtml(get(data, p) ?? ""),
    );
}

/** 由内向外展开 each，支持嵌套。 */
function expand(tpl, data) {
  const EACH = /\{\{#each\s+([^}]+?)\s*\}\}((?:(?!\{\{#each)[\s\S])*?)\{\{\/each\}\}/g;
  let out = tpl;
  for (;;) {
    const next = out.replace(EACH, (_, path, body) => {
      const arr = get(data, path);
      if (!Array.isArray(arr)) throw new Error(`each: not an array: ${path}`);
      return arr
        .map((item) =>
          interpolate(body, {
            ...data,
            ...(item && typeof item === "object" ? item : {}),
            this: item,
          }),
        )
        .join("");
    });
    if (next === out) return out;
    out = next;
  }
}

/** 每个 semver 的最新 CSV 条目，按版本升序。 */
function latestCsvEntries(dir) {
  const byVersion = new Map();
  for (const name of readdirSync(dir)) {
    const m = /^results-v(\d+)\.(\d+)\.(\d+)-\d{8}T\d{6}Z\.csv$/.exec(name);
    if (!m) continue;
    const sem = `${Number(m[1])}.${Number(m[2])}.${Number(m[3])}`;
    const cur = byVersion.get(sem);
    if (!cur || name > cur.name) byVersion.set(sem, { name, sem });
  }
  return [...byVersion.values()].sort((a, b) => compareSem(a.sem, b.sem));
}

function compareSem(a, b) {
  const pa = a.split(".").map(Number);
  const pb = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) {
    if (pa[i] !== pb[i]) return pa[i] - pb[i];
  }
  return 0;
}

const OLD_MILESTONES = new Set(["0.2.5", "0.2.9", "0.2.12", "0.2.14", "0.2.16"]);
/** 独台 CPU（Xeon Gold 6418H，跨机不可比）的 v0.2.17 快照排除。 */
const EXCLUDE_CPU = "Intel(R) Xeon(R) Gold 6418H";

/** 收集多版本数据：最新 + 关键里程碑，按版本降序（最新在前）。 */
function collectVersions(dir, latestName) {
  const versions = [];
  const names = new Set([latestName]);
  for (const e of latestCsvEntries(dir)) {
    if (OLD_MILESTONES.has(e.sem) && e.name !== latestName) names.add(e.name);
  }
  for (const name of names) {
    const file = join(dir, name);
    const rows = parseCsv(readFileSync(file, "utf8"));
    if (rows.length === 0) continue;
    const meta = csvMeta(rows);
    if (meta.cpu === EXCLUDE_CPU) continue;
    const groups = normalizeRows(rows);
    if (groups.length === 0) continue;
    const m = /^results-(v\d+\.\d+\.\d+)-(\d{8}T\d{6}Z)\.csv$/.exec(name);
    versions.push({
      name,
      version: m[1],
      groups,
      timestamp: meta.timestamp,
      cpu: meta.cpu,
      toolchain: (rows[0] && rows[0].native_opt) || "",
    });
  }
  return versions.sort((a, b) => compareSem(b.version.replace(/^v/, ""), a.version.replace(/^v/, "")));
}

/** 独立 benchmark 页数据：最新版本图/表 + 多版本对比表。 */
function benchPage(data, latestEntry) {
  const dir = join(REPO, "benchmark/results");
  const latestRows = parseCsv(readFileSync(join(dir, latestEntry.name), "utf8"));
  const groups = normalizeRows(latestRows);
  const meta = csvMeta(latestRows);
  const key = `${versionOf(latestEntry.name)}-${/-([0-9]{8}T[0-9]{6}Z)\.csv$/.exec(latestEntry.name)[1]}`;
  const csvUrl = `https://raw.githubusercontent.com/array2d/kvlang/master/benchmark/results/${latestEntry.name}`;
  const b = data.benchmarks;
  b.version = meta.version;
  b.key = key;
  b.chartSvg = buildChartSvg(groups, b);
  b.tableHtml = buildTableHtml(groups, b);
  b.metaHtml = buildMetaHtml(meta, b, csvUrl);
  b.resultsLabel = fullResultsLabel(b, groups);
  const versions = collectVersions(dir, latestEntry.name);
  b.versionsMetaHtml = buildVersionsMetaHtml(versions, b);
  b.versionsTableHtml = buildVersionsTableHtml(versions, b);
  b.labelsJson = JSON.stringify({
    title: b.title,
    desc: b.desc,
    series: b.series,
    tableHeaders: b.tableHeaders,
    versionLabel: b.versionLabel,
    dateLabel: b.dateLabel,
    rawCsvLabel: b.rawCsvLabel,
    axisTitle: b.axisTitle,
    fullResults: b.fullResults,
    versionRowLabels: b.versionRowLabels,
  });
  console.log(
    `benchmarks page: ${versions.length} versions (${versions.map((v) => v.version).join(", ")})`,
  );
}

/** 从本地 benchmark/results 选最新 CSV，填充 data.benchmarks 供页面渲染。 */
function benchFallback(data) {
  const dir = join(REPO, "benchmark/results");
  const entries = readdirSync(dir)
    .filter((f) => /^results-v\d+\.\d+\.\d+-\d{8}T\d{6}Z\.csv$/.test(f))
    .map((name) => ({ name, type: "file", download_url: join(dir, name) }));
  const entry = pickLatest(entries);
  if (!entry) return;
  benchPage(data, entry);
  console.log(
    `benchmark fallback: ${entry.name} (${rowCount(normalizeRows(parseCsv(readFileSync(join(dir, entry.name), "utf8"))))} rows)`,
  );
}

function render(template, data) {
  return interpolate(expand(template, data), data);
}

for (const page of PAGES) {
  const data = JSON.parse(readFileSync(join(CONTENT, page.data), "utf8"));
  const tpl = readFileSync(join(TEMPLATES, page.template), "utf8");
  const out = join(SITE, page.out);
  mkdirSync(dirname(out), { recursive: true });
  const marker = "<!-- generated by scripts/render.mjs from content/*.json + templates/*.html; do not edit -->";
  benchFallback(data);
  const html = render(tpl, data).replace(/^<!doctype html>\n/, `<!doctype html>\n${marker}\n`);
  writeFileSync(out, html);
  console.log(`render ${page.data} + ${page.template} -> ${page.out}`);
}
