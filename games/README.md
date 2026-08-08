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

- Tap **LIGHTS OUT** on the game-selection screen to play.
- Tap a board square to make a move.
- Tap **NEW** for another puzzle or **RESET** to restore the current puzzle.
- Tap **GAMES** to return to the selection screen.

Front buttons:

- **OK** saves the current screen and game, shows the sleep screen, and enters
  deep sleep. Press **OK** again to resume exactly where you left off.
- **UP** starts a new Lights Out puzzle.
- **DOWN** returns to the game-selection screen.

The app beeps once at startup and once before sleeping. While the app is open,
the ESP32-S3 enters light sleep between touch and button events, then wakes
immediately for input. Explicit **OK** sleep uses deep sleep for longer idle
periods. Resume state is stored in RTC slow memory, so normal sleep/resume
cycles do not write to flash or the SD card.

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
