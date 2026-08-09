#!/usr/bin/env python3
"""Generate the BMP CJK font used by the Games EPUB reader."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_FONT = ROOT / "tools" / "fonts" / "NotoSansCJKsc-Bold.otf"
EXPECTED_SHA256 = "b5f0d1a190a7f9b43c310a8850630af12553df32c4c050543f9059732d9b4c0a"
OUTPUT_NAME = "epub_cjk_16.vlw"


def load_vlw_module():
    module_path = ROOT / "tools" / "fonts" / "make_vlw.py"
    spec = importlib.util.spec_from_file_location("make_vlw", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "sd_root",
        type=Path,
        help="Mounted SD-card root; the font is written beneath its fonts folder.",
    )
    parser.add_argument(
        "--font",
        type=Path,
        default=DEFAULT_FONT,
        help=f"Noto Sans CJK SC Bold source (default: {DEFAULT_FONT}).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.sd_root.is_dir():
        raise SystemExit(f"SD-card root does not exist: {args.sd_root}")
    if not args.font.is_file():
        raise SystemExit(
            f"{args.font} is missing; download the pinned Noto Sans CJK SC "
            "Bold source documented in tools/fonts/README.md"
        )
    digest = hashlib.sha256(args.font.read_bytes()).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(
            f"{args.font} has SHA-256 {digest}, expected {EXPECTED_SHA256}"
        )

    make_vlw = load_vlw_module()
    codepoints = [
        codepoint
        for codepoint in make_vlw.enumerate_codepoints(args.font)
        if codepoint <= 0xFFFF
    ]
    if len(codepoints) > 0xFFFF:
        raise SystemExit("the BMP glyph count exceeds the renderer's 16-bit limit")

    output_dir = args.sd_root / "fonts"
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / OUTPUT_NAME
    temporary = output.with_suffix(".tmp")
    blob = make_vlw.build_vlw(
        args.font, 16, codepoints=codepoints, family_name="EpubCjk"
    )
    temporary.write_bytes(blob)
    temporary.replace(output)
    print(
        f"wrote {output} ({len(blob):,} bytes, "
        f"{len(codepoints):,} BMP glyphs)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
