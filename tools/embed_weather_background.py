"""Regenerate weather-viewer/src/weather_background_data_e1NNN.cpp for one model.

Pipeline per theme: LANCZOS upscale, Gaussian blur to melt the source into
continuous tone, unsharp mask to bring back edges, contrast auto-fix,
optional rotation or aspect-preserving crop, LANCZOS resize to the target
panel size, fade toward white so overlaid text stays legible, then
Floyd-Steinberg dither to the
model's target palette. Output is packed at the model's native bit depth
and dropped in a per-model PROGMEM cpp guarded with `#if RETERMINAL_MODEL`.

Each per-model file contains one payload array per supported theme plus a
pointer table `kThemeData[4]` indexed by the C++ `Theme` enum. On models
that only bundle the cloudy theme (E1003, E1004) every slot in the table
points at the sole cloudy payload, so the blitter can index by theme
unconditionally and callers can request any theme without a null check.

Regenerate a single model:
    python tools/embed_weather_background.py --model 1003
    python tools/embed_weather_background.py --model 1004
Regenerate everything:
    python tools/embed_weather_background.py --all
"""
from __future__ import annotations

import argparse
import os
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Callable, Dict, List, Tuple

from PIL import Image, ImageFilter, ImageOps

REPO = Path(__file__).resolve().parents[1]
ASSETS_DIR = REPO / "weather-viewer" / "assets"
OUT_DIR = REPO / "weather-viewer" / "src"

# Theme enum ordering. Matches Theme in weather_background.h - the pointer
# table emitted below is indexed directly by the enum value.
ALL_THEMES: Tuple[str, ...] = ("sunny", "cloudy", "rainy", "snowy")
CLOUDY_ONLY: Tuple[str, ...] = ("cloudy",)

# Fade the ink-wash toward white so headline text stays legible. 0.55 keeps
# ~45% of the original ink, plenty to still read as a landscape.
FADE_STRENGTH = 0.55
E1005_INK_DENSITY = 0.08


@dataclass(frozen=True)
class ModelSpec:
    model: int
    panel_w: int       # panel size in pixels
    panel_h: int
    rotate_source: bool  # rotate the landscape artwork 90 degrees before resize
    bpp: int           # bits per pixel in the packed payload
    levels: int        # number of dither levels (2, 4, ...); must match bpp mask
    scale: int         # blitter upscale factor (1 = 1:1, 2 = nearest-neighbor 2x2)
    themes: Tuple[str, ...]  # themes bundled for this model, in any order
    out_file: str
    fade_strength: float = FADE_STRENGTH
    crop_to_panel: bool = False
    target_ink_density: float | None = None

    @property
    def payload_w(self) -> int:
        return self.panel_w // self.scale

    @property
    def payload_h(self) -> int:
        return self.panel_h // self.scale

    @property
    def payload_bytes(self) -> int:
        return self.payload_w * self.payload_h * self.bpp // 8


MODELS: List[ModelSpec] = [
    # E1001 UC8179 4-gray landscape. All four themes fit comfortably.
    ModelSpec(1001, 800, 480, False, 2, 4, 1, ALL_THEMES,
              "weather_background_data_e1001.cpp"),
    # E1002 Spectra 6 landscape. 1bpp BW (Spectra 6 has no usable
    # intermediate grays for a gradient) - all four themes still small.
    ModelSpec(1002, 800, 480, False, 1, 2, 1, ALL_THEMES,
              "weather_background_data_e1002.cpp"),
    # E1003 16-gray 1872x1404. Quarter-res (scale=4, 468x351) 2bpp is
    # ~41 KB per theme, so all four fit in ~164 KB - same footprint as
    # the previous single-theme half-res payload.
    ModelSpec(1003, 1872, 1404, False, 2, 4, 4, ALL_THEMES,
              "weather_background_data_e1003.cpp"),
    # E1004 Spectra 6 1200x1600 portrait. Half-res (scale=2, 600x800)
    # 1bpp is ~60 KB per theme, so all four fit in ~240 KB - same
    # footprint as the previous single-theme full-res payload.
    ModelSpec(1004, 1200, 1600, True, 1, 2, 2, ALL_THEMES,
              "weather_background_data_e1004.cpp"),
    # E1005 SSD1677 monochrome portrait. Crop an upright 3:5 composition from
    # the landscape artwork instead of rotating the mountains on their side.
    # Normalizing the average ink keeps light themes visible without making
    # darker themes too busy behind the compact labels.
    ModelSpec(1005, 480, 800, False, 1, 2, 1, ALL_THEMES,
              "weather_background_data_e1005.cpp", crop_to_panel=True,
              target_ink_density=E1005_INK_DENSITY),
]


def refine(img: Image.Image, target_size: Tuple[int, int],
           fade_strength: float,
           target_ink_density: float | None = None) -> Image.Image:
    """Continuous-tone refinement + fade + resize into the target panel."""
    w, h = img.size
    sup = img.resize((w * 2, h * 2), Image.LANCZOS)
    sup = sup.filter(ImageFilter.GaussianBlur(radius=1.6))
    sup = sup.filter(ImageFilter.UnsharpMask(radius=1.5, percent=45, threshold=3))
    sup = ImageOps.autocontrast(sup, cutoff=0.5)
    sup = sup.resize(target_size, Image.LANCZOS)
    white = Image.new("L", sup.size, 255)
    if target_ink_density is not None:
        assert 0.0 < target_ink_density < 1.0
        histogram = sup.histogram()
        mean = sum(level * count for level, count in enumerate(histogram))
        mean /= sup.width * sup.height
        target_mean = 255 * (1.0 - target_ink_density)
        if mean >= target_mean:
            return sup
        fade_strength = (target_mean - mean) / (255 - mean)
    return Image.blend(sup, white, fade_strength)


def dither_to_palette(refined: Image.Image, levels: int) -> Image.Image:
    """Floyd-Steinberg dither to `levels` evenly-spaced gray levels."""
    assert 2 <= levels <= 256
    palette_entries: List[int] = []
    for i in range(levels):
        v = round(i * 255 / (levels - 1)) if levels > 1 else 0
        palette_entries.extend([v, v, v])
    palette_entries += [0] * (256 * 3 - len(palette_entries))
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette(palette_entries)
    return refined.convert("RGB").quantize(
        colors=levels, palette=pal_img, dither=Image.FLOYDSTEINBERG
    )


def crop_to_aspect(img: Image.Image,
                   target_size: Tuple[int, int]) -> Image.Image:
    """Center-crop without rotating or distorting the source composition."""
    target_w, target_h = target_size
    target_ratio = target_w / target_h
    source_ratio = img.width / img.height
    if source_ratio > target_ratio:
        crop_w = round(img.height * target_ratio)
        left = (img.width - crop_w) // 2
        return img.crop((left, 0, left + crop_w, img.height))
    if source_ratio < target_ratio:
        crop_h = round(img.width / target_ratio)
        top = (img.height - crop_h) // 2
        return img.crop((0, top, img.width, top + crop_h))
    return img


def pack(indexed: Image.Image, bpp: int) -> bytes:
    """Pack the indexed image MSB-first at `bpp` bits per pixel."""
    w, h = indexed.size
    raw = list(indexed.getdata())
    assert len(raw) == w * h
    pixels_per_byte = 8 // bpp
    assert w % pixels_per_byte == 0, (
        f"width {w} not a multiple of {pixels_per_byte} - refusing to pack {bpp}bpp"
    )
    mask = (1 << bpp) - 1
    out = bytearray(w * h * bpp // 8)
    idx = 0
    for i in range(0, w * h, pixels_per_byte):
        byte = 0
        for j in range(pixels_per_byte):
            shift = (pixels_per_byte - 1 - j) * bpp
            byte |= (raw[i + j] & mask) << shift
        out[idx] = byte
        idx += 1
    return bytes(out)


def emit_cpp(spec: ModelSpec, payloads: Dict[str, bytes], out_path: Path,
             landscape_spec: ModelSpec | None = None,
             landscape_payloads: Dict[str, bytes] | None = None) -> None:
    """Emit one .cpp per model containing every bundled theme + a pointer
    table indexed by the Theme enum.

    ``payloads`` maps theme name -> packed bytes for that theme. All bundled
    themes share the same payload size (driven by the ModelSpec), so we can
    expose a single ``kThemeDataLen`` at the top of the file.
    """
    themes_present = list(payloads.keys())
    payload_len = len(next(iter(payloads.values())))
    # Sanity: all payloads for one model are the same shape.
    assert all(len(p) == payload_len for p in payloads.values()), \
        "theme payloads for one model must be same length"

    # Fallback for slots in kThemeData[] we don't have a dedicated payload
    # for: prefer 'cloudy' when available (it reads as neutral), else the
    # first bundled theme.
    fallback_theme = "cloudy" if "cloudy" in payloads else themes_present[0]

    lines = [
        f"// AUTO-GENERATED - weather background(s) for reTerminal E{spec.model}.",
        f"// Panel {spec.panel_w}x{spec.panel_h}"
        f" ({'portrait' if spec.panel_h > spec.panel_w else 'landscape'}),"
        f" {len(themes_present)} theme(s), each"
        f" {spec.payload_w}x{spec.payload_h}"
        f" @ {spec.bpp}bpp x{spec.scale} nearest-neighbor upscale"
        f" ({spec.levels} gray levels).",
    ]
    if landscape_spec is not None and landscape_payloads is not None:
        lines.append(
            f"// Also bundles {landscape_spec.payload_w}x"
            f"{landscape_spec.payload_h} native landscape variants.")
    lines.extend([
        "// See tools/embed_weather_background.py to regenerate.",
        "",
        f"#if defined(RETERMINAL_MODEL) && RETERMINAL_MODEL == {spec.model}",
        "",
        "#include <pgmspace.h>",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "namespace weather_background {",
        "",
        f"extern const uint16_t kWidth = {spec.payload_w};",
        f"extern const uint16_t kHeight = {spec.payload_h};",
        f"extern const uint8_t  kBitsPerPixel = {spec.bpp};",
        f"extern const uint8_t  kLevels = {spec.levels};",
        f"extern const uint8_t  kScale = {spec.scale};",
        f"extern const uint8_t  kThemeCount = {len(themes_present)};",
        f"extern const size_t   kThemeDataLen = {payload_len};",
        "",
    ])
    if landscape_spec is not None and landscape_payloads is not None:
        lines.extend([
            f"extern const uint16_t kLandscapeWidth = "
            f"{landscape_spec.payload_w};",
            f"extern const uint16_t kLandscapeHeight = "
            f"{landscape_spec.payload_h};",
            f"extern const size_t kLandscapeThemeDataLen = "
            f"{len(next(iter(landscape_payloads.values())))};",
            "",
        ])
    # One PROGMEM array per bundled theme. Emitted with file-scope linkage
    # so callers only reach the payloads via the kThemeData[] table below.
    for theme, payload in payloads.items():
        cap = theme.capitalize()
        lines.append(f"static const uint8_t k{cap}[] PROGMEM = {{")
        for i in range(0, len(payload), 16):
            chunk = payload[i:i + 16]
            lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
        lines.append("};")
        lines.append("")

    # Pointer table indexed by the Theme enum. Themes we didn't bundle
    # redirect to the fallback so callers never see a null slot.
    entries = []
    for t in ALL_THEMES:
        name = t if t in payloads else fallback_theme
        entries.append(f"k{name.capitalize()}")
    lines.append(
        f"extern const uint8_t* const kThemeData[{len(ALL_THEMES)}] = "
        f"{{ {', '.join(entries)} }};"
    )
    lines.append("")
    if landscape_spec is not None and landscape_payloads is not None:
        for theme, payload in landscape_payloads.items():
            cap = theme.capitalize()
            lines.append(
                f"static const uint8_t kLandscape{cap}[] PROGMEM = {{")
            for i in range(0, len(payload), 16):
                chunk = payload[i:i + 16]
                lines.append(
                    "  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
            lines.append("};")
            lines.append("")
        landscape_entries = [
            f"kLandscape{(t if t in landscape_payloads else fallback_theme).capitalize()}"
            for t in ALL_THEMES
        ]
        lines.append(
            "extern const uint8_t* const "
            f"kLandscapeThemeData[{len(ALL_THEMES)}] = "
            f"{{ {', '.join(landscape_entries)} }};"
        )
        lines.append("")
    lines.append("}  // namespace weather_background")
    lines.append("")
    lines.append(f"#endif  // RETERMINAL_MODEL == {spec.model}")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def render_theme(spec: ModelSpec, theme: str, assets_dir: Path) -> bytes:
    """Refine + dither + pack the source PNG for one theme."""
    src = assets_dir / f"{theme}_source.png"
    img = Image.open(src).convert("L")
    if spec.rotate_source:
        # Our source PNGs are landscape 3:2 ink-wash mountain scenes.
        # Rotate so the mountains occupy the bottom of the portrait frame
        # before we resize to the panel dimensions.
        img = img.rotate(90, expand=True, resample=Image.BICUBIC)
    if spec.crop_to_panel:
        img = crop_to_aspect(img, (spec.payload_w, spec.payload_h))
    refined = refine(img, (spec.payload_w, spec.payload_h),
                     spec.fade_strength, spec.target_ink_density)
    indexed = dither_to_palette(refined, spec.levels)
    return pack(indexed, spec.bpp)


def render_for(spec: ModelSpec, assets_dir: Path) -> Path:
    payloads: Dict[str, bytes] = {}
    for theme in spec.themes:
        payloads[theme] = render_theme(spec, theme, assets_dir)
    out_path = OUT_DIR / spec.out_file
    landscape_spec = None
    landscape_payloads = None
    if spec.model == 1005:
        landscape_spec = replace(spec, panel_w=800, panel_h=480,
                                 crop_to_panel=False)
        landscape_payloads = {
            theme: render_theme(landscape_spec, theme, assets_dir)
            for theme in landscape_spec.themes
        }
    emit_cpp(spec, payloads, out_path, landscape_spec, landscape_payloads)
    return out_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-dir", type=Path, default=ASSETS_DIR,
                        help="Directory holding <theme>_source.png files.")
    parser.add_argument("--model", type=int, choices=[m.model for m in MODELS])
    parser.add_argument("--all", action="store_true",
                        help="Regenerate all five models.")
    args = parser.parse_args()

    if not args.model and not args.all:
        args.all = True

    specs = MODELS if args.all else [m for m in MODELS if m.model == args.model]
    for spec in specs:
        out_path = render_for(spec, args.assets_dir)
        size = os.path.getsize(out_path)
        orientation_count = 2 if spec.model == 1005 else 1
        print(
            f"E{spec.model}: wrote {out_path.name} "
            f"({size:>8d} B source, {orientation_count} orientation(s) x "
            f"{len(spec.themes)} theme(s) x {spec.payload_bytes:>7d} B payload)"
        )


if __name__ == "__main__":
    main()
