// 构建脚本与浏览器共用的 benchmark 渲染核心。纯 ESM，无 DOM / Node 依赖。

const FILE_RE = /^results-v(\d+)\.(\d+)\.(\d+)-(\d{8}T\d{6}Z)\.csv$/;

/** 最小 RFC4180 解析器：引号、转义引号、CRLF。 */
export function parseCsv(text) {
  const table = [];
  let row = [];
  let field = "";
  let quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"') {
        if (text[i + 1] === '"') {
          field += '"';
          i++;
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
    } else if (c === '"') {
      quoted = true;
    } else if (c === ",") {
      row.push(field);
      field = "";
    } else if (c === "\n") {
      row.push(field);
      table.push(row);
      row = [];
      field = "";
    } else if (c !== "\r") {
      field += c;
    }
  }
  if (field !== "" || row.length) {
    row.push(field);
    table.push(row);
  }
  if (table.length < 2) return [];
  const header = table[0];
  return table
    .slice(1)
    .filter((r) => r.length === header.length && r.some((v) => v !== ""))
    .map((r) => Object.fromEntries(header.map((h, i) => [h, r[i]])));
}

/** 从 Contents API 条目中按 semver + 时间戳选最新 CSV。 */
export function pickLatest(entries) {
  if (!Array.isArray(entries)) return null;
  let best = null;
  for (const entry of entries) {
    if (entry.type && entry.type !== "file") continue;
    const m = FILE_RE.exec(entry.name || "");
    if (!m) continue;
    const key = [Number(m[1]), Number(m[2]), Number(m[3]), m[4]];
    if (!best || compareKey(key, best.key) > 0) best = { key, entry };
  }
  return best ? best.entry : null;
}

export function versionOf(name) {
  const m = /^results-(v\d+\.\d+\.\d+)-/.exec(name || "");
  return m ? m[1] : "";
}

export function compareVersion(a, b) {
  const pa = String(a).replace(/^v/, "").split(".").map(Number);
  const pb = String(b).replace(/^v/, "").split(".").map(Number);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] || 0) - (pb[i] || 0);
    if (d !== 0) return d;
  }
  return 0;
}

/**
 * 列表中存在比 currentKey 更新的版本时返回 { entry, key }，否则 null。
 * key = "vX.Y.Z-<timestamp>"；同版本的新时间戳会被视为更新。
 */
export function pickNewerVersion(entries, currentKey) {
  const latest = pickLatest(entries);
  if (!latest) return null;
  const key = `${versionOf(latest.name)}-${FILE_RE.exec(latest.name)[4]}`;
  return compareKey(fullKey(key), fullKey(currentKey || "v0.0.0-00000000T000000Z")) > 0
    ? { entry: latest, key }
    : null;
}

function fullKey(key) {
  const m = /^(v\d+\.\d+\.\d+)-(\d{8}T\d{6}Z)$/.exec(key || "");
  if (!m) return [0, 0, 0, ""];
  const [major, minor, patch] = m[1].replace(/^v/, "").split(".").map(Number);
  return [major, minor, patch, m[2]];
}

function compareKey(a, b) {
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] - b[i];
  return a[3] < b[3] ? -1 : a[3] > b[3] ? 1 : 0;
}

function inputNum(input) {
  const m = /(\d+)/.exec(input || "");
  return m ? Number(m[1]) : 0;
}

/** 过滤有效行，按 case 分组并按规模点数值排序。 */
export function normalizeRows(rows) {
  const ok = rows.filter((r) => r.valid === "True" && r.kvlang_shm_ns !== "");
  const grouped = new Map();
  for (const r of ok) {
    if (!grouped.has(r.case)) grouped.set(r.case, []);
    grouped.get(r.case).push(r);
  }
  return [...grouped.entries()]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([name, points]) => ({
      case: name,
      points: points.sort((a, b) => inputNum(a.input) - inputNum(b.input)),
    }));
}

export function rowCount(groups) {
  return groups.reduce((n, g) => n + g.points.length, 0);
}

/** ns 自适应单位，3 位有效数字；空值返回 —。 */
export function formatNs(ns) {
  if (ns === "" || ns == null) return "—";
  const n = Number(ns);
  if (!Number.isFinite(n) || n <= 0) return "—";
  if (n < 1e3) return `${n.toPrecision(3)} ns`;
  if (n < 1e6) return `${(n / 1e3).toPrecision(3)} µs`;
  if (n < 1e9) return `${(n / 1e6).toPrecision(3)} ms`;
  return `${(n / 1e9).toPrecision(3)} s`;
}

/** 从 CSV 行取展示元数据。 */
export function csvMeta(rows) {
  const r = rows[0] || {};
  return {
    version: r.version || "",
    commit: r.commit || "",
    timestamp: r.timestamp || "",
    cpu: r.cpu || "",
    rows: rows.length,
  };
}

/** 元信息行：版本 / commit / 日期 / CPU / 原始 CSV 链接。 */
export function buildMetaHtml(meta, labels, csvHref) {
  const parts = [`${escapeXml(labels.versionLabel)} <strong>${escapeXml(meta.version)}</strong>`];
  if (meta.commit) parts.push(`commit <code>${escapeXml(meta.commit)}</code>`);
  if (meta.timestamp) parts.push(`${escapeXml(labels.dateLabel)} ${escapeXml(meta.timestamp.slice(0, 10))}`);
  if (meta.cpu) parts.push(escapeXml(meta.cpu));
  if (csvHref) parts.push(`<a href="${escapeXml(csvHref)}">${escapeXml(labels.rawCsvLabel)}</a>`);
  return parts.join(" · ");
}

const SERIES = [
  { key: "kvlang_shm_ns", cls: "s1", shape: 0 },
  { key: "kvlang_fs_ns", cls: "s2", shape: 1 },
  { key: "kvlang_redis_ns", cls: "s3", shape: 2 },
  { key: "python_ns", cls: "s4", shape: 3 },
  { key: "rust_ns", cls: "s5", shape: 0 },
  { key: "c_ns", cls: "s6", shape: 1 },
];

const Y_MIN = 1e2;
const Y_MAX = 1e9;
const PANEL_W = 320;
const PANEL_H = 170;
const PAD_L = 34;
const PAD_R = 8;
const PAD_T = 22;
const PAD_B = 20;
const COLS = 2;
const LEGEND_H = 24;

function logY(v) {
  const clamped = Math.min(Y_MAX, Math.max(Y_MIN, v));
  const t = (Math.log10(clamped) - 2) / 7;
  return PAD_T + (1 - t) * (PANEL_H - PAD_T - PAD_B);
}

function pointShape(shape, x, y) {
  switch (shape) {
    case 1:
      return `<rect x="${(x - 2.4).toFixed(1)}" y="${(y - 2.4).toFixed(1)}" width="4.8" height="4.8"/>`;
    case 2:
      return `<polygon points="${x},${(y - 3).toFixed(1)} ${(x + 2.8).toFixed(1)},${(y + 2).toFixed(1)} ${(x - 2.8).toFixed(1)},${(y + 2).toFixed(1)}"/>`;
    case 3:
      return `<polygon points="${x},${(y - 3).toFixed(1)} ${(x + 3).toFixed(1)},${y} ${x},${(y + 3).toFixed(1)} ${(x - 3).toFixed(1)},${y}"/>`;
    default:
      return `<circle cx="${x}" cy="${y}" r="2.6"/>`;
  }
}

function legendItem(shape, cls, x, y) {
  const mark =
    shape === 1
      ? `<rect x="${(x - 2.4).toFixed(1)}" y="${(y - 2.4).toFixed(1)}" width="4.8" height="4.8"/>`
      : shape === 2
        ? `<polygon points="${x},${(y - 3).toFixed(1)} ${(x + 2.8).toFixed(1)},${(y + 2).toFixed(1)} ${(x - 2.8).toFixed(1)},${(y + 2).toFixed(1)}"/>`
        : shape === 3
          ? `<polygon points="${x},${(y - 3).toFixed(1)} ${(x + 3).toFixed(1)},${y} ${x},${(y + 3).toFixed(1)} ${(x - 3).toFixed(1)},${y}"/>`
          : `<circle cx="${x}" cy="${y}" r="2.6"/>`;
  return `<g class="bench-point ${cls}">${mark}</g>`;
}

/** 10 个 case 的小多图；纵轴为对数刻度的 ns。 */
export function buildChartSvg(groups, labels) {
  const rows = Math.ceil(groups.length / COLS);
  const width = COLS * PANEL_W + 48; // 右侧留出最后一个 x 标签的宽度
  const height = LEGEND_H + rows * PANEL_H;
  const parts = [];
  parts.push(
    `<svg class="bench-chart" viewBox="0 0 ${width} ${height}" role="img" aria-labelledby="bench-chart-title">`,
    `<title id="bench-chart-title">${escapeXml(labels.title)}</title>`,
    `<desc>${escapeXml(stripTags(labels.desc || labels.lead))}</desc>`,
    `<g class="bench-legend">`,
  );

  // 图例：6 项，颜色 + 点形与图中一致
  const itemW = width / 6;
  SERIES.forEach((s, i) => {
    const x = i * itemW + 10;
    const label = (labels.series || [])[i] || s.key;
    parts.push(legendItem(s.shape, s.cls, x, 10));
    parts.push(
      `<text class="bench-legend-label" x="${(x + 7).toFixed(1)}" y="13">${escapeXml(label)}</text>`,
    );
  });
  parts.push(`</g>`);
  parts.push(
    `<text class="bench-axis-title" transform="translate(9 ${(LEGEND_H + 90).toFixed(1)}) rotate(-90)" text-anchor="middle">${escapeXml(labels.axisTitle || "time (ns, log scale)")}</text>`,
  );

  groups.forEach((group, idx) => {
    const col = idx % COLS;
    const row = Math.floor(idx / COLS);
    const ox = col * PANEL_W;
    const oy = LEGEND_H + row * PANEL_H;
    const x0 = ox + PAD_L;
    const x1 = ox + PANEL_W - PAD_R;
    const n = group.points.length;
    parts.push(`<g>`);
    parts.push(
      `<text x="${(ox + PAD_L).toFixed(1)}" y="${(oy + 13).toFixed(1)}" class="bench-case">${escapeXml(group.case)}</text>`,
    );
    for (const tick of [1e3, 1e6, 1e9]) {
      const y = oy + logY(tick);
      parts.push(
        `<line class="bench-grid" x1="${x0}" y1="${y.toFixed(1)}" x2="${x1}" y2="${y.toFixed(1)}"/>`,
      );
      // 只在左列标刻度值，避免重复
      if (col === 0) {
        const label = tick === 1e3 ? "1µs" : tick === 1e6 ? "1ms" : "1s";
        parts.push(
          `<text class="bench-y-tick" x="${(x0 - 3).toFixed(1)}" y="${(y + 3).toFixed(1)}" text-anchor="end">${label}</text>`,
        );
      }
    }
    for (const s of SERIES) {
      let d = "";
      let pen = false;
      const marks = [];
      group.points.forEach((p, i) => {
        const raw = p[s.key];
        if (raw === "" || raw == null || !Number.isFinite(Number(raw)) || Number(raw) <= 0) {
          pen = false;
          return;
        }
        const x = n === 1 ? (x0 + x1) / 2 : x0 + (i * (x1 - x0)) / (n - 1);
        const y = oy + logY(Number(raw));
        d += `${pen ? "L" : "M"}${x.toFixed(1)},${y.toFixed(1)}`;
        pen = true;
        marks.push(pointShape(s.shape, x, y));
      });
      if (d) {
        parts.push(`<path class="bench-line ${s.cls}" d="${d}"/>`);
        parts.push(`<g class="bench-point ${s.cls}">${marks.join("")}</g>`);
      }
    }
    group.points.forEach((p, i) => {
      const x = n === 1 ? (x0 + x1) / 2 : x0 + (i * (x1 - x0)) / (n - 1);
      parts.push(
        `<text class="bench-tick" x="${x.toFixed(1)}" y="${(oy + PANEL_H - 5).toFixed(1)}" text-anchor="middle">${escapeXml(p.input)}</text>`,
      );
    });
    parts.push(`</g>`);
  });

  parts.push("</svg>");
  return parts.join("");
}

/** 全部有效行的表格；单位自适应。 */
export function buildTableHtml(groups, labels) {
  const head = labels.tableHeaders
    .map((h) => `<th scope="col">${escapeXml(h)}</th>`)
    .join("");
  const body = groups
    .flatMap((g) => g.points.map((p) => ({ ...p, case: g.case })))
    .map((p) => {
      const cells = [
        escapeXml(p.case || ""),
        escapeXml(p.input || ""),
        formatNs(p.kvlang_shm_ns),
        formatNs(p.kvlang_fs_ns),
        formatNs(p.kvlang_redis_ns),
        formatNs(p.python_ns),
        formatNs(p.rust_ns),
        formatNs(p.c_ns),
      ];
      return `<tr>${cells.map((c) => `<td>${c}</td>`).join("")}</tr>`;
    })
    .join("");
  return `<table class="bench-table"><thead><tr>${head}</tr></thead><tbody>${body}</tbody></table>`;
}

export function fullResultsLabel(labels, groups) {
  return String(labels.fullResults || "").replace("{n}", String(rowCount(groups)));
}

function stripTags(s) {
  return String(s ?? "").replace(/<[^>]+>/g, "");
}

function escapeXml(s) {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}
