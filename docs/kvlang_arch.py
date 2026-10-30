#!/usr/bin/env python3
"""SVG generator for the kvlang ecosystem architecture figure (English and Chinese).

The README embeds the PNG renders; this script is their source. Text is laid out with the
real advance widths of the fonts below, so the SVG must be rendered with the same fonts:

    Inter 4.1            https://github.com/rsms/inter/releases
    JetBrains Mono 2.304 https://github.com/JetBrains/JetBrainsMono/releases
    Noto Sans SC         https://github.com/notofonts/noto-cjk (Sans/SubsetOTF/SC)

Run from the repository root with the font files in one directory:

    for l in en zh; do
        python3 docs/kvlang_arch.py --fonts ~/fonts --lang $l --output /tmp/kvlang-architecture.$l.svg
        resvg --zoom 2 --skip-system-fonts --use-fonts-dir ~/fonts \
            /tmp/kvlang-architecture.$l.svg docs/kvlang-architecture.$l.png
    done

Only the PNGs are committed.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

W, H = 1280, 800

INK, SUB, MUTED, LINE, CANVAS, EDGE = "#0F172A", "#475569", "#64748B", "#E2E8F0", "#F8FAFC", "#94A3B8"
BLUE = {"bg": "#EFF6FF", "stroke": "#BFDBFE", "title": "#1E3A8A", "text": "#3B5B9A", "chip": "#1D4ED8"}
VIOLET = {"bg": "#F5F3FF", "stroke": "#DDD6FE", "title": "#4C1D95", "text": "#6D28D9", "chip": "#6D28D9"}
TEAL = {"bg": "#F0FDFA", "stroke": "#99F6E4", "title": "#134E4A", "text": "#0F766E", "chip": "#0F766E"}
# GitHub linguist colours, so the language dots read the way they do on GitHub.
LANG = {"Rust": "#DEA584", "C": "#555555", "C11": "#555555", "Go": "#00ADD8", "Python": "#3572A5"}

TEXT = {
    "en": {
        "title": "kvlang ecosystem",
        "leg_link": "linked via the C ABI", "leg_handoff": "handoff through the KV queue", "leg_planned": "planned",
        "host_desc": "reference host · links layout + runtime-c · routes the rest",
        "layout_1": "o0 compiler", "layout_2": "scan → parse → lower → /lib",
        "rt_1": "core interpreter", "rt_2": "fetch · decode · execute",
        "stage_top": "/lib", "stage_bottom": "instruction tree",
        "inproc": "in-process",
        "ext_title": "Extension runtimes", "ext_desc": "separate processes",
        "json_1": "json·to · json·from", "numpy_1": "tensor operators",
        "abi": "C ABI", "handoff": "handoff",
        "kv_desc": "addressing space = memory space",
        "kv_domains": "one KV tree · three fixed domains", "kv_queue": "handoff queue",
        "kv_abi_1": "kvspaceGet · kvspaceWriteInPlace · …", "kv_abi_2": "one PascalCase C ABI",
        "dsn": "chosen at run time by DSN · backends byte-identical",
        "kvc_1": "shared memory", "kvd_1": "durable storage",
        "redis": "Redis", "fs": "file system", "s3": "S3 / TiKV", "planned": "planned",
        "libs": "LIBRARIES", "storage": "STORAGE",
    },
    "zh": {
        "title": "kvlang 生态架构",
        "leg_link": "经 C ABI 链接", "leg_handoff": "经 KV 队列 handoff", "leg_planned": "规划中",
        "host_desc": "参考宿主 · 链接 layout 与 runtime-c · 路由其余操作码",
        "layout_1": "o0 编译器", "layout_2": "scan → parse → lower → /lib",
        "rt_1": "核心解释器", "rt_2": "取指 · 译码 · 执行",
        "stage_top": "/lib", "stage_bottom": "指令树",
        "inproc": "进程内",
        "ext_title": "扩展 runtime", "ext_desc": "独立进程",
        "json_1": "json·to · json·from", "numpy_1": "tensor 算子",
        "abi": "C ABI", "handoff": "handoff",
        "kv_desc": "寻址空间 = 内存空间",
        "kv_domains": "一棵 KV 树 · 三个固定域", "kv_queue": "handoff 队列",
        "kv_abi_1": "kvspaceGet · kvspaceWriteInPlace · …", "kv_abi_2": "一套 PascalCase C ABI",
        "dsn": "运行时按 DSN 选择 · 各后端逐字节一致",
        "kvc_1": "共享内存", "kvd_1": "持久化存储",
        "redis": "Redis", "fs": "文件系统", "s3": "S3 / TiKV", "planned": "规划中",
        "libs": "依赖库", "storage": "存储后端",
    },
}


class Font:
    """Advance widths from a TrueType/OpenType file (cmap formats 4 and 12, hmtx)."""

    def __init__(self, path: Path):
        data = path.read_bytes()
        tables = {}
        for i in range(struct.unpack(">H", data[4:6])[0]):
            tag, _, off, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
            tables[tag.decode()] = (off, length)
        self.upm = struct.unpack(">H", data[tables["head"][0] + 18:tables["head"][0] + 20])[0]
        n_hm = struct.unpack(">H", data[tables["hhea"][0] + 34:tables["hhea"][0] + 36])[0]
        hm = tables["hmtx"][0]
        self.adv = [struct.unpack(">H", data[hm + 4 * i:hm + 4 * i + 2])[0] for i in range(n_hm)]
        self.cmap = self._cmap(data, tables["cmap"][0])

    @staticmethod
    def _cmap(data: bytes, base: int) -> dict[int, int]:
        best = None
        for i in range(struct.unpack(">H", data[base + 2:base + 4])[0]):
            pid, eid, off = struct.unpack(">HHI", data[base + 4 + 8 * i:base + 12 + 8 * i])
            fmt = struct.unpack(">H", data[base + off:base + off + 2])[0]
            if pid == 3 and fmt == 12:
                best = (12, base + off)
                break
            if pid == 3 and eid == 1 and fmt == 4:
                best = (4, base + off)
        fmt, at = best
        out: dict[int, int] = {}
        if fmt == 12:
            for g in range(struct.unpack(">I", data[at + 12:at + 16])[0]):
                start, end, gid = struct.unpack(">III", data[at + 16 + 12 * g:at + 28 + 12 * g])
                for cp in range(start, end + 1):
                    out[cp] = gid + cp - start
            return out
        seg = struct.unpack(">H", data[at + 6:at + 8])[0] // 2
        ends = struct.unpack(f">{seg}H", data[at + 14:at + 14 + 2 * seg])
        p = at + 16 + 2 * seg
        starts = struct.unpack(f">{seg}H", data[p:p + 2 * seg])
        deltas = struct.unpack(f">{seg}h", data[p + 2 * seg:p + 4 * seg])
        ro_at = p + 4 * seg
        ros = struct.unpack(f">{seg}H", data[ro_at:ro_at + 2 * seg])
        for k in range(seg):
            for cp in range(starts[k], ends[k] + 1):
                if cp == 0xFFFF:
                    continue
                if ros[k] == 0:
                    gid = (cp + deltas[k]) & 0xFFFF
                else:
                    gp = ro_at + 2 * k + ros[k] + 2 * (cp - starts[k])
                    gid = struct.unpack(">H", data[gp:gp + 2])[0]
                    gid = (gid + deltas[k]) & 0xFFFF if gid else 0
                out[cp] = gid
        return out

    def width(self, ch: str, size: float) -> float | None:
        gid = self.cmap.get(ord(ch))
        if not gid:
            return None
        return self.adv[min(gid, len(self.adv) - 1)] * size / self.upm


class Fonts:
    def __init__(self, d: Path):
        self.sans = {400: Font(d / "Inter-Regular.ttf"), 500: Font(d / "Inter-Medium.ttf"),
                     600: Font(d / "Inter-SemiBold.ttf"), 700: Font(d / "Inter-Bold.ttf")}
        self.mono = {400: Font(d / "JetBrainsMono-Regular.ttf"), 500: Font(d / "JetBrainsMono-Medium.ttf")}
        self.cjk = {400: Font(d / "NotoSansSC-Regular.otf"), 500: Font(d / "NotoSansSC-Medium.otf"),
                    600: Font(d / "NotoSansSC-Medium.otf"), 700: Font(d / "NotoSansSC-Bold.otf")}

    def runs(self, text: str, mono: bool = False) -> list[tuple[str, str]]:
        """Split text into (family, chars) runs: CJK in Noto Sans SC, the rest in Inter or JetBrains Mono.

        Choosing the family per run, rather than leaving fallback to the renderer, keeps CJK at the
        requested weight and keeps Latin punctuation such as the middle dot at its Latin width.
        """
        out: list[tuple[str, str]] = []
        for ch in text:
            fam = "cjk" if is_cjk(ch) else ("mono" if mono else "sans")
            if out and out[-1][0] == fam:
                out[-1] = (fam, out[-1][1] + ch)
            else:
                out.append((fam, ch))
        return out

    def font(self, fam: str, weight: int) -> Font:
        return {"sans": self.sans, "cjk": self.cjk}[fam][weight] if fam != "mono" else self.mono[min(weight, 500)]

    def width(self, text: str, size: float, weight: int = 400, mono: bool = False) -> float:
        total = 0.0
        for fam, chars in self.runs(text, mono):
            f = self.font(fam, weight)
            total += sum(f.width(ch, size) or size * 0.5 for ch in chars)
        return total


def is_cjk(ch: str) -> bool:
    cp = ord(ch)
    return 0x2E80 <= cp <= 0x9FFF or 0xF900 <= cp <= 0xFAFF or 0xFF00 <= cp <= 0xFFEF or 0x3000 <= cp <= 0x303F


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Figure:
    def __init__(self, fonts: Fonts, lang: str):
        self.f, self.t, self.out, self.markers = fonts, TEXT[lang], [], set()

    def add(self, s: str) -> None:
        self.out.append(s)

    def text(self, x, y, s, size=13, weight=400, color=INK, anchor="start", mono=False, spacing=0.0) -> float:
        """Draw text with its baseline at y; return its width."""
        w = self.f.width(s, size, weight, mono) + spacing * max(len(s) - 1, 0)
        sx = x - w / 2 if anchor == "middle" else (x - w if anchor == "end" else x)
        ls = f' letter-spacing="{spacing}"' if spacing else ""
        family = {"sans": "Inter", "mono": "JetBrains Mono", "cjk": "Noto Sans SC"}
        # One <text> per run: resvg renders a Latin tspan bold when a CJK tspan shares its <text>.
        for fam, chars in self.f.runs(s, mono):
            self.add(f'<text x="{sx:.1f}" y="{y:.1f}" font-family="{family[fam]}" font-size="{size}" '
                     f'font-weight="{weight}" fill="{color}"{ls} xml:space="preserve">{esc(chars)}</text>')
            sx += self.f.width(chars, size, weight, mono) + spacing * len(chars)
        return w

    def rect(self, x, y, w, h, r=12, fill="#FFFFFF", stroke=LINE, sw=1.0, shadow=False, dash=None):
        f = ' filter="url(#shadow)"' if shadow else ""
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" '
                 f'stroke-width="{sw}"{d}{f}/>')

    def pill(self, x, y, lang) -> float:
        """Language pill with its left edge at x and vertical centre at y; return its width."""
        w = self.f.width(lang, 11.5, 500) + 30
        self.rect(x, y - 10, w, 20, r=10, fill="#FFFFFF", stroke=LINE)
        self.add(f'<circle cx="{x + 11}" cy="{y}" r="4" fill="{LANG[lang]}"/>')
        self.text(x + 20, y + 4, lang, 11.5, 500, SUB)
        return w

    def chip(self, x, y, s, color, stroke, size=12.5, mono=True, dash=None, h=26) -> float:
        w = self.f.width(s, size, 400, mono) + 22
        self.rect(x, y, w, h, r=h / 2, fill="#FFFFFF", stroke=stroke, dash=dash)
        self.text(x + w / 2, y + h / 2 + size * 0.36, s, size, 400, color, "middle", mono)
        return w

    def card(self, x, y, w, h, title, lang, lines, shadow=True):
        """White card: title + language pill on the first row, then text rows."""
        self.rect(x, y, w, h, shadow=shadow)
        top = y + h / 2 - (22 + 22 * len(lines)) / 2 + 16
        tw = self.text(x + 20, top, title, 16, 600, INK)
        if lang:
            self.pill(x + 20 + tw + 10, top - 5.5, lang)
        for i, (s, mono) in enumerate(lines):
            self.text(x + 20, top + 24 + 22 * i, s, 12.5 if mono else 13, 400, SUB, mono=mono)

    def arrow(self, pts, color=EDGE, dash=None, head=True):
        d = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts)
        da = f' stroke-dasharray="{dash}"' if dash else ""
        mk = f' marker-end="url(#arrow-{color[1:]})"' if head else ""
        self.markers.add(color)
        self.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="1.5" stroke-linecap="round"'
                 f' stroke-linejoin="round"{da}{mk}/>')

    def label(self, x, y, s, color=MUTED, size=11.5, mono=False, bg=CANVAS):
        """Small label centred on (x, y) with a background that interrupts the line behind it."""
        w = self.f.width(s, size, 500, mono) + 12
        self.add(f'<rect x="{x - w / 2:.1f}" y="{y - 9}" width="{w:.1f}" height="18" rx="9" fill="{bg}"/>')
        self.text(x, y + size * 0.36, s, size, 500, color, "middle", mono)

    def cylinder(self, x, y, w, h, s, dash=None, color=SUB, stroke="#CBD5E1", dy=0):
        ry = 7
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<path d="M{x} {y + ry} A{w / 2} {ry} 0 0 1 {x + w} {y + ry} V{y + h - ry} '
                 f'A{w / 2} {ry} 0 0 1 {x} {y + h - ry} Z" fill="#FFFFFF" stroke="{stroke}"{d} filter="url(#shadow)"/>')
        self.add(f'<path d="M{x} {y + ry} A{w / 2} {ry} 0 0 0 {x + w} {y + ry}" fill="none" stroke="{stroke}"{d}/>')
        self.text(x + w / 2, y + h / 2 + 9 + dy, s, 13, 500, color, "middle")

    def build(self) -> str:
        t = self.t
        self.rect(0.5, 0.5, W - 1, H - 1, r=20, fill=CANVAS, stroke=LINE)
        self.text(40, 52, t["title"], 22, 700, INK)

        # Legend, right-aligned on the title row.
        x = W - 40
        for key, color, dash in (("leg_planned", "#CBD5E1", "2 4"), ("leg_handoff", EDGE, "6 4"), ("leg_link", EDGE, None)):
            w = self.f.width(t[key], 12, 400)
            self.text(x, 50, t[key], 12, 400, MUTED, "end")
            self.arrow([(x - w - 40, 46), (x - w - 10, 46)], color=color, dash=dash)
            x -= w + 40 + 24

        # Host runtime: byteseek links layout and runtime-c.
        hx, hy, hw, hh = 40, 104, 720, 260
        self.rect(hx, hy, hw, hh, r=18, fill=BLUE["bg"], stroke=BLUE["stroke"], sw=1.5)
        tw = self.text(hx + 24, hy + 36, "byteseek", 19, 700, BLUE["title"])
        pw = self.pill(hx + 24 + tw + 10, hy + 30, "Rust")
        self.text(hx + 24 + tw + 10 + pw + 12, hy + 35, t["host_desc"], 13, 400, BLUE["text"])
        cy, ch, cw = hy + 58, 112, 290
        self.card(hx + 24, cy, cw, ch, "layout", "Rust", [(t["layout_1"], False), (t["layout_2"], True)])
        self.card(hx + hw - 24 - cw, cy, cw, ch, "runtime-c", "C11", [(t["rt_1"], False), (t["rt_2"], False)])
        ax0, ax1, ay = hx + 24 + cw + 10, hx + hw - 24 - cw - 10, cy + ch / 2
        self.arrow([(ax0, ay), (ax1, ay)], color="#60A5FA")
        self.text((ax0 + ax1) / 2, ay - 9, t["stage_top"], 12.5, 500, BLUE["chip"], "middle", mono=True)
        self.text((ax0 + ax1) / 2, ay + 20, t["stage_bottom"], 11.5, 400, BLUE["text"], "middle")
        sy = cy + ch + 18
        lw = self.text(hx + 24, sy + 18, t["inproc"], 12.5, 500, BLUE["text"])
        x = hx + 24 + lw + 14
        for fam in ("term", "json", "http", "networld", "kvlang·*"):
            x += self.chip(x, sy, fam, BLUE["chip"], BLUE["stroke"]) + 8

        # Extension runtimes in separate processes.
        ex, ew = 800, 440
        self.rect(ex, hy, ew, hh, r=18, fill=VIOLET["bg"], stroke=VIOLET["stroke"], sw=1.5)
        tw = self.text(ex + 24, hy + 36, t["ext_title"], 19, 700, VIOLET["title"])
        self.text(ex + 24 + tw + 12, hy + 35, t["ext_desc"], 13, 400, VIOLET["text"])
        self.card(ex + 24, cy, ew - 48, 84, "json-rwirext", "Go", [(t["json_1"], True)])
        self.card(ex + 24, cy + 100, ew - 48, 84, "numpy / op-gpu", "Python", [(t["numpy_1"], False)])

        # kvspace: the one store every runtime talks to.
        ky, kh = 428, 96
        bot = hy + hh
        for x0 in (hx + 24 + cw / 2, hx + hw - 24 - cw / 2, ex + ew - 120):
            self.arrow([(x0, bot), (x0, ky - 2)])
            self.label(x0, (bot + ky) / 2, t["abi"])
        qx, qw = 655, 250
        for x0 in (700, 860):
            self.arrow([(x0, bot), (x0, ky + 16)], dash="6 4")
        self.label(780, (bot + ky) / 2, t["handoff"])
        self.rect(40, ky, 1200, kh, r=18, fill=TEAL["bg"], stroke=TEAL["stroke"], sw=1.5)
        self.text(64, ky + 44, "kvspace", 24, 700, TEAL["title"])
        self.text(64, ky + 70, t["kv_desc"], 13, 400, TEAL["text"])
        x = 340
        for dom in ("/lib", "/vthread", "/networld"):
            x += self.chip(x, ky + 20, dom, TEAL["chip"], TEAL["stroke"], size=13) + 10
        self.text(340, ky + 72, t["kv_domains"], 12, 400, TEAL["text"])
        qw = self.f.width("/lib/<op>/vids/<vtid> = pc", 12.5, 400, True) + 22
        self.chip(780 - qw / 2, ky + 20, "/lib/<op>/vids/<vtid> = pc", TEAL["chip"], TEAL["stroke"], dash="5 3")
        self.text(780, ky + 72, t["kv_queue"], 12, 400, TEAL["text"], "middle")
        self.text(1216, ky + 38, t["kv_abi_1"], 12.5, 400, TEAL["title"], "end", mono=True)
        self.text(1216, ky + 72, t["kv_abi_2"], 12, 400, TEAL["text"], "end")

        # Implementations, selected by DSN.
        iy, ih = 564, 76
        for x0 in (330, 950):
            self.arrow([(x0, ky + kh), (x0, iy - 2)])
        self.text(640, (ky + kh + iy) / 2 + 4, t["dsn"], 12, 400, MUTED, "middle")
        self.card(40, iy, 580, ih, "kvspace-c", "C", [(t["kvc_1"], False)])
        self.card(660, iy, 580, ih, "kvspace-durable", "Rust", [(t["kvd_1"], False)])
        for right, schemes in ((620, (("shm://", None),)), (1240, (("redis://", None), ("fs://", None), ("s3://", "4 3")))):
            x = right - 20
            for s, dash in reversed(schemes):
                w = self.f.width(s, 12.5, 400, True) + 22
                x -= w
                self.chip(x, iy + ih / 2 - 13, s, TEAL["chip"] if not dash else MUTED,
                          TEAL["stroke"] if not dash else "#CBD5E1", dash=dash)
                x -= 8

        # Dependencies and backends.
        by, bh = 704, 64
        self.text(64, by - 14, t["libs"], 11.5, 600, MUTED, spacing=0.6)
        self.text(684, by - 14, t["storage"], 11.5, 600, MUTED, spacing=0.6)
        for x0, w in ((64, 250), (346, 250)):
            self.arrow([(x0 + w / 2, iy + ih), (x0 + w / 2, by - 2)])
        self.card(64, by, 250, bh, "blockmalloc", "C", [])
        self.card(346, by, 250, bh, "slotsboxmalloc", "C", [])
        for x0, w, key in ((684, 160, "redis"), (870, 160, "fs")):
            self.arrow([(x0 + w / 2, iy + ih), (x0 + w / 2, by - 2)])
            self.cylinder(x0, by, w, bh, t[key])
        sx, sw = 1056, 160
        self.arrow([(sx + sw / 2, iy + ih), (sx + sw / 2, by - 2)], color="#CBD5E1", dash="2 4")
        self.cylinder(sx, by, sw, bh, t["s3"], dash="4 3", color=MUTED, dy=-5)
        self.text(sx + sw / 2, by + bh - 10, t["planned"], 11, 400, MUTED, "middle")

        defs = (
            '<defs>'
            '<filter id="shadow" x="-10%" y="-20%" width="120%" height="150%">'
            '<feDropShadow dx="0" dy="1" stdDeviation="1" flood-color="#0F172A" flood-opacity="0.06"/>'
            '<feDropShadow dx="0" dy="4" stdDeviation="6" flood-color="#0F172A" flood-opacity="0.05"/>'
            '</filter>'
            + "".join(
                f'<marker id="arrow-{c[1:]}" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" '
                f'orient="auto-start-reverse"><path d="M1 1 L9 5 L1 9" fill="none" stroke="{c}" stroke-width="1.6" '
                'stroke-linecap="round" stroke-linejoin="round"/></marker>'
                for c in sorted(self.markers))
            + '</defs>'
        )
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
                + defs + "".join(self.out) + "</svg>\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--fonts", type=Path, required=True, help="directory with the font files listed above")
    ap.add_argument("--lang", choices=sorted(TEXT), default="en")
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    a.output.write_text(Figure(Fonts(a.fonts.expanduser()), a.lang).build(), encoding="utf-8")


if __name__ == "__main__":
    main()
