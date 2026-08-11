from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "levels" / "Microban.txt"
OUTPUT = ROOT / "src" / "microban_levels.h"
VALID_TILES = frozenset(" #$*+.@")


def read_levels() -> list[list[str]]:
    levels: list[list[str]] = []
    current: list[str] = []
    for raw_line in SOURCE.read_text(encoding="utf-8-sig").splitlines():
        if raw_line.startswith(";"):
            continue
        if not raw_line.strip():
            if current:
                levels.append(current)
                current = []
            continue
        current.append(raw_line.rstrip())
    if current:
        levels.append(current)
    return levels


def validate_level(level: list[str], number: int) -> tuple[int, int]:
    width = max(map(len, level))
    height = len(level)
    tiles = "".join(level)
    unexpected = set(tiles) - VALID_TILES
    if unexpected:
        raise ValueError(f"level {number}: unexpected tiles {unexpected!r}")
    players = tiles.count("@") + tiles.count("+")
    boxes = tiles.count("$") + tiles.count("*")
    targets = tiles.count(".") + tiles.count("*") + tiles.count("+")
    if players != 1:
        raise ValueError(f"level {number}: expected one player, found {players}")
    if boxes == 0 or boxes != targets:
        raise ValueError(
            f"level {number}: boxes ({boxes}) and targets ({targets}) differ"
        )
    if width > 30 or height > 17:
        raise ValueError(f"level {number}: unexpected size {width}x{height}")
    return width, height


def generate() -> str:
    levels = read_levels()
    if len(levels) != 155:
        raise ValueError(f"expected 155 Microban levels, found {len(levels)}")

    cell_lines: list[str] = []
    metadata: list[tuple[int, int, int]] = []
    offset = 0
    for number, level in enumerate(levels, start=1):
        width, height = validate_level(level, number)
        metadata.append((offset, width, height))
        cell_lines.append(f"    // Level {number}: {width}x{height}")
        for row in level:
            cell_lines.append(f"    {json.dumps(row.ljust(width))}")
        offset += width * height

    metadata_lines = [
        f"    {{{level_offset}, {width}, {height}}},"
        for level_offset, width, height in metadata
    ]
    return f"""#pragma once

#include <stdint.h>

// Generated from levels/Microban.txt by tools/generate_microban_header.py.
// Microban I contains 155 puzzles by David W. Skinner (April 2000).
// The author permits free distribution provided the collection is credited:
// http://www.abelmartin.com/rj/sokobanJS/Skinner/David%20W.%20Skinner%20-%20Sokoban.htm
// Source mirror:
// https://github.com/OMerkel/Sokoban/blob/master/3rdParty/Levels/Microban.txt
namespace microban_levels {{

struct Level {{
  uint16_t offset;
  uint8_t width;
  uint8_t height;
}};

inline constexpr char kCells[] =
{chr(10).join(cell_lines)}
    ;

inline constexpr Level kLevels[] = {{
{chr(10).join(metadata_lines)}
}};

inline constexpr uint16_t kLevelCount =
    sizeof(kLevels) / sizeof(kLevels[0]);
inline constexpr uint16_t kCellDataSize = sizeof(kCells) - 1;

static_assert(kLevelCount == 155, "Microban level count changed");
static_assert(kCellDataSize == {offset}, "Microban cell data size changed");

}}  // namespace microban_levels
"""


def main() -> None:
    OUTPUT.write_text(generate(), encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
