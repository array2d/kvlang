// 侧边栏目录树：把扁平的 stdlib .kv 路径列表构造成嵌套树并渲染为导航。

import type { KvFile } from "./github";

interface Node {
  dirs: Map<string, Node>;
  files: { label: string; path: string }[];
}

function newNode(): Node {
  return { dirs: new Map(), files: [] };
}

function build(files: KvFile[]): Node {
  const root = newNode();
  for (const f of files) {
    const rel = f.path.replace(/^stdlib\//, "");
    const parts = rel.split("/");
    let node = root;
    for (let i = 0; i < parts.length - 1; i++) {
      const dir = parts[i];
      if (!node.dirs.has(dir)) node.dirs.set(dir, newNode());
      node = node.dirs.get(dir)!;
    }
    node.files.push({ label: parts[parts.length - 1], path: f.path });
  }
  return root;
}

function esc(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// 目录是否包含当前选中文件（决定 <details> 默认展开）。
function hasActive(node: Node, active: string): boolean {
  if (node.files.some((f) => f.path === active)) return true;
  for (const child of node.dirs.values())
    if (hasActive(child, active)) return true;
  return false;
}

function renderNode(node: Node, active: string): string {
  const parts: string[] = [];
  for (const [dir, child] of [...node.dirs.entries()].sort((a, b) =>
    a[0].localeCompare(b[0]),
  )) {
    const open = hasActive(child, active) ? " open" : "";
    parts.push(
      `<li class="dir"><details${open}><summary class="dir-name">${esc(
        dir,
      )}/</summary>${renderNode(child, active)}</details></li>`,
    );
  }
  for (const f of node.files) {
    const cls = f.path === active ? "file active" : "file";
    parts.push(
      `<li class="${cls}"><a href="#/${esc(f.path)}">${esc(f.label)}</a></li>`,
    );
  }
  return `<ul>${parts.join("")}</ul>`;
}

export function renderSidebar(files: KvFile[], active: string): string {
  return renderNode(build(files), active);
}
