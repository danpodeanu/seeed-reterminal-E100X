from __future__ import annotations

import json
import random
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "levels" / "minicross_emoji_clues.json"
OUTPUT = ROOT / "src" / "crossword_puzzles.h"

GRID_SIZE = 5
PUZZLE_COUNT = 100
RANDOM_SEED = 0xC2055

CURATED_GRIDS = [
    ["SWEEP", "A#N#A", "WATER", "##EAT", "BERRY"],
    ["CLUBS", "###E#", "STEAK", "SWAN#", "HORSE"],
    ["B#SSH", "ELK#M", "A#AIM", "NOT##", "SHELL"],
    ["S#SSH", "LUCK#", "E#AIM", "EAR#I", "P#FAX"],
    ["STARS", "O##UP", "CRANE", "K#X#A", "STEAK"],
    ["MAP#W", "AIR#H", "SMILE", "K#Z#A", "SWEET"],
    ["SWEET", "OH#C#", "BATHE", "#LION", "BEE#D"],
    ["DROPS", "R##I#", "ENTER", "SAW#U", "SPOON"],
    ["A#LOG", "ICE#R", "RAM#A", "#SOAP", "PEN#H"],
    ["SPADE", "MIC#A", "ENTER", "L###T", "L#SSH"],
    ["SOCKS", "A#AIM", "WASTE", "#REEL", "#M##L"],
    ["MASKS", "A#P#L", "PLATE", "##DIE", "SWEEP"],
]

CLUES = {
    "ACT": "Perform on stage",
    "AIM": "Point toward a target",
    "AIR": "What we breathe",
    "ARM": "Limb from shoulder to hand",
    "AXE": "Wood-chopping tool",
    "BATHE": "Wash in water",
    "BEANS": "Seeds used in chili",
    "BEE": "Honey-making insect",
    "BERRY": "Small juicy fruit",
    "CASE": "Container or situation",
    "CLUBS": "Groups with members",
    "CRANE": "Tall lifting machine",
    "DIE": "Single numbered cube",
    "DRESS": "One-piece garment",
    "DROPS": "Small beads of liquid",
    "EAR": "Organ used for hearing",
    "EARTH": "Our home planet",
    "EAT": "Have a meal",
    "ECHO": "A repeated sound",
    "ELK": "Large antlered animal",
    "END": "Final part",
    "ENTER": "Go inside",
    "FAX": "Send a document by phone line",
    "GRAPH": "Chart with plotted data",
    "HMM": "Sound of thinking",
    "HORSE": "Animal ridden with a saddle",
    "ICE": "Frozen water",
    "KITE": "Toy flown on a string",
    "LEMON": "Sour yellow fruit",
    "LION": "Big cat with a mane",
    "LOG": "Cut tree trunk",
    "LUCK": "Good fortune",
    "MAP": "Guide showing places",
    "MASKS": "Face coverings",
    "MIC": "Short name for microphone",
    "MIX": "Blend together",
    "NAP": "Short sleep",
    "NOT": "Word that makes a negative",
    "OH": "Cry of surprise",
    "PARTY": "Celebration with guests",
    "PEN": "Tool filled with ink",
    "PIE": "Baked dish with a crust",
    "PIN": "Small sharp fastener",
    "PLATE": "Flat dish for food",
    "PRIZE": "Award for a winner",
    "RAM": "Male sheep",
    "REEL": "Spool for fishing line",
    "RUN": "Move faster than a walk",
    "SAW": "Cutting tool with teeth",
    "SCARF": "Cloth worn around the neck",
    "SHELL": "Hard outer covering",
    "SKATE": "Glide on an ice blade",
    "SKI": "Glide over snow",
    "SLEEP": "Rest with eyes closed",
    "SMELL": "Sense used by the nose",
    "SMILE": "Happy facial expression",
    "SOAP": "Used for washing",
    "SOB": "Cry with short breaths",
    "SOCKS": "Clothing worn inside shoes",
    "SPADE": "Small digging tool",
    "SPEAK": "Say words aloud",
    "SPOON": "Round-ended eating utensil",
    "SSH": "Command meaning be quiet",
    "STARS": "Lights in the night sky",
    "STEAK": "Thick slice of meat",
    "SWAN": "Large graceful water bird",
    "SWEEP": "Clean with a broom",
    "SWEET": "Sugary in taste",
    "TIE": "Neckwear with a knot",
    "TWO": "One plus one",
    "UP": "Opposite of down",
    "WASTE": "Use carelessly",
    "WATER": "Clear liquid we drink",
    "WHALE": "Very large sea mammal",
    "WHEAT": "Grain used to make flour",
}


def slot_cells(slot: tuple[int, int, int, int]) -> list[tuple[int, int]]:
    direction, row, column, length = slot
    return [
        (row + index if direction else row, column if direction else column + index)
        for index in range(length)
    ]


def pattern_is_connected_and_covered(pattern: list[list[bool]]) -> bool:
    open_cells = {
        (row, column)
        for row in range(GRID_SIZE)
        for column in range(GRID_SIZE)
        if pattern[row][column]
    }
    pattern_slots = slots(
        ["".join("." if cell else "#" for cell in row) for row in pattern]
    )
    covered_cells = {
        cell for slot in pattern_slots for cell in slot_cells(slot[:3] + (len(slot[3]),))
    }
    if len(pattern_slots) < 6 or not open_cells or covered_cells != open_cells:
        return False

    connected = {next(iter(open_cells))}
    pending = list(connected)
    while pending:
        row, column = pending.pop()
        for neighbor in (
            (row - 1, column),
            (row + 1, column),
            (row, column - 1),
            (row, column + 1),
        ):
            if neighbor in open_cells and neighbor not in connected:
                connected.add(neighbor)
                pending.append(neighbor)
    return connected == open_cells


def solve_pattern(
    pattern: list[list[bool]],
    words_by_length: dict[int, list[str]],
    rng: random.Random,
    max_backtracks: int = 100_000,
) -> dict[tuple[int, int, int, int], str] | None:
    pattern_grid = [
        "".join("." if cell else "#" for cell in row) for row in pattern
    ]
    pattern_slots = [
        (direction, row, column, len(answer))
        for direction, row, column, answer in slots(pattern_grid)
    ]
    candidates = {
        slot: set(words_by_length.get(slot[3], [])) for slot in pattern_slots
    }
    intersections: dict[
        tuple[int, int, int, int],
        list[tuple[tuple[int, int, int, int], int, int]],
    ] = {slot: [] for slot in pattern_slots}
    cells_to_slots: dict[
        tuple[int, int], list[tuple[tuple[int, int, int, int], int]]
    ] = {}
    for slot in pattern_slots:
        for position, cell in enumerate(slot_cells(slot)):
            cells_to_slots.setdefault(cell, []).append((slot, position))
    for crossing in cells_to_slots.values():
        if len(crossing) != 2:
            continue
        (first, first_position), (second, second_position) = crossing
        intersections[first].append((second, first_position, second_position))
        intersections[second].append((first, second_position, first_position))

    assignment: dict[tuple[int, int, int, int], str] = {}
    backtracks = 0

    def backtrack() -> bool:
        nonlocal backtracks
        backtracks += 1
        if backtracks > max_backtracks:
            return False
        if len(assignment) == len(pattern_slots):
            return True

        unfilled = [slot for slot in pattern_slots if slot not in assignment]
        rng.shuffle(unfilled)
        slot = min(unfilled, key=lambda candidate: len(candidates[candidate]))
        available = sorted(candidates[slot] - set(assignment.values()))
        rng.shuffle(available)
        for word in available:
            saved_candidates: dict[tuple[int, int, int, int], set[str]] = {}
            assignment[slot] = word
            failed = False
            for other, word_position, other_position in intersections[slot]:
                if other in assignment:
                    if assignment[other][other_position] != word[word_position]:
                        failed = True
                        break
                    continue
                matching = {
                    candidate
                    for candidate in candidates[other]
                    if candidate[other_position] == word[word_position]
                }
                if not matching:
                    failed = True
                    break
                saved_candidates[other] = candidates[other]
                candidates[other] = matching
            if not failed and backtrack():
                return True
            for other, previous in saved_candidates.items():
                candidates[other] = previous
            del assignment[slot]
        return False

    return assignment if backtrack() else None


def fill_grid(
    pattern: list[list[bool]],
    assignment: dict[tuple[int, int, int, int], str],
) -> list[str]:
    grid = [
        ["?" if pattern[row][column] else "#" for column in range(GRID_SIZE)]
        for row in range(GRID_SIZE)
    ]
    for slot, word in assignment.items():
        for (row, column), letter in zip(slot_cells(slot), word):
            grid[row][column] = letter
    if any("?" in row for row in grid):
        raise ValueError("generated crossword contains an unfilled cell")
    return ["".join(row) for row in grid]


def build_grids() -> list[list[str]]:
    grids = [list(grid) for grid in CURATED_GRIDS]
    seen = {"".join(grid) for grid in grids}
    words_by_length = {
        length: sorted(word for word in CLUES if len(word) == length)
        for length in range(2, GRID_SIZE + 1)
    }
    rng = random.Random(RANDOM_SEED)
    attempts = 0
    while len(grids) < PUZZLE_COUNT and attempts < 500_000:
        attempts += 1
        black_count = rng.randint(3, 9)
        black_cells = set(rng.sample(range(GRID_SIZE * GRID_SIZE), black_count))
        pattern = [
            [
                row * GRID_SIZE + column not in black_cells
                for column in range(GRID_SIZE)
            ]
            for row in range(GRID_SIZE)
        ]
        if not pattern_is_connected_and_covered(pattern):
            continue
        assignment = solve_pattern(pattern, words_by_length, rng)
        if assignment is None:
            continue
        grid = fill_grid(pattern, assignment)
        serialized = "".join(grid)
        if serialized in seen:
            continue
        seen.add(serialized)
        grids.append(grid)
    if len(grids) != PUZZLE_COUNT:
        raise RuntimeError(
            f"generated only {len(grids)} of {PUZZLE_COUNT} crossword puzzles"
        )
    return grids


def slots(grid: list[str]) -> list[tuple[int, int, int, str]]:
    height = len(grid)
    width = len(grid[0])
    result: list[tuple[int, int, int, str]] = []
    for row in range(height):
        column = 0
        while column < width:
            if grid[row][column] == "#":
                column += 1
                continue
            start = column
            while column < width and grid[row][column] != "#":
                column += 1
            if column - start >= 2:
                result.append((0, row, start, grid[row][start:column]))
    for column in range(width):
        row = 0
        while row < height:
            if grid[row][column] == "#":
                row += 1
                continue
            start = row
            answer = ""
            while row < height and grid[row][column] != "#":
                answer += grid[row][column]
                row += 1
            if row - start >= 2:
                result.append((1, start, column, answer))
    return result


def generate() -> str:
    source_words = {
        word.upper()
        for word in json.loads(SOURCE.read_text(encoding="utf-8")).keys()
    }
    solution_data = ""
    clue_text = ""
    puzzle_lines: list[str] = []
    clue_lines: list[str] = []
    clue_offset = 0
    grids = build_grids()
    for number, grid in enumerate(grids, start=1):
        width = len(grid[0])
        if not 1 <= len(grid) <= 9 or not 1 <= width <= 9:
            raise ValueError(f"puzzle {number}: dimensions exceed 9x9")
        if any(len(row) != width for row in grid):
            raise ValueError(f"puzzle {number}: ragged grid")
        puzzle_slots = slots(grid)
        puzzle_lines.append(
            f"    {{{len(solution_data)}, {clue_offset}, {width}, "
            f"{len(grid)}, {len(puzzle_slots)}}},"
        )
        solution_data += "".join(grid)
        for direction, row, column, answer in puzzle_slots:
            if answer not in source_words:
                raise ValueError(f"puzzle {number}: {answer} missing from source")
            if answer not in CLUES:
                raise ValueError(f"puzzle {number}: {answer} has no text clue")
            text = CLUES[answer]
            start_index = row * width + column
            clue_lines.append(
                f"    {{{len(clue_text)}, {start_index}, {len(answer)}, "
                f"{direction}}},"
            )
            clue_text += text + "\0"
            clue_offset += 1

    return f"""#pragma once

#include <stdint.h>

// Generated from the MIT-licensed cout/minicross word database by
// tools/generate_crossword_header.py. Text clues are original to this project.
namespace crossword_puzzles {{

struct Puzzle {{
  uint16_t solutionOffset;
  uint16_t clueOffset;
  uint8_t width;
  uint8_t height;
  uint8_t clueCount;
}};

struct Clue {{
  uint16_t textOffset;
  uint8_t startIndex;
  uint8_t length;
  uint8_t direction;
}};

inline constexpr char kSolutions[] =
    {json.dumps(solution_data)};

inline constexpr char kClueText[] =
    {json.dumps(clue_text)};

inline constexpr Puzzle kPuzzles[] = {{
{chr(10).join(puzzle_lines)}
}};

inline constexpr Clue kClues[] = {{
{chr(10).join(clue_lines)}
}};

inline constexpr uint8_t kPuzzleCount =
    sizeof(kPuzzles) / sizeof(kPuzzles[0]);
static_assert(kPuzzleCount == {len(grids)}, "crossword puzzle count changed");

}}  // namespace crossword_puzzles
"""


def main() -> None:
    OUTPUT.write_text(generate(), encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
