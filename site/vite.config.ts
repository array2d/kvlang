import { defineConfig, type Plugin } from "vite";
import { execSync } from "node:child_process";
import { readdirSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const SITE = fileURLToPath(new URL(".", import.meta.url));
const REPO = resolve(SITE, "..");

function git(cmd: string, fallback: string): string {
  try {
    return execSync(cmd, {
      cwd: REPO,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
  } catch {
    return fallback;
  }
}

function countFiles(dir: string, ext: string): number {
  let n = 0;
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, entry.name);
    if (entry.isDirectory()) n += countFiles(p, ext);
    else if (entry.name.endsWith(ext)) n++;
  }
  return n;
}

/** tutorial 总数 ≠ 带锚点的用例数：只有含「期望输出」注释的才被 test.py 当作可断言用例。 */
function countAnchoredTutorials(dir: string): number {
  let n = 0;
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, entry.name);
    if (entry.isDirectory()) n += countAnchoredTutorials(p);
    else if (entry.name.endsWith(".kv") && readFileSync(p, "utf8").includes("// 期望输出"))
      n++;
  }
  return n;
}

let cached: Record<string, string> | null = null;

function info(): Record<string, string> {
  if (cached) return cached;
  const version = git("git describe --tags --abbrev=0", "dev");
  const date =
    version === "dev" ? "dev" : git(`git log -1 --format=%cs ${version}`, "dev");
  cached = {
    VERSION: version,
    DATE: date,
    SPEC_COUNT: String(countFiles(join(REPO, "stdlib/kvlang/spec"), ".kv")),
    TUTORIAL_COUNT: String(countAnchoredTutorials(join(REPO, "tutorial"))),
  };
  return cached;
}

/** 构建期注入：版本、tag 日期、spec/教程数量。dev 下回退 dev。 */
function kvInfo(): Plugin {
  return {
    name: "kv-info",
    transformIndexHtml(html) {
      const map = info();
      return Object.entries(map).reduce(
        (s, [k, v]) => s.split(`{{${k}}}`).join(v),
        html,
      );
    },
  };
}

export default defineConfig({
  base: "/",
  plugins: [kvInfo()],
  build: {
    rollupOptions: {
      input: {
        main: resolve(SITE, "index.html"),
        zh: resolve(SITE, "zh/index.html"),
        stdlib: resolve(SITE, "stdlib/index.html"),
      },
    },
  },
});
