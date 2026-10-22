// GitHub API 客户端：运行时列出 stdlib/ 目录树并拉取 .kv 原文。
// 纯静态托管无法读目录，改由前端调用 GitHub REST API。
// 响应按内容 sha 缓存进 sessionStorage，缓解未认证 60 次/时 的限流。

const OWNER = "array2d";
const REPO = "kvlang";
const DIR = "stdlib";

export interface KvFile {
  path: string; // 仓库内完整路径，如 stdlib/time/duration.kv
  name: string; // 文件名，如 duration.kv
  sha: string;
}

export class RateLimitError extends Error {
  constructor() {
    super("GitHub API 请求受限（未认证 60 次/时）。请稍后重试。");
    this.name = "RateLimitError";
  }
}

function cacheGet(key: string): string | null {
  try {
    return sessionStorage.getItem(key);
  } catch {
    return null;
  }
}

function cacheSet(key: string, val: string): void {
  try {
    sessionStorage.setItem(key, val);
  } catch {
    /* 隐私模式/超额：静默降级 */
  }
}

async function api<T>(url: string): Promise<T> {
  const cached = cacheGet(url);
  if (cached !== null) return JSON.parse(cached) as T;
  const resp = await fetch(url, {
    headers: { Accept: "application/vnd.github+json" },
  });
  if (resp.status === 403 || resp.status === 429) throw new RateLimitError();
  if (!resp.ok) throw new Error(`GitHub API ${resp.status}: ${url}`);
  const text = await resp.text();
  cacheSet(url, text);
  return JSON.parse(text) as T;
}

let branchPromise: Promise<string> | null = null;

export function defaultBranch(): Promise<string> {
  if (!branchPromise) {
    branchPromise = api<{ default_branch: string }>(
      `https://api.github.com/repos/${OWNER}/${REPO}`,
    ).then((r) => r.default_branch);
  }
  return branchPromise;
}

export async function listStdlib(): Promise<KvFile[]> {
  const branch = await defaultBranch();
  const tree = await api<{ tree: { path: string; type: string; sha: string }[] }>(
    `https://api.github.com/repos/${OWNER}/${REPO}/git/trees/${branch}?recursive=1`,
  );
  return tree.tree
    .filter(
      (n) =>
        n.type === "blob" &&
        n.path.startsWith(`${DIR}/`) &&
        n.path.endsWith(".kv"),
    )
    .map((n) => ({
      path: n.path,
      name: n.path.slice(n.path.lastIndexOf("/") + 1),
      sha: n.sha,
    }))
    .sort((a, b) => a.path.localeCompare(b.path));
}

export async function fetchRaw(path: string, sha: string): Promise<string> {
  const key = `raw:${sha}`;
  const cached = cacheGet(key);
  if (cached !== null) return cached;
  const branch = await defaultBranch();
  const url = `https://raw.githubusercontent.com/${OWNER}/${REPO}/${branch}/${path}`;
  const resp = await fetch(url);
  if (resp.status === 403 || resp.status === 429) throw new RateLimitError();
  if (!resp.ok) throw new Error(`raw ${resp.status}: ${url}`);
  const text = await resp.text();
  cacheSet(key, text);
  return text;
}
