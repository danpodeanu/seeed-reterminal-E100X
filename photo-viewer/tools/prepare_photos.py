#!/usr/bin/env python3
"""Prepare photos for direct, high-quality display on reTerminal E100X panels."""

from __future__ import annotations

import argparse
import io
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageChops, ImageCms, ImageOps


@dataclass(frozen=True)
class DisplayProfile:
    width: int
    height: int
    colors: tuple[tuple[int, int, int], ...]
    description: str


E6_COLORS = (
    (255, 255, 255),
    (29, 185, 84),
    (229, 57, 53),
    (255, 216, 0),
    (0, 76, 255),
    (0, 0, 0),
)

PROFILES = {
    "e1001": DisplayProfile(
        800,
        480,
        tuple((level, level, level) for level in (0, 85, 170, 255)),
        "800x480, 4-level grayscale",
    ),
    "e1002": DisplayProfile(800, 480, E6_COLORS, "800x480, six color"),
    "e1003": DisplayProfile(
        1872,
        1404,
        tuple((level, level, level) for level in range(0, 256, 17)),
        "1872x1404, 16-level grayscale",
    ),
    "e1004": DisplayProfile(1200, 1600, E6_COLORS, "1200x1600, six color"),
}

SUPPORTED_SUFFIXES = {
    ".bmp",
    ".gif",
    ".jpeg",
    ".jpg",
    ".png",
    ".tif",
    ".tiff",
    ".webp",
}


def parse_color(value: str) -> tuple[int, int, int]:
    text = value.removeprefix("#")
    if len(text) != 6:
        raise argparse.ArgumentTypeError("background must be RRGGBB or #RRGGBB")
    try:
        return tuple(int(text[index : index + 2], 16) for index in (0, 2, 4))
    except ValueError as error:
        raise argparse.ArgumentTypeError("background contains non-hex digits") from error


def input_files(paths: Iterable[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_dir():
            files.extend(
                candidate
                for candidate in sorted(path.rglob("*"))
                if candidate.is_file()
                and candidate.suffix.lower() in SUPPORTED_SUFFIXES
            )
        elif path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES:
            files.append(path)
        else:
            print(f"warning: skipping unsupported or missing input: {path}", file=sys.stderr)
    return files


def convert_to_srgb(image: Image.Image) -> Image.Image:
    icc_profile = image.info.get("icc_profile")
    if not icc_profile:
        return image.convert("RGB")
    try:
        source = ImageCms.ImageCmsProfile(io.BytesIO(icc_profile))
        destination = ImageCms.createProfile("sRGB")
        return ImageCms.profileToProfile(
            image, source, destination, outputMode="RGB"
        )
    except (ImageCms.PyCMSError, OSError, ValueError):
        print("warning: embedded ICC profile could not be converted; assuming sRGB",
              file=sys.stderr)
        return image.convert("RGB")


def load_photo(path: Path, background: tuple[int, int, int]) -> Image.Image:
    with Image.open(path) as opened:
        opened.seek(0)
        image = ImageOps.exif_transpose(opened)
        if image.mode in ("RGBA", "LA") or "transparency" in image.info:
            rgba = image.convert("RGBA")
            canvas = Image.new("RGBA", rgba.size, (*background, 255))
            image = Image.alpha_composite(canvas, rgba).convert("RGB")
        else:
            image = convert_to_srgb(image)
        return image.copy()


def resize_photo(
    image: Image.Image,
    profile: DisplayProfile,
    fit: str,
    background: tuple[int, int, int],
) -> Image.Image:
    size = (profile.width, profile.height)
    if fit == "cover":
        return ImageOps.fit(
            image,
            size,
            method=Image.Resampling.LANCZOS,
            centering=(0.5, 0.5),
        )
    contained = ImageOps.contain(image, size, method=Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", size, background)
    canvas.paste(
        contained,
        ((profile.width - contained.width) // 2,
         (profile.height - contained.height) // 2),
    )
    return canvas


def apply_gamma(image: Image.Image, gamma: float) -> Image.Image:
    if abs(gamma - 1.0) < 0.0001:
        return image
    # Values above 1 brighten midtones, which can help compensate for a dark
    # photographic source on a reflective e-paper panel.
    lookup = [
        round(255 * ((value / 255) ** (1.0 / gamma)))
        for value in range(256)
    ]
    return image.point(lookup * 3)


def fixed_palette_image(colors: tuple[tuple[int, int, int], ...]) -> Image.Image:
    palette = [component for color in colors for component in color]
    palette.extend([component for color in colors[-1:] for component in color] *
                   (256 - len(colors)))
    image = Image.new("P", (1, 1))
    image.putpalette(palette)
    return image


def quantize_for_panel(
    image: Image.Image,
    colors: tuple[tuple[int, int, int], ...],
    dither: bool,
) -> Image.Image:
    palette = fixed_palette_image(colors)
    quantized = image.quantize(
        palette=palette,
        dither=(
            Image.Dither.FLOYDSTEINBERG
            if dither
            else Image.Dither.NONE
        ),
    )

    # Pillow may select one of the duplicate padding entries in the 256-entry
    # palette. Collapse every possible index to the canonical 0..15 range used
    # by the compact 4-bit BMP writer.
    source_palette = quantized.getpalette()
    remap: list[int] = []
    for index in range(256):
        offset = index * 3
        source_color = tuple(source_palette[offset : offset + 3])
        best = min(
            range(len(colors)),
            key=lambda candidate: sum(
                (source_color[channel] - colors[candidate][channel]) ** 2
                for channel in range(3)
            ),
        )
        remap.append(best)
    return quantized.point(remap)


def remap_indices(image: Image.Image, mapping: tuple[int, ...]) -> Image.Image:
    lookup = list(range(256))
    lookup[: len(mapping)] = mapping
    return image.point(lookup)


def warm_tone_mask(image: Image.Image) -> Image.Image:
    """Return a mask selecting visible, meaningfully saturated warm pixels.

    A normal RGB error diffuser sometimes uses green ink to correct the error
    left by a red skin-tone dot. That may be mathematically reasonable but is
    conspicuous on a sparse six-color e-paper palette. Restrict warm hues to
    white, red, yellow, and black inks.
    """
    hue, saturation, value = image.convert("HSV").split()
    hue_mask = hue.point(
        [255 if channel <= 50 or channel >= 246 else 0 for channel in range(256)]
    )
    saturation_mask = saturation.point(
        [255 if channel >= 20 else 0 for channel in range(256)]
    )
    visible_mask = value.point(
        [255 if channel >= 35 else 0 for channel in range(256)]
    )
    return ImageChops.multiply(
        ImageChops.multiply(hue_mask, saturation_mask), visible_mask
    )


def quantize_six_color_photo(
    image: Image.Image,
    dither: bool,
    protect_warm_tones: bool,
) -> Image.Image:
    full = quantize_for_panel(image, E6_COLORS, dither)
    if not protect_warm_tones:
        return full

    warm_colors = (
        E6_COLORS[0],  # white
        E6_COLORS[2],  # red
        E6_COLORS[3],  # yellow
        E6_COLORS[5],  # black
    )
    warm = quantize_for_panel(image, warm_colors, dither)

    # Convert the temporary palette positions to the canonical E6 positions.
    warm = remap_indices(warm, (0, 2, 3, 5))
    full.paste(warm, mask=warm_tone_mask(image))
    full.putpalette(fixed_palette_image(E6_COLORS).getpalette())
    return full


def quantize_grayscale_photo(
    image: Image.Image,
    colors: tuple[tuple[int, int, int], ...],
    dither: bool,
) -> Image.Image:
    # Fixed-palette RGB quantization measures distance across three channels.
    # Explicit luminance conversion produces more natural skin brightness and
    # cleaner monochrome error diffusion.
    luminance = ImageOps.grayscale(image).convert("RGB")
    return quantize_for_panel(luminance, colors, dither)


def write_4bit_bmp(
    output: Path,
    indexed: Image.Image,
    colors: tuple[tuple[int, int, int], ...],
) -> None:
    width, height = indexed.size
    packed_width = (width + 1) // 2
    row_stride = (packed_width + 3) & ~3
    pixel_size = row_stride * height
    palette_size = 16 * 4
    pixel_offset = 14 + 40 + palette_size
    file_size = pixel_offset + pixel_size

    palette = list(colors)
    palette.extend([colors[-1]] * (16 - len(palette)))
    pixels = indexed.load()

    with output.open("wb") as handle:
        handle.write(struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset))
        handle.write(
            struct.pack(
                "<IiiHHIIiiII",
                40,
                width,
                height,
                1,
                4,
                0,
                pixel_size,
                2835,
                2835,
                16,
                len(colors),
            )
        )
        for red, green, blue in palette:
            handle.write(bytes((blue, green, red, 0)))

        padding = b"\0" * (row_stride - packed_width)
        for y in range(height - 1, -1, -1):
            row = bytearray(packed_width)
            for x in range(0, width, 2):
                left = int(pixels[x, y]) & 0x0F
                right = int(pixels[x + 1, y]) & 0x0F if x + 1 < width else 0
                row[x // 2] = (left << 4) | right
            handle.write(row)
            handle.write(padding)


def unique_output_path(output_dir: Path, source: Path, model: str) -> Path:
    base = "".join(
        character if character.isalnum() or character in "-_" else "-"
        for character in source.stem
    ).strip("-_") or "photo"
    candidate = output_dir / f"{base}-{model}.bmp"
    sequence = 2
    while candidate.exists():
        candidate = output_dir / f"{base}-{model}-{sequence}.bmp"
        sequence += 1
    return candidate


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Resize and panel-dither photos into direct-display 4-bit BMPs "
            "for a Seeed reTerminal E100X."
        )
    )
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="image files and/or directories (recursive)")
    parser.add_argument("--model", required=True, choices=sorted(PROFILES),
                        help="target display model")
    parser.add_argument("--output", required=True, type=Path,
                        help="output directory, normally the SD card's /photos")
    parser.add_argument(
        "--fit",
        choices=("cover", "contain"),
        default="cover",
        help="fill and center-crop (default), or letterbox the complete photo",
    )
    parser.add_argument(
        "--background",
        type=parse_color,
        default=(255, 255, 255),
        metavar="RRGGBB",
        help="letterbox/transparency color (default: FFFFFF)",
    )
    parser.add_argument(
        "--gamma",
        type=float,
        default=1.0,
        help="mid-tone adjustment; values above 1 brighten (default: 1.0)",
    )
    parser.add_argument(
        "--no-dither",
        action="store_true",
        help="disable Floyd-Steinberg error-diffusion dithering",
    )
    parser.add_argument(
        "--no-warm-tone-protection",
        action="store_true",
        help=(
            "allow green/blue correction dots in warm areas on six-color "
            "panels; normally disabled to improve skin tones"
        ),
    )
    parser.add_argument(
        "--overwrite",
        action="store_true",
        help="replace an existing same-named output instead of adding a suffix",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.gamma <= 0:
        raise SystemExit("--gamma must be greater than zero")

    files = input_files(args.inputs)
    if not files:
        raise SystemExit("no supported input photos found")

    profile = PROFILES[args.model]
    args.output.mkdir(parents=True, exist_ok=True)
    print(f"Target {args.model.upper()}: {profile.description}")
    print(
        f"Fit: {args.fit}; dither: "
        f"{'off' if args.no_dither else 'Floyd-Steinberg'}"
    )
    if profile.colors == E6_COLORS:
        print(
            "Six-color warm-tone protection: "
            f"{'off' if args.no_warm_tone_protection else 'on'}"
        )

    converted = 0
    failed = 0
    for source in files:
        try:
            destination = args.output / f"{source.stem}-{args.model}.bmp"
            if destination.exists() and not args.overwrite:
                destination = unique_output_path(args.output, source, args.model)
            photo = load_photo(source, args.background)
            photo = resize_photo(photo, profile, args.fit, args.background)
            photo = apply_gamma(photo, args.gamma)
            if profile.colors == E6_COLORS:
                indexed = quantize_six_color_photo(
                    photo,
                    not args.no_dither,
                    not args.no_warm_tone_protection,
                )
            else:
                indexed = quantize_grayscale_photo(
                    photo, profile.colors, not args.no_dither
                )
            write_4bit_bmp(destination, indexed, profile.colors)
            converted += 1
            print(
                f"[{converted}/{len(files)}] {source} -> {destination} "
                f"({destination.stat().st_size / 1024:.0f} KiB)"
            )
        except (OSError, ValueError) as error:
            failed += 1
            print(f"error: {source}: {error}", file=sys.stderr)

    print(f"Prepared {converted} photo(s); {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
