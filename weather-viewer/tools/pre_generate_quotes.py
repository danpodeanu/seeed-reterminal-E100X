"""PlatformIO pre-script that regenerates weather_quotes_data.h when
data/quotes.txt or the generator itself changes.

Mirrors pre_generate_icons.py but for the footer proverb table. The
header is board-agnostic (unlike the icons), so the stamp only tracks
generator + quote file hashes.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path

Import("env")  # type: ignore  # provided by PlatformIO

PROJECT_DIR = Path(env["PROJECT_DIR"])  # noqa: F821
TOOLS_DIR = PROJECT_DIR / "tools"
QUOTES_TXT = PROJECT_DIR / "data" / "quotes.txt"

BUILD_DIR = Path(env.subst("$BUILD_DIR"))  # noqa: F821
GENERATED_DIR = BUILD_DIR / "weather_quotes_generated"
OUT_PATH = GENERATED_DIR / "generated" / "weather_quotes_data.h"
STAMP_PATH = OUT_PATH.with_suffix(".stamp.json")

GENERATOR = TOOLS_DIR / "generate_weather_quotes.py"


def _hash_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _compute_stamp() -> dict:
    return {
        "generator": _hash_bytes(GENERATOR.read_bytes()),
        "quotes": _hash_bytes(QUOTES_TXT.read_bytes()),
    }


def _stamp_matches(stamp: dict) -> bool:
    if not OUT_PATH.exists() or not STAMP_PATH.exists():
        return False
    try:
        return json.loads(STAMP_PATH.read_text(encoding="utf-8")) == stamp
    except json.JSONDecodeError:
        return False


def _run_generator() -> None:
    cmd = [sys.executable, str(GENERATOR), str(QUOTES_TXT), str(OUT_PATH)]
    print(f"weather-viewer: generating {OUT_PATH.name}")
    subprocess.check_call(cmd)


def _main() -> None:
    env.Append(CPPPATH=[str(GENERATED_DIR)])  # noqa: F821
    stamp = _compute_stamp()
    if _stamp_matches(stamp):
        print(f"weather-viewer: {OUT_PATH.name} up to date")
        return
    _run_generator()
    STAMP_PATH.write_text(json.dumps(stamp, indent=2), encoding="utf-8")


_main()
