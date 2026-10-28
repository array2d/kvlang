#!/usr/bin/env python3
"""Parametric SVG generator for the kvlang k-v mark.

The geometry is described by six points and five lines:

    l14: p1 -> p4   left stem
    l23: p2 -> p3   tapered upper arm
    l35: p3 -> p5   tapered lower arm
    l46: p4 -> p6   left side of v
    l56: p5 -> p6   right side of v

Run from the repository root:

    python3 docs/kvlang_mark.py --output docs/kvlang-mark-generated.svg
    python3 docs/kvlang_mark.py --print-geometry
"""

from __future__ import annotations

import argparse
import csv
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Point:
    """A structural midpoint with a horizontal left-right extent."""

    name: str
    x: float
    y: float
    width: float


@dataclass(frozen=True)
class Line:
    name: str
    start: str
    end: str


@dataclass(frozen=True)
class MarkParams:
    """Parameters that define the complete mark.

    Args:
        p14_width:
            Width of the horizontal terminals at p1 and p4. This is also the
            width of the vertical stem and the arm width where the chevron
            joins the stem.
        p25_width:
            Width of the horizontal terminals at p2 and p5. A value smaller
            than ``p14_width`` makes the k arms taper toward their ends.
        p3_width:
            Width of the horizontal terminal at p3. Defaults to ``p14_width``
            so the two k arms begin at the same width as the stem.
        p6_width:
            Width of the horizontal terminal at p6. Defaults to ``p14_width``
            so p6 matches p1 and p4. A positive value prevents the two sides
            of the v from collapsing into a zero-width point.
        k_height_ratio:
            K height divided by the horizontal distance p4--p5.
        v_aspect_ratio:
            V height divided by the horizontal distance p4--p5.
        background_color, k_color, v_color:
            Optional explicit SVG fill colors. When background_color or
            v_color is omitted, it is derived from base_color.
        base_color:
            Shared RGB source for the default background and V colors.
        background_transparency, v_transparency:
            Transparency where 0 is opaque and 1 is fully transparent. Default
            tones are composited over white independently, so the more opaque
            background is darker than the lighter V.
        canvas_size:
            Width and height of the square SVG canvas.
        span_ratio:
            Horizontal distance p4--p5 divided by ``canvas_size``.
        logo_scale:
            Uniform scale for the mark inside the canvas. Values above 1 zoom
            the mark and reduce its outer margins without changing ratios.
        fit_padding:
            Minimum margin, in SVG units, retained around the rendered mark.
            If the requested scale exceeds the available space, the geometry is
            reduced uniformly around the canvas center until it fits.
    """

    p14_width: float = 48.0
    p25_width: float = 44.0
    p3_width: float | None = None
    p6_width: float | None = None
    k_height_ratio: float = 0.74
    v_aspect_ratio: float = 0.58
    background_color: str | None = None
    k_color: str = "#111111"
    v_color: str | None = None
    base_color: str = "#6F7883"
    background_transparency: float = 0.2
    v_transparency: float = 0.5
    canvas_size: int = 512
    span_ratio: float = 0.45
    logo_scale: float = 1.45
    fit_padding: float = 6.0

    def validate(self) -> None:
        if self.canvas_size <= 0:
            raise ValueError("canvas_size must be positive")
        if self.p14_width <= 0 or self.p25_width <= 0:
            raise ValueError("p14_width and p25_width must be positive")
        if self.p6_width is not None and self.p6_width < 0:
            raise ValueError("p6_width must be non-negative")
        if self.p3_width is not None and self.p3_width <= 0:
            raise ValueError("p3_width must be positive when specified")
        if not 0 <= self.background_transparency <= 1:
            raise ValueError("background_transparency must be between 0 and 1")
        if not 0 <= self.v_transparency <= 1:
            raise ValueError("v_transparency must be between 0 and 1")
        if self.k_height_ratio <= 0 or self.v_aspect_ratio <= 0:
            raise ValueError("height ratios must be positive")
        if not 0 < self.span_ratio < 1:
            raise ValueError("span_ratio must be between 0 and 1")
        if self.logo_scale <= 0:
            raise ValueError("logo_scale must be positive")
        if self.fit_padding < 0 or self.fit_padding * 2 >= self.canvas_size:
            raise ValueError("fit_padding must leave a positive drawable area")


@dataclass(frozen=True)
class MarkGeometry:
    canvas_size: int
    span: float
    k_height: float
    v_height: float
    points: dict[str, Point]
    lines: tuple[Line, ...]

    def to_dict(self) -> dict:
        return {
            "canvas_size": self.canvas_size,
            "span": self.span,
            "k_height": self.k_height,
            "v_height": self.v_height,
            "points": {name: asdict(point) for name, point in self.points.items()},
            "lines": [asdict(line) for line in self.lines],
        }


def build_geometry(params: MarkParams) -> MarkGeometry:
    """Create the six-point, five-line geometry from the high-level params.

    The mark is centered on the canvas. P1/P4 and P2/P5 share their respective
    x coordinates. P3 is centered on the stem at the K midpoint. P6 is centered
    between the two V terminals. Every point has a horizontal width.
    """

    params.validate()
    size = params.canvas_size
    span = size * params.span_ratio * params.logo_scale
    k_height = span * params.k_height_ratio
    v_height = span * params.v_aspect_ratio
    total_height = k_height + v_height
    top = (size - total_height) / 2.0
    center_x = size / 2.0
    left_x = center_x - span / 2.0
    right_x = center_x + span / 2.0
    connection_y = top + k_height

    p3_width = params.p3_width if params.p3_width is not None else params.p14_width
    p6_width = params.p6_width if params.p6_width is not None else params.p14_width
    scale = params.logo_scale
    p1 = Point("p1", left_x, top, params.p14_width * scale)
    p2 = Point("p2", right_x, top, params.p25_width * scale)
    p3 = Point("p3", left_x, top + k_height / 2.0, p3_width * scale)
    p4 = Point("p4", left_x, connection_y, params.p14_width * scale)
    p5 = Point("p5", right_x, connection_y, params.p25_width * scale)
    p6 = Point("p6", center_x, connection_y + v_height, p6_width * scale)

    points = {point.name: point for point in (p1, p2, p3, p4, p5, p6)}
    half_width = max(
        max(
            center_x - (point.x - point.width / 2.0),
            point.x + point.width / 2.0 - center_x,
        )
        for point in points.values()
    )
    half_height = max(
        max(size / 2.0 - point.y, point.y - size / 2.0)
        for point in points.values()
    )
    available_half = size / 2.0 - params.fit_padding
    fit = min(1.0, available_half / half_width, available_half / half_height)
    if fit < 1.0:
        points = {
            name: Point(
                name,
                center_x + (point.x - center_x) * fit,
                size / 2.0 + (point.y - size / 2.0) * fit,
                point.width * fit,
            )
            for name, point in points.items()
        }
        span *= fit
        k_height *= fit
        v_height *= fit

    lines = (
        Line("l14", "p1", "p4"),
        Line("l23", "p2", "p3"),
        Line("l35", "p3", "p5"),
        Line("l46", "p4", "p6"),
        Line("l56", "p5", "p6"),
    )
    return MarkGeometry(size, span, k_height, v_height, points, lines)


def _format_number(value: float) -> str:
    rounded = round(value, 4)
    if rounded == int(rounded):
        return str(int(rounded))
    return f"{rounded:.4f}".rstrip("0").rstrip(".")


def _parse_hex_color(color: str) -> tuple[int, int, int]:
    value = color.removeprefix("#")
    if len(value) != 6:
        raise ValueError(f"expected a six-digit hex color, got {color!r}")
    return tuple(int(value[index : index + 2], 16) for index in (0, 2, 4))


def composite_over_white(color: str, transparency: float) -> str:
    """Resolve an RGB + transparency color against a white backdrop."""

    if not 0 <= transparency <= 1:
        raise ValueError("transparency must be between 0 and 1")
    alpha = 1.0 - transparency
    channels = (
        round(alpha * channel + (1.0 - alpha) * 255)
        for channel in _parse_hex_color(color)
    )
    return "#" + "".join(f"{channel:02X}" for channel in channels)


def effective_colors(params: MarkParams) -> tuple[str, str, str]:
    """Return effective background, K, and V fill colors."""

    background = (
        params.background_color
        if params.background_color is not None
        else composite_over_white(params.base_color, params.background_transparency)
    )
    value = (
        params.v_color
        if params.v_color is not None
        else composite_over_white(params.base_color, params.v_transparency)
    )
    return background, params.k_color, value


def horizontal_span(point: Point) -> tuple[tuple[float, float], tuple[float, float]]:
    """Return the left and right endpoints of a point's horizontal width."""

    half = point.width / 2.0
    return (point.x - half, point.y), (point.x + half, point.y)


def _segments_cross(
    a: tuple[float, float],
    b: tuple[float, float],
    c: tuple[float, float],
    d: tuple[float, float],
) -> bool:
    def orientation(
        p: tuple[float, float],
        q: tuple[float, float],
        r: tuple[float, float],
    ) -> float:
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])

    return (
        orientation(a, b, c) * orientation(a, b, d) < 0
        and orientation(c, d, a) * orientation(c, d, b) < 0
    )


def line_polygon(points: dict[str, Point], line: Line) -> tuple[tuple[float, float], ...]:
    """Return a quad connecting two horizontal point widths."""

    start = points[line.start]
    end = points[line.end]
    start_left, start_right = horizontal_span(start)
    end_left, end_right = horizontal_span(end)
    polygon = (
        start_left,
        start_right,
        end_right,
        end_left,
    )
    signed_area = sum(
        polygon[i][0] * polygon[(i + 1) % len(polygon)][1]
        - polygon[(i + 1) % len(polygon)][0] * polygon[i][1]
        for i in range(len(polygon))
    )
    if signed_area == 0:
        raise ValueError(f"{line.name} produces a zero-area polygon")
    if _segments_cross(polygon[0], polygon[1], polygon[2], polygon[3]):
        raise ValueError(f"{line.name} produces a self-intersecting polygon")
    return polygon if signed_area >= 0 else tuple(reversed(polygon))


def _polygons_path(polygons: Iterable[tuple[tuple[float, float], ...]]) -> str:
    commands: list[str] = []
    for polygon in polygons:
        first, *rest = polygon
        commands.append(f"M{_format_number(first[0])} {_format_number(first[1])}")
        commands.extend(
            f"L{_format_number(x)} {_format_number(y)}" for x, y in rest
        )
        commands.append("Z")
    return " ".join(commands)


def render_svg(params: MarkParams) -> str:
    """Render a complete standalone SVG string."""

    geometry = build_geometry(params)
    k_polygons = [
        line_polygon(geometry.points, line)
        for line in geometry.lines
        if line.name in {"l14", "l23", "l35"}
    ]
    v_polygons = [
        line_polygon(geometry.points, line)
        for line in geometry.lines
        if line.name in {"l46", "l56"}
    ]
    size = geometry.canvas_size
    background_color, k_color, v_color = effective_colors(params)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     width="{size}"
     height="{size}"
     viewBox="0 0 {size} {size}"
     role="img"
     aria-labelledby="title desc">
  <title id="title">kvlang k-v mark</title>
  <desc id="desc">A parametric six-point, five-line k-v mark.</desc>
  <rect width="{size}" height="{size}" fill="{background_color}"
        data-base-color="{params.base_color}"
        data-transparency="{params.background_transparency:g}"/>
  <path d="{_polygons_path(k_polygons)}"
        fill="{k_color}"
        fill-rule="nonzero"
        shape-rendering="geometricPrecision"/>
  <path d="{_polygons_path(v_polygons)}"
        fill="{v_color}"
        data-base-color="{params.base_color}"
        data-transparency="{params.v_transparency:g}"
        fill-rule="nonzero"
        shape-rendering="geometricPrecision"/>
</svg>
"""


def save_svg(path: str | Path, params: MarkParams) -> Path:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_svg(params), encoding="utf-8")
    return output


@dataclass(frozen=True)
class Example:
    filename: str
    title: str
    note: str
    params: MarkParams


EXAMPLES = (
    Example("01-standard.svg", "标准结构", "默认同色相深灰底", MarkParams()),
    Example(
        "02-tapered.svg",
        "明显收尖",
        "P2/P5 收窄到 22，K 的斜臂更轻",
        MarkParams(
            p14_width=34,
            p25_width=22,
            k_color="#1B1815",
            base_color="#6F6255",
            background_transparency=0.15,
            v_transparency=0.55,
        ),
    ),
    Example(
        "03-slate.svg",
        "深底加强",
        "略宽 K、浅色前景、钢灰 V",
        MarkParams(
            p14_width=38,
            p25_width=25,
            k_height_ratio=0.93,
            v_aspect_ratio=0.82,
            k_color="#F5F7F9",
            base_color="#0F1822",
            background_transparency=0.05,
            v_transparency=0.58,
        ),
    ),
    Example(
        "04-midnight-slim.svg",
        "细线午夜蓝",
        "更窄的线条、更高的 K 比例",
        MarkParams(
            p14_width=27,
            p25_width=17,
            k_height_ratio=1.02,
            v_aspect_ratio=0.75,
            k_color="#EEF4FF",
            base_color="#0E2B4A",
            background_transparency=0.03,
            v_transparency=0.56,
        ),
    ),
    Example(
        "05-forest-bold.svg",
        "森林粗体",
        "更厚的 K 与更深的 V",
        MarkParams(
            p14_width=43,
            p25_width=34,
            k_height_ratio=0.84,
            v_aspect_ratio=0.9,
            k_color="#F5F1E8",
            base_color="#315F52",
            background_transparency=0.18,
            v_transparency=0.58,
        ),
    ),
    Example(
        "06-clay-compact.svg",
        "黑橙加宽",
        "继续增加横向跨度、笔画宽度和扁平度",
        MarkParams(
            p14_width=56,
            p25_width=43,
            k_height_ratio=0.7,
            v_aspect_ratio=0.54,
            k_color="#1D1A18",
            base_color="#C86749",
            background_transparency=0.15,
            v_transparency=0.5,
            span_ratio=0.47,
        ),
    ),
    Example(
        "07-tall-v.svg",
        "高 V",
        "V 高宽比提升到 1.02，方向感更强",
        MarkParams(
            p14_width=34,
            p25_width=27,
            k_height_ratio=0.82,
            v_aspect_ratio=1.02,
            k_color="#F4F1EB",
            base_color="#8A929B",
            background_transparency=0.08,
            v_transparency=0.55,
        ),
    ),
    Example(
        "08-sage-light.svg",
        "轻盈鼠尾草",
        "细线、短 K、低对比度",
        MarkParams(
            p14_width=25,
            p25_width=15,
            k_height_ratio=0.92,
            v_aspect_ratio=0.72,
            k_color="#F5F2EA",
            base_color="#507563",
            background_transparency=0.18,
            v_transparency=0.6,
        ),
    ),
    Example(
        "09-black-orange-wide.svg",
        "黑橙加宽 · 轻",
        "增加整体横向比例和笔画宽度",
        MarkParams(
            p14_width=39,
            p25_width=30,
            p3_width=39,
            p6_width=39,
            k_height_ratio=0.8,
            v_aspect_ratio=0.62,
            k_color="#1D1A18",
            base_color="#C86749",
            background_transparency=0.15,
            v_transparency=0.5,
            span_ratio=0.425,
        ),
    ),
    Example(
        "10-black-orange-wider.svg",
        "黑橙加宽 · 中",
        "更宽的横向展开与更饱满的笔画",
        MarkParams(
            p14_width=45,
            p25_width=36,
            p3_width=45,
            p6_width=45,
            k_height_ratio=0.77,
            v_aspect_ratio=0.6,
            k_color="#1D1A18",
            base_color="#C86749",
            background_transparency=0.15,
            v_transparency=0.5,
            span_ratio=0.445,
        ),
    ),
    Example(
        "11-black-orange-widest.svg",
        "黑橙加宽 · 强",
        "明显更宽、更扁，适合作为大胆的展示版本",
        MarkParams(
            p14_width=51,
            p25_width=42,
            p3_width=51,
            p6_width=51,
            k_height_ratio=0.74,
            v_aspect_ratio=0.58,
            k_color="#1D1A18",
            base_color="#C86749",
            background_transparency=0.15,
            v_transparency=0.5,
            span_ratio=0.46,
        ),
    ),
    Example(
        "12-black-red-halfblack.svg",
        "黑底纯红 K 半黑 V",
        "黑色背景、纯红 K、50% 黑色 V",
        MarkParams(
            p14_width=48,
            p25_width=44,
            k_height_ratio=0.74,
            v_aspect_ratio=0.58,
            k_color="#FF0000",
            base_color="#000000",
            background_transparency=0.0,
            v_transparency=0.5,
            span_ratio=0.45,
        ),
    ),
    Example(
        "13-black-pink-halfblack.svg",
        "黑底粉 K 半黑 V",
        "黑色背景、亮粉色 K、50% 黑色 V",
        MarkParams(
            p14_width=48,
            p25_width=44,
            k_height_ratio=0.74,
            v_aspect_ratio=0.58,
            k_color="#FF00FF",
            base_color="#000000",
            background_transparency=0.0,
            v_transparency=0.5,
            span_ratio=0.45,
        ),
    ),
    Example(
        "14-black-green-halfblack.svg",
        "黑底绿 K 半黑 V",
        "黑色背景、亮绿色 K、50% 黑色 V",
        MarkParams(
            p14_width=48,
            p25_width=44,
            k_height_ratio=0.74,
            v_aspect_ratio=0.58,
            k_color="#00FF00",
            base_color="#000000",
            background_transparency=0.0,
            v_transparency=0.5,
            span_ratio=0.45,
        ),
    ),
    Example(
        "15-silver-skyblue-white.svg",
        "银白天蓝 K 白 V",
        "银白色背景、天蓝色 K、纯白色 V",
        MarkParams(
            p14_width=48,
            p25_width=44,
            k_height_ratio=0.74,
            v_aspect_ratio=0.58,
            background_color="#D9DEE6",
            k_color="#4FC3F7",
            v_color="#FFFFFF",
            span_ratio=0.45,
        ),
    ),
    Example(
        "16-white-skyblue-silver.svg",
        "白底亮蓝 K 银白 V",
        "纯白色背景、亮蓝色 K、银白色 V",
        MarkParams(
            p14_width=48,
            p25_width=44,
            k_height_ratio=0.74,
            v_aspect_ratio=0.58,
            background_color="#FFFFFF",
            k_color="#00A8FF",
            v_color="#D9DEE6",
            span_ratio=0.45,
        ),
    ),
)


def generate_examples(output_dir: str | Path) -> Path:
    """Generate all example SVGs and the single color/parameter CSV."""

    svg_dir = Path(output_dir)
    svg_dir.mkdir(parents=True, exist_ok=True)
    with (svg_dir / "examples.csv").open("w", encoding="utf-8", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "filename",
                "title",
                "p14_width",
                "p25_width",
                "p3_width",
                "p6_width",
                "k_height_ratio",
                "v_aspect_ratio",
                "span_ratio",
                "logo_scale",
                "fit_scale",
                "canvas_size",
                "background_rgb",
                "background_transparency",
                "v_rgb",
                "v_transparency",
                "background_effective",
                "v_effective",
                "k",
            ]
        )
        for example in EXAMPLES:
            geometry = build_geometry(example.params)
            background, k_color, v_color = effective_colors(example.params)
            background_rgb = (
                example.params.background_color or example.params.base_color
            )
            v_rgb = example.params.v_color or example.params.base_color
            background_transparency = (
                0.0
                if example.params.background_color is not None
                else example.params.background_transparency
            )
            v_transparency = (
                0.0
                if example.params.v_color is not None
                else example.params.v_transparency
            )
            save_svg(svg_dir / example.filename, example.params)
            writer.writerow(
                [
                    example.filename,
                    example.title,
                    geometry.points["p1"].width,
                    geometry.points["p2"].width,
                    geometry.points["p3"].width,
                    geometry.points["p6"].width,
                    example.params.k_height_ratio,
                    example.params.v_aspect_ratio,
                    example.params.span_ratio,
                    example.params.logo_scale,
                    geometry.span
                    / (example.params.canvas_size * example.params.span_ratio),
                    example.params.canvas_size,
                    background_rgb,
                    background_transparency,
                    v_rgb,
                    v_transparency,
                    background,
                    v_color,
                    k_color,
                ]
            )
    return svg_dir / "examples.csv"


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("kvlang-mark.svg"))
    parser.add_argument("--p14-width", type=float, default=48.0)
    parser.add_argument("--p25-width", type=float, default=44.0)
    parser.add_argument("--p3-width", type=float, default=None)
    parser.add_argument("--p6-width", type=float, default=None)
    parser.add_argument("--k-height-ratio", type=float, default=0.74)
    parser.add_argument("--v-aspect-ratio", type=float, default=0.58)
    parser.add_argument("--background", default=None)
    parser.add_argument("--k-color", default="#111111")
    parser.add_argument("--v-color", default=None)
    parser.add_argument("--base-color", default="#6F7883")
    parser.add_argument("--background-transparency", type=float, default=0.2)
    parser.add_argument("--v-transparency", type=float, default=0.5)
    parser.add_argument("--canvas-size", type=int, default=512)
    parser.add_argument("--span-ratio", type=float, default=0.45)
    parser.add_argument("--logo-scale", type=float, default=1.45)
    parser.add_argument("--fit-padding", type=float, default=6.0)
    parser.add_argument(
        "--print-geometry",
        action="store_true",
        help="Print the generated points and lines as JSON instead of writing SVG.",
    )
    parser.add_argument(
        "--generate-examples",
        action="store_true",
        help="Generate the full SVG example set and examples.csv.",
    )
    parser.add_argument(
        "--examples-dir",
        type=Path,
        default=Path(__file__).resolve().parent / "kvlang-mark-examples",
    )
    return parser.parse_args()


def main() -> None:
    args = _parse_args()
    if args.generate_examples:
        print(generate_examples(args.examples_dir))
        return
    params = MarkParams(
        p14_width=args.p14_width,
        p25_width=args.p25_width,
        p3_width=args.p3_width,
        p6_width=args.p6_width,
        k_height_ratio=args.k_height_ratio,
        v_aspect_ratio=args.v_aspect_ratio,
        background_color=args.background,
        k_color=args.k_color,
        v_color=args.v_color,
        base_color=args.base_color,
        background_transparency=args.background_transparency,
        v_transparency=args.v_transparency,
        canvas_size=args.canvas_size,
        span_ratio=args.span_ratio,
        logo_scale=args.logo_scale,
        fit_padding=args.fit_padding,
    )
    if args.print_geometry:
        print(json.dumps(build_geometry(params).to_dict(), indent=2, ensure_ascii=False))
        return
    output = save_svg(args.output, params)
    print(output)


if __name__ == "__main__":
    main()
