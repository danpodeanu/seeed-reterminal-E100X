"""Regenerate weather-viewer/src/zenith_background_data.cpp from a source PNG.

The pipeline: 2x LANCZOS upscale, Gaussian blur to melt the source halftone
into continuous tone, unsharp mask to bring back edges, contrast auto-fix,
LANCZOS back to native panel size, then Floyd-Steinberg dither to the E1001
4-gray palette. Output is packed 2bpp MSB-first, 96000 bytes for 800x480.

The default input is the zenith horizontal icon extracted from the
reTerminal Sticky firmware; rerun this whenever the PNG changes or when you
want to tune the refinement parameters.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path

from PIL import Image, ImageFilter, ImageOps

WIDTH = 800
HEIGHT = 480

DEFAULT_SRC = Path(__file__).resolve().parents[1] / "weather-viewer" / "assets" / "zenith_source.png"
DEFAULT_OUT = Path(__file__).resolve().parents[1] / "weather-viewer" / "src" / "zenith_background_data.cpp"


FADE_STRENGTH = 0.55


def refine(img: Image.Image) -> Image.Image:
    w, h = img.size
    sup = img.resize((w * 2, h * 2), Image.LANCZOS)
    sup = sup.filter(ImageFilter.GaussianBlur(radius=1.6))
    sup = sup.filter(ImageFilter.UnsharpMask(radius=1.5, percent=45, threshold=3))
    sup = ImageOps.autocontrast(sup, cutoff=0.5)
    sup = sup.resize((w, h), Image.LANCZOS)
    # Blend toward white so overlaid text stays readable. FADE_STRENGTH=0.55
    # keeps ~45% of the ink, plenty to still read as a landscape.
    white = Image.new("L", sup.size, 255)
    return Image.blend(sup, white, FADE_STRENGTH)


def to_e1001_palette(refined: Image.Image) -> Image.Image:
    pal_img = Image.new("P", (1, 1))
    palette = [0, 0, 0, 85, 85, 85, 170, 170, 170, 255, 255, 255] + [0] * (256 * 3 - 12)
    pal_img.putpalette(palette)
    return refined.convert("RGB").quantize(colors=4, palette=pal_img, dither=Image.FLOYDSTEINBERG)


def pack_2bpp(indexed: Image.Image) -> bytes:
    w, h = indexed.size
    raw = list(indexed.getdata())
    assert len(raw) == w * h and w % 4 == 0
    out = bytearray(w * h // 4)
    for i in range(0, w * h, 4):
        out[i // 4] = (raw[i] << 6) | (raw[i + 1] << 4) | (raw[i + 2] << 2) | raw[i + 3]
    return bytes(out)


def emit_cpp(payload: bytes, out_path: Path, width: int, height: int) -> None:
    lines = [
        "// AUTO-GENERATED FROM sticky icon_zenith_h.bin (2bpp 800x480, 4-gray).",
        "// See tools/embed_zenith_background.py to regenerate.",
        "#include <pgmspace.h>",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "namespace zenith_background {",
        "",
        f"extern const uint16_t kWidth = {width};",
        f"extern const uint16_t kHeight = {height};",
        f"extern const size_t kDataLen = {len(payload)};",
        "",
        "extern const uint8_t kData[] PROGMEM = {",
    ]
    for i in range(0, len(payload), 16):
        chunk = payload[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace zenith_background")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=DEFAULT_SRC)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    img = Image.open(args.src).convert("L")
    if img.size != (WIDTH, HEIGHT):
        img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)

    refined = refine(img)
    indexed = to_e1001_palette(refined)
    packed = pack_2bpp(indexed)
    emit_cpp(packed, args.out, WIDTH, HEIGHT)
    print(f"wrote {args.out} ({os.path.getsize(args.out)} bytes source, {len(packed)} bytes payload)")


if __name__ == "__main__":
    main()
