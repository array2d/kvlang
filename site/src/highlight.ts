// kvlang 语法高亮：把一段 kvlang 源码转成带 <span class="tok-*"> 的 HTML。
// 输出整体包进 <code language="kvlang">。

const KEYWORDS = new Set([
  "lib",
  "rwfunc",
  "rwir",
  "defrwir",
  "defrwfunc",
  "while",
  "if",
  "else",
  "return",
  "and",
  "or",
  "not",
]);

const TYPES = new Set([
  "int8",
  "int16",
  "int32",
  "int64",
  "uint8",
  "uint16",
  "uint32",
  "uint64",
  "float32",
  "float64",
  "char",
  "utf8",
  "utf32",
  "bool",
]);

const LITERALS = new Set(["true", "false", "None"]);

function esc(s: string): string {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

const TOKEN =
  /("""[\s\S]*?"""|#[^\n]*|"(?:\\.|[^"\\])*"|\/[A-Za-z0-9_/·.\-]+|\b\d+(?:\.\d+)?\b|->|<-|·|[A-Za-z_][A-Za-z0-9_]*)/g;

export function highlightKv(code: string): string {
  let out = "";
  let last = 0;
  for (let m: RegExpExecArray | null; (m = TOKEN.exec(code)); ) {
    out += esc(code.slice(last, m.index));
    last = m.index + m[0].length;
    const t = m[0];
    let cls = "";
    if (t.startsWith("#")) cls = "tok-comment";
    else if (t.startsWith('"')) cls = "tok-string";
    else if (t.startsWith("/")) cls = "tok-path";
    else if (t === "->" || t === "<-" || t === "·") cls = "tok-op";
    else if (/^\d/.test(t)) cls = "tok-num";
    else if (KEYWORDS.has(t)) cls = "tok-kw";
    else if (TYPES.has(t)) cls = "tok-type";
    else if (LITERALS.has(t)) cls = "tok-lit";
    out += cls ? `<span class="${cls}">${esc(t)}</span>` : esc(t);
  }
  out += esc(code.slice(last));
  return `<code language="kvlang" class="kv-code">${out}</code>`;
}
