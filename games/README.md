# Games

Touch-friendly, silent meeting games for the Seeed reTerminal E1005
("Seeed Sticky"). The app opens on a game-selection screen so additional games
can be added without installing separate firmware.

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
- Tap **NEW** for another field or **RESET** to replay the current field.
- Tap the back arrow to save and return to the selection screen.

Front buttons:

- On the selection screen, a short **OK** press does nothing.
- While playing, a short **OK** press saves the game and returns to the
  selection screen from any game.
- Hold **OK** for 1.2 seconds anywhere to save and enter deep sleep. The beep
  confirms that the long press has registered, so the button can be released.
  Press **OK** again to resume.
- **UP** and **DOWN** are unused.

The app beeps once at startup and once before sleeping. While the app is open,
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
