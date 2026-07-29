#!/usr/bin/env python3
"""Rasterise the Meteocons line SVGs into a header of packed sprites.

The header is regenerated per-build against the active RETERMINAL_MODEL and
included from weather_icons.cpp. Each of the 26 icons is emitted at three
sizes (small / mid / large) tuned to the drawWeatherIcon call-sites on that
board.

Sprites are stored as either 1 or 2 bits per pixel depending on what the
target panel can actually display:

  * 1001 (Gray4)   -> 2 bpp: 4 alpha levels quantise to native greys, so
                             edges anti-alias instead of staircasing.
  * 1003 (Gray16)  -> 2 bpp: same story with even smoother greys.
  * 1002, 1004 (6-color) -> 1 bpp: no native greys available, so the extra
                                   bit would be wasted.

Bit layout for 1 bpp: MSB-first, row-padded to whole bytes.
Bit layout for 2 bpp: 4 pixels per byte, top pair first (bits 7-6 = first
pixel, 5-4 = second, ...); row-padded to whole bytes. Value 0 is
transparent (skip), values 1..3 index the runtime ink palette from lightest
to darkest.

Usage:
    python generate_weather_icons.py <MODEL> <output-header>

Where MODEL is 1001 / 1002 / 1003 / 1004.
"""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

import resvg_py
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[1]
SVG_DIR = REPO_ROOT / "icons" / "svg"

# Bucket order — MUST match the IconId enum in weather_icons.h.
BUCKETS = [
    "clear-day", "clear-night",
    "partly-cloudy-day", "partly-cloudy-night",
    "overcast", "overcast-day", "overcast-night", "cloudy",
    "fog-day", "fog-night", "haze",
    "drizzle", "rain",
    "partly-cloudy-day-rain", "partly-cloudy-night-rain",
    "snow", "partly-cloudy-day-snow", "partly-cloudy-night-snow", "sleet",
    "thunderstorms", "thunderstorms-day", "thunderstorms-night",
    "thunderstorms-day-rain", "thunderstorms-night-rain",
    "thunderstorms-day-snow", "thunderstorms-night-snow",
]

# Per-board (small, mid, large) pixel sizes. Derived from drawWeatherIcon
# call-sites in main.cpp:
#   forecast card = min(width/7, ui(32))
#   portrait row  = min(height/5, ui(40))
#   landscape hero = min(PANEL_WIDTH*27/100, ...)
#   portrait main  = min(PANEL_WIDTH*35/100, ...)
# The largest slot for each board sets the "large" entry; small/mid cover
# the smaller call-sites without runtime downscaling artefacts. E1003 hero
# is bumped down from 512 -> 480 to keep the 2 bpp storage under the app
# partition ceiling.
SIZES_BY_MODEL = {
    1001: (32, 48, 216),   # 800x480 landscape, 1:1 scale
    1002: (32, 48, 216),   # same panel dimensions as 1001
    1003: (72, 96, 480),   # 1872x1404 Gray16, 9/4 scale
    1004: (48, 64, 420),   # 1200x1600 portrait, 3/2 scale
}

# Bits per pixel. 2 bpp gives 4 alpha levels: level 3 = solid stroke,
# levels 1 and 2 = anti-aliased edge (dithered at blit time), level 0 =
# transparent. Applied to every model so 6-colour panels get the same
# dithered soft edges as the greyscale ones.
BPP_BY_MODEL = {
    1001: 2,
    1002: 2,
    1003: 2,
    1004: 2,
}


def rasterise_alpha(svg_path: Path, size: int) -> bytes:
    """Return a `size*size` bytes buffer where each byte is the alpha
    coverage (0..255) of the rasterised SVG at (x, y). resvg matches the
    SVG viewBox so we resize with LANCZOS after rendering for accuracy."""
    text = svg_path.read_text(encoding="utf-8")
    png_bytes = bytes(resvg_py.svg_to_bytes(
        svg_string=text, resources_dir=str(svg_path.parent),
    ))
    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)
    alpha = img.split()[-1]
    return alpha.tobytes()


def pack_1bit(alpha: bytes, size: int, threshold: int = 128) -> bytes:
    """Pack an alpha buffer into a 1-bit MSB-first, row-padded bitmap.
    Rows are aligned to whole bytes so blitting is straightforward."""
    row_bytes = (size + 7) // 8
    out = bytearray(row_bytes * size)
    for y in range(size):
        row_off = y * row_bytes
        row_src = y * size
        for x in range(size):
            if alpha[row_src + x] >= threshold:
                out[row_off + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def pack_2bit(alpha: bytes, size: int) -> bytes:
    """Pack an alpha buffer into a 2-bit-per-pixel bitmap, 4 pixels per
    byte, top pair first. Alpha is quantised into 4 buckets (0..63 = 0,
    64..127 = 1, 128..191 = 2, 192..255 = 3). Value 0 stays transparent
    at blit time; 1..3 index the runtime ink palette lightest -> darkest.
    Rows are aligned to whole bytes."""
    row_bytes = (size * 2 + 7) // 8
    out = bytearray(row_bytes * size)
    for y in range(size):
        row_off = y * row_bytes
        row_src = y * size
        for x in range(size):
            v = alpha[row_src + x] >> 6  # 0..3
            if v:
                shift = 6 - 2 * (x & 3)
                out[row_off + (x >> 2)] |= v << shift
    return bytes(out)


def to_c_identifier(name: str) -> str:
    return name.replace("-", "_")


def emit_c_array(w, name: str, data: bytes, indent: str = "  ") -> None:
    """Emit a `constexpr uint8_t NAME[] PROGMEM = {...};` array."""
    w.write(f"{indent}constexpr uint8_t {name}[] PROGMEM = {{\n")
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        line = ", ".join(f"0x{b:02x}" for b in chunk)
        w.write(f"{indent}  {line},\n")
    w.write(f"{indent}}};\n")


def generate(model: int, out_path: Path) -> None:
    if model not in SIZES_BY_MODEL:
        raise SystemExit(f"Unsupported RETERMINAL_MODEL={model}")
    small, mid, large = SIZES_BY_MODEL[model]
    bpp = BPP_BY_MODEL[model]
    pack = pack_2bit if bpp == 2 else pack_1bit
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("w", encoding="utf-8", newline="\n") as w:
        w.write(
            "// Auto-generated by weather-viewer/tools/generate_weather_icons.py.\n"
            "// Do not edit by hand. Regenerate by running a build (the pre-\n"
            "// script runs the generator with the active RETERMINAL_MODEL) or\n"
            "// by invoking the script directly.\n"
            "//\n"
            f"// Model: {model}\n"
            f"// Sizes: small={small}, mid={mid}, large={large}\n"
            f"// Bits per pixel: {bpp}\n"
            "//\n"
            "// Icons derived from basmilius/weather-icons (MIT). See\n"
            "// LICENSES/METEOCONS.txt for the upstream license.\n"
            "\n"
            "#pragma once\n"
            "\n"
            "#include <Arduino.h>\n"
            "#include <pgmspace.h>\n"
            "#include <stddef.h>\n"
            "#include <stdint.h>\n"
            "\n"
            "namespace weather_icons {\n"
            "namespace generated {\n"
            "\n"
            f"inline constexpr uint16_t kSmallPx = {small};\n"
            f"inline constexpr uint16_t kMidPx   = {mid};\n"
            f"inline constexpr uint16_t kLargePx = {large};\n"
            f"inline constexpr uint8_t  kBpp     = {bpp};\n"
            "\n"
        )

        # Emit per-icon sprite triples.
        for stem in BUCKETS:
            ident = to_c_identifier(stem)
            svg = SVG_DIR / f"{stem}.svg"
            if not svg.exists():
                raise SystemExit(f"Missing SVG: {svg}")
            for suffix, size in (("s", small), ("m", mid), ("l", large)):
                alpha = rasterise_alpha(svg, size)
                bits = pack(alpha, size)
                emit_c_array(w, f"kBits_{ident}_{suffix}", bits)

        # Emit a Sprite / IconTriple table for lookup.
        w.write(
            "\n"
            "struct Sprite {\n"
            "  const uint8_t* bits;\n"
            "  uint16_t w;\n"
            "  uint16_t h;\n"
            "};\n"
            "\n"
            "struct IconTriple {\n"
            "  Sprite small;\n"
            "  Sprite mid;\n"
            "  Sprite large;\n"
            "};\n"
            "\n"
            f"inline constexpr size_t kIconCount = {len(BUCKETS)};\n"
            "\n"
            "inline constexpr IconTriple kIcons[kIconCount] = {\n"
        )
        for stem in BUCKETS:
            ident = to_c_identifier(stem)
            w.write(
                f"  {{ {{ kBits_{ident}_s, kSmallPx, kSmallPx }},\n"
                f"    {{ kBits_{ident}_m, kMidPx,   kMidPx   }},\n"
                f"    {{ kBits_{ident}_l, kLargePx, kLargePx }} }},  // {stem}\n"
            )
        w.write("};\n\n")

        w.write("}  // namespace generated\n")
        w.write("}  // namespace weather_icons\n")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("model", type=int, help="RETERMINAL_MODEL (1001..1004)")
    p.add_argument("output", type=Path, help="Destination header path")
    args = p.parse_args()
    generate(args.model, args.output)
    print(f"generated {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
