#!/usr/bin/env python3
"""Enforce the canonical log-tag set defined in common/include/log_tags.h.

Every log line in the firmware takes the form `[tag] message`. Contract C7
(one helper computes every user-facing string) applies here too: if a tag is
used in code, it must be listed in `log_tags.h`. This script scans firmware
sources for `[tag]` prefixes on `Serial*.print*` and `LOG.print*` calls,
compares them against the canonical set, and fails loudly on any drift.

Run from any of `common/tools/` (or repo root) with no arguments. Exits
non-zero and prints a diff when tags are missing from the header.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOG_TAGS_HEADER = REPO_ROOT / "common" / "include" / "log_tags.h"

# Directories to scan for log lines. Anything under lib/ (third-party libs
# like pngle/miniz/Seeed_GFX) is skipped because their log format is not ours
# to normalise.
SEARCH_DIRS = [
    REPO_ROOT / "weather-viewer" / "src",
    REPO_ROOT / "weather-viewer" / "include",
    REPO_ROOT / "xkcd-viewer" / "src",
    REPO_ROOT / "xkcd-viewer" / "include",
    REPO_ROOT / "photo-viewer" / "src",
    REPO_ROOT / "photo-viewer" / "include",
    REPO_ROOT / "common" / "src",
    REPO_ROOT / "common" / "include",
]

# Match a log call and capture the tag between square brackets at the very
# start of the format string. Tags are lowercase ASCII words (letters, digits,
# and underscore) matching what log_tags.h can hold as C identifiers.
LOG_CALL_RE = re.compile(
    r"""(?:LOG|Serial[0-9]?|appLog)\.print(?:ln|f)?
        \s*\(
        \s*"\[(?P<tag>[a-z][a-z0-9_]*)\]""",
    re.VERBOSE,
)

# Match a raw string-literal log line that starts with `[tag] ` and might be
# passed to LOG via a variable or const string.
BARE_TAG_RE = re.compile(r'"\[(?P<tag>[a-z][a-z0-9_]*)\][ ]')

# Match declarations in log_tags.h. `inline constexpr char BOOT[] = "boot";`.
DECL_RE = re.compile(
    r"""inline\s+constexpr\s+char\s+
        (?P<symbol>[A-Z][A-Z0-9_]*)\s*\[\s*\]
        \s*=\s*"(?P<value>[a-z0-9_]+)";""",
    re.VERBOSE,
)


def canonical_tags() -> set[str]:
    text = LOG_TAGS_HEADER.read_text(encoding="utf-8")
    return {match.group("value") for match in DECL_RE.finditer(text)}


def source_files() -> list[Path]:
    files: list[Path] = []
    for directory in SEARCH_DIRS:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix in {".cpp", ".c", ".h", ".hpp"} and path.is_file():
                # The canonical registry itself is not a caller and would
                # otherwise self-match on its own docstring examples.
                if path.resolve() == LOG_TAGS_HEADER.resolve():
                    continue
                files.append(path)
    return files


def used_tags() -> dict[str, list[tuple[Path, int]]]:
    """Return every tag found in log lines and the (file, lineno) it occurs at."""
    usage: dict[str, list[tuple[Path, int]]] = {}
    for path in source_files():
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), start=1):
            for match in LOG_CALL_RE.finditer(line):
                usage.setdefault(match.group("tag"), []).append((path, lineno))
            for match in BARE_TAG_RE.finditer(line):
                # Skip matches already caught by the LOG_CALL_RE - those
                # occurrences are the same character range, so we only add
                # the tag when it isn't already recorded for this line.
                tag = match.group("tag")
                seen = usage.get(tag, [])
                if not seen or seen[-1] != (path, lineno):
                    seen.append((path, lineno))
                    usage[tag] = seen
    return usage


def main() -> int:
    canonical = canonical_tags()
    if not canonical:
        print(f"error: no tag declarations found in {LOG_TAGS_HEADER}",
              file=sys.stderr)
        return 2
    used = used_tags()

    missing = {tag: locations for tag, locations in used.items()
               if tag not in canonical}
    unused = sorted(canonical - set(used.keys()))

    if missing:
        print("Log tags used in source but missing from log_tags.h:")
        for tag in sorted(missing):
            print(f"  [{tag}]")
            for path, lineno in missing[tag][:5]:
                rel = path.relative_to(REPO_ROOT)
                print(f"    {rel}:{lineno}")
            if len(missing[tag]) > 5:
                print(f"    ... and {len(missing[tag]) - 5} more")
        print()

    if unused:
        print("Log tags declared in log_tags.h but not used anywhere:")
        for tag in unused:
            print(f"  [{tag}]")
        print()

    if missing:
        return 1
    print(f"OK: {len(used)} tags in use, all in log_tags.h.")
    if unused:
        print("(Unused tags are informational, not a failure.)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
