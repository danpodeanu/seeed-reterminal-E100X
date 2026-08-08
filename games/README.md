# Games

Touch-friendly games for the Seeed reTerminal E1005
("Seeed Sticky"). The app opens on a game-selection screen so additional games
can be added without installing separate firmware.

| First selector page | Mini Minesweeper |
| --- | --- |
| ![First Games selector page on a reTerminal E1005](assets/e1005-games-menu.png) | ![Mini Minesweeper running on a reTerminal E1005](assets/e1005-minesweeper.png) |

The two selector pages contain:

| Game | Page | Board or collection |
| --- | ---: | --- |
| Lights Out | 1 | Solvable generated 5x5 puzzles |
| 2048 | 1 | 4x4 sliding-tile board |
| Pipe Connect | 1 | Solvable generated 6x6 networks |
| Mini Minesweeper | 1 | First-tap-safe 6x6 fields with six mines |
| Nonogram / Picross | 1 | Generated 5x5 pictures |
| Reversi / Othello | 1 | 8x8, one-player AI or two-player mode |
| Dots and Boxes | 2 | Two-player 5x5 box board |
| Sokoban | 2 | All 155 Microban I levels |
| Peg Solitaire | 2 | Classic 33-hole English board |
| Slitherlink | 2 | Bundled 5x5 logic puzzles |
| Sudoku | 2 | 12 uniquely solvable easy 9x9 puzzles |
| Crossword | 2 | 12 easy 5x5 mini crosswords; engine supports up to 9x9 |

## Included games

### Lights Out

Tap a square to toggle it and its horizontal and vertical neighbours. Turn all
25 squares off to solve the puzzle. Every generated puzzle is solvable because
it is produced by applying a random sequence of legal moves to a cleared board.

Touch controls:

- Tap the Lights Out icon on the game-selection screen to play.
- Tap a board square to make a move.
- Tap **NEW** for another puzzle or **RESET** to restore the current puzzle.
- Tap the back arrow to save the current puzzle and return to the selection screen.
  Opening Lights Out again resumes that puzzle.

### 2048

Swipe the four-by-four board up, down, left, or right to slide matching tiles
together. A new 2 or 4 tile appears after each move that changes the board.
The score, best score, board, win state, and game-over state are restored when
you return from the selection screen or deep sleep.

Touch controls:

- Tap the 2048 icon on the game-selection screen to play or resume.
- Swipe across the board to move all tiles.
- Tap **NEW** to start over while retaining the best score.
- Tap the back arrow to save and return to the selection screen.

### Pipe Connect

Tap tiles to rotate their pipes until all 36 tiles form one connected network.
Every generated board starts from a connected spanning tree, so it always has a
solution. The current board and tap count persist across menu returns and deep
sleep.

Touch controls:

- Tap the Pipe Connect icon on the game-selection screen to play or resume.
- Tap a tile to rotate it clockwise.
- Tap **NEW** for another network or **RESET** to restore the current scramble.
- Tap the back arrow to save and return to the selection screen.

### Mini Minesweeper

Clear a six-by-six field containing six mines. The first revealed tile and its
neighbours are always safe. Empty areas open automatically, and the board,
flags, and win or loss state persist across menu returns and deep sleep.

Touch controls:

- Tap the Minesweeper icon on the game-selection screen to play or resume.
- Tap a covered tile to reveal it.
- Hold a covered tile for 650 ms, then release, to add or remove a flag.
- Tap a revealed number once that many adjacent tiles are flagged to open the
  other adjacent tiles.
- Tap **NEW** for another field.
- Tap the back arrow to save and return to the selection screen.

### Nonogram / Picross

Use the number clues above and beside the five-by-five grid to reveal the hidden
picture. Each clue describes a consecutive run of filled cells; separate clues
have at least one empty cell between them. The current puzzle and marks persist
across menu returns and deep sleep.

Touch controls:

- Tap the Nonogram icon on the game-selection screen to play or resume.
- Tap a cell to cycle it from blank, to filled, to crossed, then back to blank.
- Tap **NEW** for a different picture or **RESET** to clear the current grid.
- Tap the back arrow to save and return to the selection screen.

### Reversi / Othello

Two players take turns placing black and white discs on the eight-by-eight
board. A move must trap one or more opposing discs between the new disc and
another disc of the same colour; every trapped line is flipped. The player with
the most discs when neither player can move wins. One-player mode is the
default: the user plays Black and the built-in AI plays White. Two-player mode
keeps both colours under touch control.

Touch controls:

- Tap the Reversi icon on the game-selection screen to play or resume.
- Tap a dotted legal-move cell to place the current player's disc.
- Tap **1 PLAYER** or **2 PLAYERS** to switch modes and begin a new match.
- A player with no legal move passes automatically.
- Tap **NEW** to return to the standard four-disc opening.
- Tap the back arrow to save and return to the selection screen.

### Dots and Boxes

Two players take turns drawing one edge between adjacent dots. Completing a box
claims it and grants another turn. The player with the most boxes after all
edges have been drawn wins.

Touch controls:

- Tap near a horizontal or vertical edge to draw it.
- Tap **NEW** to start an empty board.
- Tap the back arrow to save and return to the selection screen.

### Sokoban

Push every crate onto a target across all 155 levels of David W. Skinner's
Microban I collection. Crates can be pushed but not pulled, so plan ahead to
avoid trapping one against a wall. The board scales automatically to fit levels
up to 30 columns by 17 rows. A black crate marked with a white circle is already
sitting on a target.

Touch controls:

- Tap anywhere on the board in the direction you want the worker to move. The
  worker steps once or pushes one crate, making even the smallest scaled boards
  easy to control.
- Tap **RESTART** to restart only the current attempt after a deadlock. It never
  changes completed-level progress.
- After solving a level, **NEXT** replaces **RESTART** and opens the next
  unfinished level.
- Tap the back arrow to save and return to the selection screen.

Microban I is copyright David W. Skinner and is redistributed with permission
and credit. See the
[author's redistribution notice](http://www.abelmartin.com/rj/sokobanJS/Skinner/David%20W.%20Skinner%20-%20Sokoban.htm)
and the [source level file](https://github.com/OMerkel/Sokoban/blob/master/3rdParty/Levels/Microban.txt).
Each completed level is saved immediately to non-volatile memory. After a power
loss or firmware update, Sokoban starts at the next unfinished level. Completion
progress never moves backward and can be cleared only by erasing NVM.

### Peg Solitaire

Clear the classic cross-shaped board by jumping one peg over an adjacent peg
into an empty hole. The jumped peg is removed; the ideal finish is one peg in
the center.

Touch controls:

- Tap a peg to select it, then tap a legal destination two holes away.
- Tap **RESET** to restore the full board with its center hole empty.
- Tap the back arrow to save and return to the selection screen.

### Slitherlink

Draw one continuous loop around the numbered cells. Each number says how many
of that cell's four edges belong to the loop. Crosses can mark edges that are
known not to be part of the solution.

Touch controls:

- Tap an edge to cycle it from blank, to line, to crossed, then back to blank.
- Tap **NEW** for the next puzzle or **RESET** to clear the current puzzle.
- Tap the back arrow to save and return to the selection screen.

### Sudoku

Complete a randomly selected easy 9x9 Sudoku. Every row, column, and thick
three-by-three box must contain the digits 1 through 9 exactly once. All bundled
puzzles have one verified solution.

Touch controls:

- Tap an empty square, then tap a solid black **1**-**9** key.
- Tap **X** to clear the selected square. Conflicting digits are rejected.
- Tap **NEW** for another random puzzle or **RESET** to clear your entries.
- Tap the back arrow to save and return to the selection screen.

### Crossword

Solve one of 12 randomly selected easy 5x5 mini crosswords. The model and
renderer support crossword grids up to 9x9. The word database comes from
[cout/minicross](https://github.com/cout/minicross) under its bundled MIT
license; the short text clues are original to this project.

Touch controls:

- Tap a white square to select it and open the QWERTY keyboard overlay.
- Tap an intersecting square again to switch between its Across and Down clues.
- Tap letters to enter answers, **DEL** to erase, or **OK** to close the
  keyboard.
- Tap **NEW** for another random crossword or **RESET** to clear the grid.
- Tap the back arrow to save and return to the selection screen.

The selector uses two two-column pages with six games each. Sudoku and
Crossword occupy the bottom row of the second page, below Peg Solitaire and
Slitherlink. Previous and next arrows appear only when a page exists in that
direction.

Front buttons:

- On the selection screen, a short **OK** press does nothing.
- While playing, a short **OK** press saves the game and returns to the
  selection screen from any game.
- Hold **OK** for at least 1.2 seconds, then release before 5 seconds, to save
  and enter deep sleep. Press **OK** again to resume.
- With the temporary `ENABLE_SCREENSHOT_GESTURE` build flag enabled, hold
  **OK** continuously for 5 seconds to save the current screen as
  `/screenshot.bmp` on the SD card. A beep confirms the capture gesture.
- **UP** and **DOWN** are unused.

Every actionable on-screen button and every front-button press gives an
immediate confirmation beep. Ordinary board-cell taps remain silent.

The app beeps once at startup and when a game is opened from the selector.
While the app is open,
the ESP32-S3 enters light sleep between touch and button events, then wakes
immediately for input and logs each light-sleep entry and exit. A long **OK**
press uses deep sleep for longer idle periods. Resume state is stored in RTC
slow memory, so normal sleep/resume cycles do not write to flash or the SD
card. The same saved deep sleep starts automatically after five minutes without
touch or button input. Below 10% battery, the app saves its state, displays a
recharge screen, and enters deep sleep unless USB-C power is connected. The
normal sleep screen includes a QR code for this repository. All active screens
show the current battery percentage and the shared battery gauge used by the
other viewer apps; the gauge displays a charging bolt while USB-C power is
connected. On each game screen, the back arrow, game title, and battery status
share the top bar above a full-width divider.

The SSD1677 path uses 40 MHz window transfers and reseeds both differential RAM
planes after every refresh. This keeps normal moves near the panel's physical
waveform limit while preventing one move from affecting earlier regions.

## Build and flash

```powershell
# From the repository root
pio run -d games -e reterminal_e1005

# Or build, upload, and monitor from games/
..\deploy.bat e1005
```

This firmware is E1005-only. Other reTerminal E-series environments are not
defined intentionally.

## SD firmware updates

The app checks for `/update.bin` on an inserted SD card during startup. Use the
E1005 Games `-ota.bin` release asset; model validation, rollback protection,
and failure-safe file renaming use the same shared updater as the viewer apps.

## Tests

```powershell
Set-Location games
pio test -c platformio-test.ini -e native_test
```
