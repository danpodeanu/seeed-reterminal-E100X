"""Regenerate weather-viewer/src/weather_background_data_e1NNN.cpp for one model.

Pipeline: LANCZOS upscale, Gaussian blur to melt the source halftone into
continuous tone, unsharp mask to bring back edges, contrast auto-fix,
optional rotation, LANCZOS resize to the target panel size, fade toward
white so overlaid text stays legible, then Floyd-Steinberg dither to the
model's target palette. Output is packed at the model's native bit depth
and dropped in a per-model PROGMEM cpp guarded with `#if RETERMINAL_MODEL`.

Regenerate a single model:
    python tools/embed_weather_background.py --model 1003
    python tools/embed_weather_background.py --model 1004
Regenerate everything:
    python tools/embed_weather_background.py --all
"""
from __future__ import annotations

import argparse
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Tuple

from PIL import Image, ImageFilter, ImageOps

REPO = Path(__file__).resolve().parents[1]
DEFAULT_SRC = REPO / "weather-viewer" / "assets" / "cloudy_source.png"
OUT_DIR = REPO / "weather-viewer" / "src"

# Fade the ink-wash toward white so headline text stays legible. 0.55 keeps
# ~45% of the original ink, plenty to still read as a landscape.
FADE_STRENGTH = 0.55


@dataclass(frozen=True)
class ModelSpec:
    model: int
    panel_w: int       # panel size in pixels
    panel_h: int
    portrait: bool     # rotate the landscape source 90 degrees before resize
    bpp: int           # bits per pixel in the packed payload
    levels: int        # number of dither levels (2, 4, ...); must match bpp mask
    scale: int         # blitter upscale factor (1 = 1:1, 2 = nearest-neighbor 2x2)
    out_file: str

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
    # E1001 UC8179 4-gray landscape.
    ModelSpec(1001, 800, 480, False, 2, 4, 1, "weather_background_data_e1001.cpp"),
    # E1002 Spectra 6 landscape. Only black and white read cleanly for a
    # dithered gray landscape on this palette, so store 1bpp BW.
    ModelSpec(1002, 800, 480, False, 1, 2, 1, "weather_background_data_e1002.cpp"),
    # E1003 16-gray landscape. 1872x1404 at 2bpp would be 657 KB. Store
    # at half resolution and let the blitter upscale 2x with nearest
    # neighbour - the picture is a stylised ink wash so soft edges are
    # fine, and this drops the payload to ~164 KB and keeps E1003's
    # firmware well under the flash limit.
    ModelSpec(1003, 1872, 1404, False, 2, 4, 2, "weather_background_data_e1003.cpp"),
    # E1004 Spectra 6 portrait. Same BW rationale as E1002.
    ModelSpec(1004, 1200, 1600, True, 1, 2, 1, "weather_background_data_e1004.cpp"),
]


def refine(img: Image.Image, target_size: Tuple[int, int]) -> Image.Image:
    """Continuous-tone refinement + fade + resize into the target panel."""
    w, h = img.size
    sup = img.resize((w * 2, h * 2), Image.LANCZOS)
    sup = sup.filter(ImageFilter.GaussianBlur(radius=1.6))
    sup = sup.filter(ImageFilter.UnsharpMask(radius=1.5, percent=45, threshold=3))
    sup = ImageOps.autocontrast(sup, cutoff=0.5)
    sup = sup.resize(target_size, Image.LANCZOS)
    white = Image.new("L", sup.size, 255)
    return Image.blend(sup, white, FADE_STRENGTH)


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


def emit_cpp(spec: ModelSpec, payload: bytes, out_path: Path) -> None:
    lines = [
        f"// AUTO-GENERATED - weather background for reTerminal E{spec.model}.",
        f"// Panel {spec.panel_w}x{spec.panel_h}"
        f" ({'portrait' if spec.portrait else 'landscape'}),"
        f" payload {spec.payload_w}x{spec.payload_h}"
        f" @ {spec.bpp}bpp x{spec.scale} nearest-neighbor upscale"
        f" ({spec.levels} gray levels).",
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
        f"extern const size_t kDataLen = {len(payload)};",
        "",
        "extern const uint8_t kData[] PROGMEM = {",
    ]
    for i in range(0, len(payload), 16):
        chunk = payload[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace weather_background")
    lines.append("")
    lines.append(f"#endif  // RETERMINAL_MODEL == {spec.model}")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def render_for(spec: ModelSpec, src: Path) -> Path:
    img = Image.open(src).convert("L")
    if spec.portrait:
        # The source is a landscape ink-wash. Rotate so the mountains
        # occupy the bottom of the portrait frame before we resize.
        img = img.rotate(90, expand=True, resample=Image.BICUBIC)
    refined = refine(img, (spec.payload_w, spec.payload_h))
    indexed = dither_to_palette(refined, spec.levels)
    packed = pack(indexed, spec.bpp)
    out_path = OUT_DIR / spec.out_file
    emit_cpp(spec, packed, out_path)
    return out_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=DEFAULT_SRC)
    parser.add_argument("--model", type=int, choices=[m.model for m in MODELS])
    parser.add_argument("--all", action="store_true",
                        help="Regenerate all four models.")
    args = parser.parse_args()

    if not args.model and not args.all:
        args.all = True

    specs = MODELS if args.all else [m for m in MODELS if m.model == args.model]
    for spec in specs:
        out_path = render_for(spec, args.src)
        size = os.path.getsize(out_path)
        print(
            f"E{spec.model}: wrote {out_path.name} "
            f"({size:>8d} B source, {spec.payload_bytes:>7d} B payload)"
        )


if __name__ == "__main__":
    main()
