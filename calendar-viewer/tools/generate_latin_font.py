#!/usr/bin/env python3
"""Generate flash-backed Latin event-title fonts for Calendar Viewer."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FONT = ROOT / "tools" / "fonts" / "NotoSans-SemiCondensedBold.ttf"
OUTPUT = ROOT / "calendar-viewer" / "src" / "calendar_latin_font_data.h"
EXPECTED_SHA256 = "f7dc6bbca7a6b134600a49ca05352cb51cb519305b554bd93311bcd2c925d1bc"
COVERAGE_THRESHOLD = 64
TITLE_SIZES = (("Grid", 18), ("Agenda", 24))
UI_Y_ADVANCE = {
    10: 12,
    16: 20,
    18: 22,
    24: 29,
    36: 42,
    48: 56,
}
ASCII_CODEPOINTS = list(range(0x20, 0x7F))
UI_LATIN_FIRST = 0x20
UI_LATIN_LAST = 0x024F
CODEPOINT_RANGES = (
    (0x0020, 0x02FF),
    (0x0300, 0x036F),
    (0x1AB0, 0x1AFF),
    (0x1D00, 0x1DBF),
    (0x1DC0, 0x1DFF),
    (0x1E00, 0x1EFF),
    (0x2000, 0x218F),
    (0x2C60, 0x2C7F),
    (0xA720, 0xA7FF),
    (0xAB30, 0xAB6F),
    (0xFB00, 0xFB06),
    (0xFE20, 0xFE2F),
)
REQUIRED_CODEPOINTS = tuple(
    ord(character)
    for character in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "0123456789?éŁźüãíýðİŽșțỹ€–—…"
)


def selected_codepoints(font_path: Path) -> list[int]:
    available = set(TTFont(str(font_path)).getBestCmap())
    requested = {
        codepoint
        for first, last in CODEPOINT_RANGES
        for codepoint in range(first, last + 1)
    }
    selected = sorted(available & requested)
    missing = sorted(set(REQUIRED_CODEPOINTS) - set(selected))
    if missing:
        values = ", ".join(f"U+{codepoint:04X}" for codepoint in missing)
        raise RuntimeError(f"font is missing required calendar glyphs: {values}")
    if selected[-1] > 0xFFFF:
        raise RuntimeError("generated codepoint table exceeds 16-bit storage")
    return selected


def pack_bitmap(image: Image.Image) -> bytes:
    packed = bytearray()
    current = 0
    bits = 0
    for coverage in image.tobytes():
        current = (current << 1) | int(coverage >= COVERAGE_THRESHOLD)
        bits += 1
        if bits == 8:
            packed.append(current)
            current = 0
            bits = 0
    if bits:
        packed.append(current << (8 - bits))
    return bytes(packed)


def build_size(
    font_path: Path, pixel_size: int, codepoints: list[int]
) -> tuple[dict[str, list[int]], bytes, int, int]:
    font = ImageFont.truetype(str(font_path), pixel_size)
    ascent, descent = font.getmetrics()
    metrics = {
        "BitmapOffsets": [],
        "Widths": [],
        "Heights": [],
        "Advances": [],
        "XOffsets": [],
        "YOffsets": [],
    }
    bitmaps = bytearray()

    for codepoint in codepoints:
        character = chr(codepoint)
        bounding_box = font.getbbox(character, anchor="ls")
        advance = int(round(font.getlength(character)))
        if bounding_box is None:
            x_offset = y_offset = width = height = 0
        else:
            x_offset, y_offset, right, bottom = map(int, bounding_box)
            width = max(0, right - x_offset)
            height = max(0, bottom - y_offset)

        values = (width, height, advance, x_offset, y_offset)
        if not (
            all(0 <= value <= 255 for value in values[:3])
            and all(-128 <= value <= 127 for value in values[3:])
        ):
            raise RuntimeError(
                f"U+{codepoint:04X} metrics do not fit compact storage: {values}"
            )

        metrics["BitmapOffsets"].append(len(bitmaps))
        metrics["Widths"].append(width)
        metrics["Heights"].append(height)
        metrics["Advances"].append(advance)
        metrics["XOffsets"].append(x_offset)
        metrics["YOffsets"].append(y_offset)

        if width and height:
            image = Image.new("L", (width, height), 0)
            draw = ImageDraw.Draw(image)
            draw.text(
                (-x_offset, -y_offset),
                character,
                font=font,
                fill=255,
                anchor="ls",
            )
            bitmaps.extend(pack_bitmap(image))

    if len(bitmaps) > 0xFFFF:
        raise RuntimeError(
            f"{pixel_size}px bitmap data is {len(bitmaps)} bytes; "
            "16-bit offsets require at most 65535"
        )
    return metrics, bytes(bitmaps), ascent, descent


def format_array(
    value_type: str, name: str, values: list[int] | bytes, columns: int
) -> str:
    if value_type == "uint8_t":
        format_value = lambda value: f"0x{value:02X}"
    elif value_type == "uint16_t":
        format_value = lambda value: f"0x{value:04X}"
    elif value_type == "int8_t":
        format_value = str
    else:
        raise ValueError(f"unsupported array type: {value_type}")

    rows = []
    for offset in range(0, len(values), columns):
        row = ", ".join(
            format_value(value) for value in values[offset : offset + columns]
        )
        rows.append(f"    {row},")
    return (
        f"static const {value_type} {name}[] PROGMEM = {{\n"
        + "\n".join(rows)
        + "\n};\n"
    )


def glyph_values(metrics: dict[str, list[int]], index: int) -> tuple[int, ...]:
    return (
        metrics["BitmapOffsets"][index],
        metrics["Widths"][index],
        metrics["Heights"][index],
        metrics["Advances"][index],
        metrics["XOffsets"][index],
        metrics["YOffsets"][index],
    )


def format_gfx_glyphs(name: str, glyphs: list[tuple[int, ...]]) -> str:
    rows = [
        "    {"
        + f"{bitmap_offset}, {width}, {height}, {advance}, "
        + f"{x_offset}, {y_offset}"
        + "},"
        for bitmap_offset, width, height, advance, x_offset, y_offset in glyphs
    ]
    return (
        f"static const GFXglyph {name}[] PROGMEM = {{\n"
        + "\n".join(rows)
        + "\n};\n"
    )


def format_gfx_font(
    name: str,
    bitmap_name: str,
    glyph_name: str,
    first: int,
    last: int,
    y_advance: int,
) -> str:
    return (
        f"static const GFXfont {name} PROGMEM = {{\n"
        f"    (uint8_t*){bitmap_name},\n"
        f"    (GFXglyph*){glyph_name},\n"
        f"    0x{first:04X}, 0x{last:04X}, {y_advance}}};\n"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--font",
        type=Path,
        default=DEFAULT_FONT,
        help="Pinned NotoSans-SemiCondensedBold.ttf source.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.font.is_file():
        raise SystemExit(f"font source not found: {args.font}")
    digest = hashlib.sha256(args.font.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"{args.font} has SHA-256 {digest}, expected {EXPECTED_SHA256}"
        )

    codepoints = selected_codepoints(args.font)
    arrays = [
        format_array("uint16_t", "kCodepoints", codepoints, 12),
        f"constexpr size_t kCodepointCount = {len(codepoints)};\n",
        f"constexpr int kReplacementGlyphIndex = {codepoints.index(ord('?'))};\n",
    ]
    total_bytes = len(codepoints) * 2
    title_fonts = {}
    for name, pixel_size in TITLE_SIZES:
        metrics, bitmaps, ascent, descent = build_size(
            args.font, pixel_size, codepoints
        )
        title_fonts[pixel_size] = (name, metrics, bitmaps)
        arrays.append(format_array("uint8_t", f"k{name}Bitmaps", bitmaps, 16))
        arrays.append(
            format_array(
                "uint16_t",
                f"k{name}BitmapOffsets",
                metrics["BitmapOffsets"],
                12,
            )
        )
        for suffix in ("Widths", "Heights", "Advances"):
            arrays.append(
                format_array("uint8_t", f"k{name}{suffix}", metrics[suffix], 16)
            )
        for suffix in ("XOffsets", "YOffsets"):
            arrays.append(
                format_array("int8_t", f"k{name}{suffix}", metrics[suffix], 16)
            )
        arrays.append(f"constexpr uint8_t k{name}Ascent = {ascent};\n")
        arrays.append(f"constexpr uint8_t k{name}Descent = {descent};\n")
        size_bytes = len(bitmaps) + len(codepoints) * 7
        total_bytes += size_bytes
        print(
            f"{name.lower()} {pixel_size}px: {len(codepoints)} glyphs, "
            f"{size_bytes:,} bytes"
        )

    codepoint_indexes = {
        codepoint: index for index, codepoint in enumerate(codepoints)
    }
    for pixel_size, y_advance in UI_Y_ADVANCE.items():
        if pixel_size in title_fonts:
            source_name, metrics, _ = title_fonts[pixel_size]
            bitmap_name = f"k{source_name}Bitmaps"
            glyphs = []
            for codepoint in range(UI_LATIN_FIRST, UI_LATIN_LAST + 1):
                index = codepoint_indexes.get(codepoint)
                glyphs.append(
                    glyph_values(metrics, index)
                    if index is not None
                    else (0, 0, 0, 0, 0, 0)
                )
            first = UI_LATIN_FIRST
            last = UI_LATIN_LAST
            bitmap_bytes = 0
        else:
            metrics, bitmaps, _, _ = build_size(
                args.font, pixel_size, ASCII_CODEPOINTS
            )
            bitmap_name = f"kUi{pixel_size}Bitmaps"
            arrays.append(
                format_array("uint8_t", bitmap_name, bitmaps, 16)
            )
            glyphs = [
                glyph_values(metrics, index)
                for index in range(len(ASCII_CODEPOINTS))
            ]
            first = ASCII_CODEPOINTS[0]
            last = ASCII_CODEPOINTS[-1]
            bitmap_bytes = len(bitmaps)

        glyph_name = f"kUi{pixel_size}Glyphs"
        font_name = f"kUiFont{pixel_size}"
        arrays.append(format_gfx_glyphs(glyph_name, glyphs))
        arrays.append(
            format_gfx_font(
                font_name,
                bitmap_name,
                glyph_name,
                first,
                last,
                y_advance,
            )
        )
        size_bytes = bitmap_bytes + len(glyphs) * 12
        total_bytes += size_bytes
        print(
            f"ui {pixel_size}px: {len(glyphs)} slots, "
            f"{size_bytes:,} additional bytes"
        )

    output = (
        "#pragma once\n\n"
        "#include <TFT_eSPI.h>\n\n"
        "// Generated by calendar-viewer/tools/generate_latin_font.py from\n"
        "// Noto Sans SemiCondensed Bold. Noto fonts are licensed under the\n"
        "// SIL Open Font License 1.1; see tools/fonts/LICENSE.noto.\n"
        "namespace calendar_latin_font {\n"
        "namespace data {\n\n"
        + "\n".join(arrays)
        + "\n}  // namespace data\n"
        "}  // namespace calendar_latin_font\n"
    )
    OUTPUT.write_text(output, encoding="ascii", newline="\n")
    print(f"wrote {OUTPUT} ({total_bytes:,} embedded bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
