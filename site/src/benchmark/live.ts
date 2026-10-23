// 运行时刷新：按 GitHub Contents API 找最新 CSV，若比构建期新则原地重绘。
// 失败（403 / 网络 / CORS / 解析 / 超时）一律静默，保留构建期内容。

import {
  buildChartSvg,
  buildMetaHtml,
  buildTableHtml,
  csvMeta,
  fullResultsLabel,
  normalizeRows,
  parseCsv,
  pickNewerVersion,
} from "./core.js";

type Entry = { name?: string; type?: string; download_url?: string };

const API =
  "https://api.github.com/repos/array2d/kvlang/contents/benchmark/results?per_page=100";
const TTL = 6 * 60 * 60 * 1000;
const TIMEOUT = 8000;
const LIST_KEY = "bench:list";

function cacheGet<T>(key: string): T | null {
  try {
    const raw = sessionStorage.getItem(key);
    if (!raw) return null;
    const { t, v } = JSON.parse(raw) as { t: number; v: T };
    return Date.now() - t > TTL ? null : v;
  } catch {
    return null;
  }
}

function cacheSet(key: string, v: unknown): void {
  try {
    sessionStorage.setItem(key, JSON.stringify({ t: Date.now(), v }));
  } catch {
    /* 隐私模式或配额不足，忽略 */
  }
}

async function get(url: string): Promise<Response> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT);
  try {
    return await fetch(url, {
      signal: ctrl.signal,
      headers: { Accept: "application/vnd.github+json" },
    });
  } finally {
    clearTimeout(timer);
  }
}

async function listEntries(): Promise<Entry[]> {
  const hit = cacheGet<Entry[]>(LIST_KEY);
  if (hit) return hit;
  const res = await get(API);
  if (!res.ok) throw new Error(`list ${res.status}`);
  const json = (await res.json()) as unknown;
  if (!Array.isArray(json)) throw new Error("list is not an array");
  const entries = json as Entry[];
  cacheSet(LIST_KEY, entries);
  return entries;
}

async function fetchCsv(key: string, url: string): Promise<string> {
  const cacheKey = `bench:csv:${key}`;
  const hit = cacheGet<string>(cacheKey);
  if (hit) return hit;
  const res = await get(url);
  if (!res.ok) throw new Error(`csv ${res.status}`);
  const text = await res.text();
  cacheSet(cacheKey, text);
  return text;
}

export async function refreshBenchmarks(): Promise<void> {
  const root = document.getElementById("benchmarks");
  const labelsEl = document.getElementById("bench-labels");
  if (!root || !labelsEl) return;

  let labels: Record<string, string | string[]>;
  try {
    labels = JSON.parse(labelsEl.textContent || "{}");
  } catch {
    return;
  }

  const current = root.dataset.version || "";
  try {
    const newer = pickNewerVersion(await listEntries(), current);
    if (!newer || !newer.entry.download_url) return;

    const rows = parseCsv(await fetchCsv(newer.key, newer.entry.download_url));
    const groups = normalizeRows(rows);
    if (groups.length === 0) return;

    const meta = csvMeta(rows);
    const chart = root.querySelector<HTMLElement>("[data-bench-chart]");
    const table = root.querySelector<HTMLElement>("[data-bench-table]");
    const metaEl = root.querySelector<HTMLElement>("[data-bench-meta]");
    const summaryEl = root.querySelector<HTMLElement>("[data-bench-summary]");

    if (chart) chart.innerHTML = buildChartSvg(groups, labels);
    if (table) table.innerHTML = buildTableHtml(groups, labels);
    if (metaEl) {
      metaEl.innerHTML = buildMetaHtml(meta, labels, newer.entry.download_url);
    }
    if (summaryEl) summaryEl.textContent = fullResultsLabel(labels, groups);
    root.dataset.version = newer.key;
  } catch {
    /* 保留构建期 fallback */
  }
}
