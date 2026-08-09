#!/usr/bin/env python3
"""Generate TFT_eSPI-compatible .vlw smooth-font files from a TTF/OTF.

TFT_eSPI (and the Seeed_GFX fork) supports VLW smooth fonts with built-in
UTF-8 decoding.  Its runtime reads a big-endian binary format that the
Processing sketch shipped with the library normally produces.  This script
produces the same format from Python so builds don't need a Processing
install.

Format (from Seeed_GFX/Extensions/Smooth_font.cpp):

  Header (24 bytes, big-endian uint32):
    1. glyph count
    2. version (11)
    3. font size in points (unused at runtime; we store the pixel size)
    4. mboxY (unused, 0)
    5. ascent in pixels
    6. descent in pixels

  Per glyph (28 bytes, big-endian int32):
    1. Unicode codepoint
    2. bitmap height
    3. bitmap width
    4. gxAdvance (pen advance)
    5. dY (baseline to top of bitmap; +ve = up)
    6. dX (cursor to left edge of bitmap; -ve = left)
    7. padding (0)

  Bitmaps: for each glyph, height*width bytes of 8-bit coverage (0..255).

  Trailer (optional, kept for compatibility with the Processing tool):
    1 byte  font name length (excluding null)
    <name> \\0
    1 byte  postscript name length (excluding null)
    <psname> \\0
    1 byte  anti-aliased flag (1)

Glyphs are emitted in ascending Unicode order so the runtime's binary
search over gUnicode[] resolves correctly.  Codepoints missing a real
glyph in the font are skipped rather than emitted as ``.notdef``.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path
from typing import Iterable

# PIL and fontTools are only needed when we actually build a font; keep
# them out of module import so callers that just want the constants
# (e.g. preload_sd re-exporting DEFAULT_SIZES_PX) don't pay for them.

VLW_VERSION = 11

# Default pixel sizes generated when --size is not passed.  We generate
# every integer size from 12 to 48 so future tweaks to
# SMOOTH_FONT_*_PX in either app's main.cpp don't require regenerating
# the SD card - any size we might reasonably pick is already there.
# ~5,900 glyphs per size makes this ~110 MB total, which is well below
# the SD card capacity used by xkcd caches and weather history.
DEFAULT_SIZES_PX = tuple(range(12, 49))

# Default TTF source lives alongside this script.
DEFAULT_TTF = Path(__file__).resolve().parent / "DejaVuSans-Bold.ttf"


def enumerate_codepoints(ttf_path: Path) -> list[int]:
    from fontTools.ttLib import TTFont
    font = TTFont(str(ttf_path))
    cmap = font.getBestCmap()
    return sorted(cp for cp in cmap.keys() if cp >= 0x20)


def render_glyph(font, codepoint: int):
    """Return (bitmap_bytes, width, height, dX, dY, xAdvance) or None."""
    from PIL import Image, ImageDraw
    ch = chr(codepoint)

    try:
        bbox = font.getbbox(ch)  # (x0, y0, x1, y1) in pen-origin coords
    except Exception:
        return None
    if bbox is None:
        return None
    x0, y0, x1, y1 = bbox
    width = max(0, int(x1) - int(x0))
    height = max(0, int(y1) - int(y0))
    x_advance = int(round(font.getlength(ch)))

    # Whitespace-only glyphs (space, tabs, NBSP, ...) legitimately have zero
    # width/height but still need to move the cursor.  Emit a zero-bitmap
    # entry so the runtime knows about the codepoint and its advance.
    if width == 0 or height == 0:
        return (b"", 0, 0, 0, 0, x_advance)

    # Render the glyph to an L-mode image so each pixel is a coverage byte.
    canvas = Image.new("L", (width + 2, height + 2), 0)
    draw = ImageDraw.Draw(canvas)
    # Position the glyph so its top-left in the bbox lands at (1, 1).  The
    # font's pen origin sits at (-x0 + 1, -y0 + 1).
    draw.text((-int(x0) + 1, -int(y0) + 1), ch, font=font, fill=255)
    cropped = canvas.crop((1, 1, 1 + width, 1 + height))
    data = cropped.tobytes()  # row-major, width*height bytes

    ascent = font.getmetrics()[0]
    # gdY is "baseline to top of bitmap, +ve up".  In PIL bbox space the
    # baseline is at y = ascent (approximately) and the top of the bitmap
    # is at y = y0.  For most fonts y0 < ascent so gdY is positive.
    gdY = ascent - int(y0)
    gdX = int(x0)
    return (data, width, height, gdX, gdY, x_advance)


def build_vlw(
    ttf_path: Path,
    pixel_size: int,
    codepoints: Iterable[int] | None = None,
    family_name: str | None = None,
) -> bytes:
    from PIL import ImageFont
    font = ImageFont.truetype(str(ttf_path), pixel_size)
    ascent_px, descent_px = font.getmetrics()

    entries: list[tuple[int, bytes, int, int, int, int, int]] = []
    available = set(enumerate_codepoints(ttf_path))
    selected = sorted(
        available if codepoints is None else available.intersection(codepoints)
    )
    for cp in selected:
        rendered = render_glyph(font, cp)
        if rendered is None:
            continue
        data, w, h, dX, dY, xAdv = rendered
        entries.append((cp, data, w, h, xAdv, dY, dX))

    header = struct.pack(
        ">IIIIII",
        len(entries),
        VLW_VERSION,
        pixel_size,
        0,
        ascent_px,
        descent_px,
    )

    metrics = bytearray()
    for cp, data, w, h, xAdv, dY, dX in entries:
        metrics += struct.pack(">IIIIiiI", cp, h, w, xAdv, dY, dX, 0)

    bitmaps = bytearray()
    for _, data, w, h, *_ in entries:
        bitmaps += data

    source_name = family_name or ttf_path.stem
    family = "".join(character for character in source_name if character.isalnum())
    name = f"{family}_{pixel_size}".encode("ascii")
    ps_name = name
    trailer = bytearray()
    trailer.append(len(name))
    trailer += name
    trailer.append(0)
    trailer.append(len(ps_name))
    trailer += ps_name
    trailer.append(0)
    trailer.append(1)  # anti-aliased

    return bytes(header) + bytes(metrics) + bytes(bitmaps) + bytes(trailer)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--ttf", type=Path, default=DEFAULT_TTF,
        help=f"Path to source TTF/OTF (default: {DEFAULT_TTF.name} next to this script).",
    )
    parser.add_argument(
        "--size", action="append", type=int, default=None,
        help=(
            "Pixel size to render (may be repeated). Defaults to every "
            f"integer from {DEFAULT_SIZES_PX[0]} to {DEFAULT_SIZES_PX[-1]} "
            "so future tweaks to the firmware do not require regenerating "
            "the SD card."
        ),
    )
    parser.add_argument(
        "out_dir", type=Path, metavar="OUT_DIR",
        help="Directory to write <basename>_<size>.vlw files into.",
    )
    parser.add_argument(
        "--basename", default="sans_bold",
        help="Filename prefix for the generated .vlw files.",
    )
    args = parser.parse_args()
    if args.size is None:
        args.size = list(DEFAULT_SIZES_PX)
    return args


def main() -> int:
    args = parse_args()
    if not args.ttf.exists():
        print(f"error: {args.ttf} does not exist", file=sys.stderr)
        return 2
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for size in args.size:
        blob = build_vlw(args.ttf, size)
        out = args.out_dir / f"{args.basename}_{size}.vlw"
        out.write_bytes(blob)
        print(f"wrote {out} ({len(blob):,} bytes, {size}px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
