"""PlatformIO pre-script that regenerates weather_icons_data.h when
any of its shared icon inputs change.

Registered by each app that uses the common weather icons. Runs before
compilation so the header sees ``RETERMINAL_MODEL`` at Python-time.

Idempotent: recomputes a stamp file of (model, generator hash, svg
hashes) and skips regeneration when nothing has changed. That keeps
incremental builds fast -- rasterising 26 SVGs at three sizes each
takes ~5 seconds and the header itself is multi-MB text, so avoiding
the rewrite matters for local dev loops.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

Import("env")  # type: ignore  # provided by PlatformIO

PROJECT_DIR = Path(env["PROJECT_DIR"]).resolve()  # noqa: F821
PACKAGE_DIR = PROJECT_DIR.parent / "common-weather"
TOOLS_DIR = PACKAGE_DIR / "tools"
SVG_DIR = PACKAGE_DIR / "icons" / "svg"

# Per-env output dir so parallel `pio run` for different boards can't
# clobber each other's generated headers. The dir is added to the
# env's CPPPATH below so `#include "generated/weather_icons_data.h"`
# in weather_icons.cpp still resolves.
BUILD_DIR = Path(env.subst("$BUILD_DIR"))  # noqa: F821
GENERATED_DIR = BUILD_DIR / "weather_icons_generated"
OUT_PATH = GENERATED_DIR / "generated" / "weather_icons_data.h"
STAMP_PATH = OUT_PATH.with_suffix(".stamp.json")

GENERATOR = TOOLS_DIR / "generate_weather_icons.py"


def _model_from_build_flags() -> int:
    """Parse RETERMINAL_MODEL out of the resolved build_flags. Falls
    back to the platformio.ini default (1001) if the flag isn't set."""
    flags = env.subst("$BUILD_FLAGS")  # noqa: F821
    match = re.search(r"-D\s+RETERMINAL_MODEL(?:=|\s+)(\d+)", flags)
    return int(match.group(1)) if match else 1001


def _hash_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _compute_stamp(model: int) -> dict:
    svg_hashes = {
        p.name: _hash_bytes(p.read_bytes())
        for p in sorted(SVG_DIR.glob("*.svg"))
    }
    return {
        "model": model,
        "generator": _hash_bytes(GENERATOR.read_bytes()),
        "svgs": svg_hashes,
    }


def _stamp_matches(stamp: dict) -> bool:
    if not OUT_PATH.exists() or not STAMP_PATH.exists():
        return False
    try:
        return json.loads(STAMP_PATH.read_text(encoding="utf-8")) == stamp
    except json.JSONDecodeError:
        return False


def _run_generator(model: int) -> None:
    cmd = [sys.executable, str(GENERATOR), str(model), str(OUT_PATH)]
    print(f"common-weather: generating {OUT_PATH.name} for model={model}")
    subprocess.check_call(cmd)


def _main() -> None:
    model = _model_from_build_flags()
    # Make the generated header findable via
    # `#include "generated/weather_icons_data.h"`.
    env.Append(CPPPATH=[str(GENERATED_DIR)])  # noqa: F821
    stamp = _compute_stamp(model)
    if _stamp_matches(stamp):
        print(
            f"common-weather: {OUT_PATH.name} up to date (model={model})"
        )
        return
    _run_generator(model)
    STAMP_PATH.write_text(json.dumps(stamp, indent=2), encoding="utf-8")


_main()
