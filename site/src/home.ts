import "./tokens.css";
import "./home.css";
import { refreshBenchmarks } from "./benchmark/live";

const root = document.documentElement;
const themeBtn = document.getElementById("theme");
const isZh = root.lang.toLowerCase().startsWith("zh");

function saved(): string | null {
  try {
    return localStorage.getItem("theme");
  } catch {
    return null;
  }
}

function systemDark(): boolean {
  return matchMedia("(prefers-color-scheme: dark)").matches;
}

/** 当前实际生效的主题：显式 data-theme 优先，否则跟随系统。 */
function effectiveTheme(): "dark" | "light" {
  const attr = root.getAttribute("data-theme");
  if (attr === "dark" || attr === "light") return attr;
  return systemDark() ? "dark" : "light";
}

function syncThemeButton(): void {
  themeBtn?.setAttribute("aria-pressed", effectiveTheme() === "dark" ? "true" : "false");
}

const stored = saved();
if (stored === "dark" || stored === "light") root.setAttribute("data-theme", stored);
syncThemeButton();
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", syncThemeButton);

themeBtn?.addEventListener("click", () => {
  // 系统深色且无显式选择时，首次点击必须切到浅色。
  const next = effectiveTheme() === "dark" ? "light" : "dark";
  root.setAttribute("data-theme", next);
  try {
    localStorage.setItem("theme", next);
  } catch {
    /* 隐私模式下不可写，忽略 */
  }
  syncThemeButton();
});

// 复制安装命令。按钮用 data-copy 指向目标元素选择器。
void refreshBenchmarks();

const copiedLabel = isZh ? "已复制" : "Copied";
document.querySelectorAll<HTMLButtonElement>("[data-copy]").forEach((btn) => {
  btn.addEventListener("click", async () => {
    const target = document.querySelector(btn.dataset.copy ?? "");
    const text = target?.textContent?.trim() ?? "";
    const label = btn.textContent;
    try {
      await navigator.clipboard.writeText(text);
      btn.textContent = copiedLabel;
      setTimeout(() => (btn.textContent = label), 1200);
    } catch {
      /* 剪贴板被拒时保持原样，用户可手动选中 */
    }
  });
});
