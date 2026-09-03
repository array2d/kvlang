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
  sidebar.innerHTML = renderSidebar(files, path);
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
    sidebar.innerHTML = renderSidebar(files, "");
    content.innerHTML = `<article class="doc"><h1 class="doc-title">kvlang 标准库文档</h1>
      <p>左侧选择一个 <code class="inline">.kv</code> 文档查看。内容实时取自
      <code class="inline">array2d/kvlang</code> 仓库 <code class="inline">stdlib/</code> 目录。</p></article>`;
  }
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
