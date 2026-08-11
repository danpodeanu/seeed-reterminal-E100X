#!/usr/bin/env python3
"""Generate embedded styled Latin fonts for the Sticky Arcade EPUB reader."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_HEADER = ROOT / "sticky-arcade" / "src" / "epub_latin_fonts.h"
PIXEL_SIZE = 24
EPAPER_COVERAGE_THRESHOLD = 64
FALLBACK_FONTS = (
    ROOT / "fonts" / "sans_bold_24.vlw",
    ROOT / "fonts" / "epub_cjk_24.vlw",
)
FONT_FILES = {
    "Regular": (
        "NotoSerif-Regular.ttf",
        "c8f669ceb2c9c60ccf55198b305e08a997ffca79a38cc7eeb551e643cbe66505",
    ),
    "Bold": (
        "NotoSerif-Bold.ttf",
        "3b2086a869bcded2aeb4416fc281ceec9d6ce3c06756cda19f8f763636204e7d",
    ),
    "Italic": (
        "NotoSerif-Italic.ttf",
        "0ea81d6b54fc8aa5dff0fd6ebe7cfa431e9e6cf747a8d4aa33581fa0aaccffea",
    ),
    "BoldItalic": (
        "NotoSerif-BoldItalic.ttf",
        "4fb8737145b4a503d548af4b517afdfc532e44a96ac15378257e825741334eec",
    ),
}


def load_vlw_module():
    module_path = ROOT / "tools" / "fonts" / "make_vlw.py"
    spec = importlib.util.spec_from_file_location("make_vlw", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def required_codepoints() -> list[int]:
    codepoints = set(range(0x20, 0x0250))
    codepoints.update(range(0x0300, 0x0370))
    codepoints.update(range(0x2000, 0x2070))
    codepoints.update(range(0x20A0, 0x20D0))
    return sorted(codepoints)


def format_array(name: str, data: bytes) -> str:
    rows = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02X}" for value in data[offset : offset + 16])
        rows.append(f"    {values},")
    return (
        f"inline constexpr uint8_t {name}[] PROGMEM = {{\n"
        + "\n".join(rows)
        + "\n};\n"
    )


def format_codepoints(codepoints: list[int]) -> str:
    rows = []
    for offset in range(0, len(codepoints), 12):
        values = ", ".join(
            f"0x{value:04X}" for value in codepoints[offset : offset + 12]
        )
        rows.append(f"    {values},")
    return (
        "inline constexpr uint16_t kSupportedCodepoints[] PROGMEM = {\n"
        + "\n".join(rows)
        + "\n};\n\n"
        "inline int indexOf(uint32_t codepoint) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(kSupportedCodepoints) / "
        "sizeof(kSupportedCodepoints[0]);\n"
        "  while (low < high) {\n"
        "    const size_t middle = low + (high - low) / 2;\n"
        "    const uint16_t candidate = "
        "pgm_read_word(&kSupportedCodepoints[middle]);\n"
        "    if (candidate == codepoint) return static_cast<int>(middle);\n"
        "    if (candidate < codepoint) low = middle + 1;\n"
        "    else high = middle;\n"
        "  }\n"
        "  return -1;\n"
        "}\n\n"
        "inline bool supports(uint32_t codepoint) {\n"
        "  return indexOf(codepoint) >= 0;\n"
        "}\n"
    )


def format_advances(name: str, advances: list[int]) -> str:
    rows = []
    for offset in range(0, len(advances), 16):
        values = ", ".join(
            f"0x{value:02X}" for value in advances[offset : offset + 16]
        )
        rows.append(f"    {values},")
    return (
        f"inline constexpr uint8_t k{name}Advance[] PROGMEM = {{\n"
        + "\n".join(rows)
        + "\n};\n"
    )


def format_fallback_advances(codepoints: list[int], advances: list[int]) -> str:
    codepoint_rows = []
    for offset in range(0, len(codepoints), 12):
        values = ", ".join(
            f"0x{value:04X}" for value in codepoints[offset : offset + 12]
        )
        codepoint_rows.append(f"    {values},")
    return (
        "inline constexpr uint16_t kFallbackCodepoints[] PROGMEM = {\n"
        + "\n".join(codepoint_rows)
        + "\n};\n\n"
        + format_advances("FallbackMaximum", advances)
        + "\n"
        "inline bool maximumFallbackAdvance(uint32_t codepoint, "
        "uint8_t& advance) {\n"
        "  size_t low = 0;\n"
        "  size_t high = sizeof(kFallbackCodepoints) / "
        "sizeof(kFallbackCodepoints[0]);\n"
        "  while (low < high) {\n"
        "    const size_t middle = low + (high - low) / 2;\n"
        "    const uint16_t candidate = "
        "pgm_read_word(&kFallbackCodepoints[middle]);\n"
        "    if (candidate == codepoint) {\n"
        "      advance = pgm_read_byte(&kFallbackMaximumAdvance[middle]);\n"
        "      return true;\n"
        "    }\n"
        "    if (candidate < codepoint) low = middle + 1;\n"
        "    else high = middle;\n"
        "  }\n"
        "  advance = 0;\n"
        "  return false;\n"
        "}\n"
    )


def vlw_advances(blob: bytes) -> dict[int, int]:
    if len(blob) < 24:
        raise ValueError("VLW data is shorter than its header")
    count, _, _, _, ascent, descent = struct.unpack_from(">IIIIII", blob, 0)
    metrics_end = 24 + count * 28
    if metrics_end > len(blob):
        raise ValueError("VLW metrics exceed the font data")

    advances = {}
    max_descent = descent
    for index in range(count):
        codepoint, height, _, advance, d_y, _, _ = struct.unpack_from(
            ">IIIIiiI", blob, 24 + index * 28
        )
        advances[codepoint] = advance
        glyph_descent = int(height) - int(d_y)
        if (
            glyph_descent > max_descent
            and (
                (0x20 < codepoint < 0xA0 and codepoint != 0x7F)
                or codepoint > 0xFF
            )
        ):
            max_descent = glyph_descent
    advances[0x20] = (ascent + max_descent) * 2 // 7
    return advances


def quantize_for_one_bit_epaper(blob: bytes) -> bytes:
    """Convert retained antialias coverage to solid ink for a 1-bit sprite."""
    if len(blob) < 24:
        raise ValueError("VLW data is shorter than its header")
    count = struct.unpack_from(">I", blob, 0)[0]
    bitmap_start = 24 + count * 28
    bitmap_length = 0
    for index in range(count):
        height, width = struct.unpack_from(">II", blob, 24 + index * 28 + 4)
        bitmap_length += height * width
    bitmap_end = bitmap_start + bitmap_length
    if bitmap_end > len(blob):
        raise ValueError("VLW bitmaps exceed the font data")

    quantized = bytearray(blob)
    for index in range(bitmap_start, bitmap_end):
        quantized[index] = (
            0xFF if quantized[index] >= EPAPER_COVERAGE_THRESHOLD else 0x00
        )
    return bytes(quantized)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "font_dir",
        type=Path,
        help="Directory containing the four pinned Noto Serif TTF files.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    make_vlw = load_vlw_module()
    requested_codepoints = set(required_codepoints())
    sources = {}
    shared_codepoints = requested_codepoints
    for style, (filename, expected_hash) in FONT_FILES.items():
        source = args.font_dir / filename
        if not source.is_file():
            raise SystemExit(f"missing font source: {source}")
        actual_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise SystemExit(
                f"{source} has SHA-256 {actual_hash}, expected {expected_hash}"
            )
        sources[style] = source
        shared_codepoints = shared_codepoints.intersection(
            make_vlw.enumerate_codepoints(source)
        )

    codepoints = sorted(shared_codepoints)
    arrays = []
    advance_arrays = []
    style_advances = {}
    for style, source in sources.items():
        blob = make_vlw.build_vlw(
            source,
            PIXEL_SIZE,
            codepoints=codepoints,
            family_name=f"EpubLatin{style}",
        )
        blob = quantize_for_one_bit_epaper(blob)
        arrays.append(format_array(f"k{style}", blob))
        runtime_advances = vlw_advances(blob)
        advances = [runtime_advances[codepoint] for codepoint in codepoints]
        style_advances[style] = advances
        advance_arrays.append(format_advances(style, advances))
        print(f"generated {style}: {len(blob):,} bytes")

    fallback_advances = []
    for fallback in FALLBACK_FONTS:
        if not fallback.is_file():
            raise SystemExit(f"missing fallback font: {fallback}")
        fallback_advances.append(vlw_advances(fallback.read_bytes()))
    fallback_codepoints = sorted(
        codepoint
        for codepoint in set().union(
            *(advances.keys() for advances in fallback_advances)
        )
        if codepoint <= 0xFFFF
    )
    fallback_maximum_advances = [
        max(
            advances[codepoint]
            for advances in fallback_advances
            if codepoint in advances
        )
        for codepoint in fallback_codepoints
    ]
    maximum_advances = []
    for index, codepoint in enumerate(codepoints):
        candidates = [
            style_advances[style][index]
            for style in FONT_FILES
        ]
        candidates.extend(
            advances[codepoint]
            for advances in fallback_advances
            if codepoint in advances
        )
        maximum_advances.append(max(candidates))
    maximum_advance_array = format_advances("Maximum", maximum_advances)

    advance_function = (
        "inline uint8_t advance(uint8_t style, uint32_t codepoint) {\n"
        "  const int index = indexOf(codepoint);\n"
        "  if (index < 0) return 0;\n"
        "  switch (style) {\n"
        "    case 1: return pgm_read_byte(&kBoldAdvance[index]);\n"
        "    case 2: return pgm_read_byte(&kItalicAdvance[index]);\n"
        "    case 3: return pgm_read_byte(&kBoldItalicAdvance[index]);\n"
        "    default: return pgm_read_byte(&kRegularAdvance[index]);\n"
        "  }\n"
        "}\n\n"
        "inline uint8_t maximumAdvance(uint32_t codepoint) {\n"
        "  const int index = indexOf(codepoint);\n"
        "  return index < 0 ? 0 : pgm_read_byte(&kMaximumAdvance[index]);\n"
        "}\n"
    )
    output = (
        "#pragma once\n\n"
        "#include <Arduino.h>\n\n"
        "// Generated by sticky-arcade/tools/generate_epub_latin_fonts.py from Noto Serif.\n"
        "// Coverage is quantized for Seeed's one-bit e-paper sprite.\n"
        "// Noto fonts are licensed under the SIL Open Font License 1.1.\n"
        "namespace epub_latin_fonts {\n\n"
        + format_codepoints(codepoints)
        + "\n"
        + "\n".join(advance_arrays)
        + "\n"
        + maximum_advance_array
        + "\n"
        + advance_function
        + "\n"
        + format_fallback_advances(
            fallback_codepoints, fallback_maximum_advances
        )
        + "\n"
        + "\n".join(arrays)
        + "\n}  // namespace epub_latin_fonts\n"
    )
    OUTPUT_HEADER.write_text(output, encoding="ascii", newline="\n")
    print(f"wrote {OUTPUT_HEADER}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
