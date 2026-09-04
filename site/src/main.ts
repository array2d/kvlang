// 入口：编排目录列表 → 侧边栏 → 选中渲染，hash 路由支持深链。

import "./styles.css";
import {
  listStdlib,
  fetchRaw,
  defaultBranch,
  RateLimitError,
  type KvFile,
} from "./github";
import { renderSidebar } from "./tree";
import { renderKvDoc } from "./kvdoc";

const sidebar = document.getElementById("sidebar")!;
const content = document.getElementById("content")!;
const branchEl = document.getElementById("branch")!;
const themeBtn = document.getElementById("theme")!;

let files: KvFile[] = [];

function currentPath(): string {
  const h = location.hash;
  return h.startsWith("#/") ? decodeURIComponent(h.slice(2)) : "";
}

function errHtml(e: unknown): string {
  const msg =
    e instanceof RateLimitError
      ? e.message
      : `加载失败：${e instanceof Error ? e.message : String(e)}`;
  return `<div class="error">${msg}</div>`;
}

async function renderFile(path: string): Promise<void> {
  const file = files.find((f) => f.path === path);
  if (!file) {
    content.innerHTML = `<div class="error">未找到文档：${path}</div>`;
    return;
  }
  setSidebar(renderSidebar(files, path));
  content.innerHTML = `<div class="loading">加载 ${file.name}…</div>`;
  try {
    const src = await fetchRaw(file.path, file.sha);
    content.innerHTML = `<article class="doc"><h1 class="doc-title">${file.name}</h1>${renderKvDoc(
      src,
    )}</article>`;
    content.scrollTop = 0;
  } catch (e) {
    content.innerHTML = errHtml(e);
  }
}

function route(): void {
  const path = currentPath();
  if (path) {
    renderFile(path);
  } else {
    setSidebar(renderSidebar(files, ""));
    content.innerHTML = `<article class="doc"><h1 class="doc-title">kvlang 标准库文档</h1>
      <p>左侧选择一个 <code class="inline">.kv</code> 文档查看。内容实时取自
      <code class="inline">array2d/kvlang</code> 仓库 <code class="inline">stdlib/</code> 目录。</p>
      ${agentCard()}</article>`;
  }
}

// 侧边栏统一追加 Agent 源清单入口。
function setSidebar(html: string): void {
  sidebar.innerHTML =
    html +
    `<div class="side-agent"><a href="#agent-sources">⚙ 给 Agent 的源清单</a></div>`;
}

// 首页给人看的 Agent 源清单卡片（用 HEAD 跟随默认分支，避免分支名硬编码）。
function agentCard(): string {
  return `<section class="agent-card">
    <h2>给 Agent / 爬虫</h2>
    <p>本页客户端渲染，正文运行时拉取。不执行 JS 的 agent 请直接抓生数据源（<code class="inline">HEAD</code> 跟随默认分支）：</p>
    <ul>
      <li>目录清单：<a href="https://api.github.com/repos/array2d/kvlang/git/trees/HEAD?recursive=1">GitHub trees API</a>（取 <code class="inline">stdlib/*.kv</code>）</li>
      <li>单文件全文：<code class="inline">https://raw.githubusercontent.com/array2d/kvlang/HEAD/&lt;path&gt;</code></li>
      <li>可浏览目录：<a href="https://github.com/array2d/kvlang/tree/HEAD/stdlib">github.com/array2d/kvlang/tree/HEAD/stdlib</a></li>
    </ul>
  </section>`;
}

function initTheme(): void {
  let saved: string | null = null;
  try {
    saved = localStorage.getItem("theme");
  } catch {
    /* ignore */
  }
  if (saved) document.documentElement.setAttribute("data-theme", saved);
  themeBtn.addEventListener("click", () => {
    const cur =
      document.documentElement.getAttribute("data-theme") === "dark"
        ? "light"
        : "dark";
    document.documentElement.setAttribute("data-theme", cur);
    try {
      localStorage.setItem("theme", cur);
    } catch {
      /* ignore */
    }
  });
}

async function boot(): Promise<void> {
  initTheme();
  window.addEventListener("hashchange", route);
  try {
    defaultBranch().then((b) => (branchEl.textContent = `@${b}`));
    files = await listStdlib();
    if (files.length === 0) {
      sidebar.innerHTML = `<div class="error">stdlib/ 下没有 .kv 文档</div>`;
      return;
    }
    route();
  } catch (e) {
    sidebar.innerHTML = errHtml(e);
    content.innerHTML = "";
  }
}

boot();
