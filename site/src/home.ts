import "./tokens.css";
import "./home.css";

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
  
  const next = effectiveTheme() === "dark" ? "light" : "dark";
  root.setAttribute("data-theme", next);
  try {
    localStorage.setItem("theme", next);
  } catch {
    
  }
  syncThemeButton();
});

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
      
    }
  });
});
