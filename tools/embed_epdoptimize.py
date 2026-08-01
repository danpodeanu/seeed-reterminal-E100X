#!/usr/bin/env python3
"""Bake paperlesspaper/epdoptimize into a C++ source file.

The /upload-photo page in common-sd-web imports epdoptimize for calibrated
Spectra 6 dithering. To avoid depending on an external CDN at runtime we
embed the library into the firmware as a gzipped byte array and serve it
from ``/epdoptimize.mjs`` with ``Content-Encoding: gzip``.

Usage:
    python tools/embed_epdoptimize.py [--version 1.3.0]

Downloads ``epdoptimize@<version>/dist/index.mjs`` from jsDelivr, gzips it,
and rewrites ``common-sd-web/src/epdoptimize_js.cpp`` in place. Commit the
result. Run this again whenever bumping the pinned version.
"""

from __future__ import annotations

import argparse
import gzip
import pathlib
import sys
import urllib.request

DEFAULT_VERSION = "1.3.0"
CDN_URL = "https://cdn.jsdelivr.net/npm/epdoptimize@{version}/dist/index.mjs"

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT_PATH = REPO_ROOT / "common-sd-web" / "src" / "epdoptimize_js.cpp"


def fetch(version: str) -> bytes:
    url = CDN_URL.format(version=version)
    print(f"fetching {url}")
    with urllib.request.urlopen(url, timeout=30) as resp:
        return resp.read()


def render(gz_bytes: bytes, version: str) -> str:
    header = [
        f"// Auto-generated from paperlesspaper/epdoptimize v{version} dist/index.mjs (gzipped).",
        "// Do not edit by hand; regenerate with tools/embed_epdoptimize.py.",
        "//",
        "// The gzip wrapper roughly cuts the flash footprint in half (~140 KB -> ~40 KB)",
        "// while browsers still decompress the module transparently via Content-Encoding.",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <pgmspace.h>",
        "",
        "namespace sd_web_portal {",
        "",
        "extern const uint8_t kEpdoptimizeJsGz[] PROGMEM = {",
    ]
    for i in range(0, len(gz_bytes), 16):
        chunk = gz_bytes[i : i + 16]
        header.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    header.append("};")
    header.append(f"extern const size_t kEpdoptimizeJsGzLen = {len(gz_bytes)};")
    header.append("")
    header.append("}  // namespace sd_web_portal")
    header.append("")
    return "\n".join(header)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=DEFAULT_VERSION,
                        help=f"epdoptimize version (default {DEFAULT_VERSION})")
    args = parser.parse_args(argv)

    raw = fetch(args.version)
    gz = gzip.compress(raw, compresslevel=9)
    print(f"raw {len(raw)} bytes -> gzip {len(gz)} bytes")

    text = render(gz, args.version)
    OUT_PATH.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT_PATH} ({OUT_PATH.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
