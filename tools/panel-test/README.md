# panel-test

Full-screen e-paper test pattern, one build per reTerminal E100X panel model.

## Purpose

Drop-in diagnostic: after flashing, the panel refreshes once with a
SMPTE-inspired colour-bar pattern adapted to that panel's palette. Useful
for:

- Confirming a brand-new device drives every colour lane / grey level.
- Spotting dead pixel rows after a rough shipment.
- Sanity-checking a driver upgrade of `Seeed_GFX` against a known
  reference image.
- Handing to someone else so they can flash a known-good sketch and take
  a photo of the panel for support.

The sketch draws once in `setup()` and then enters deep sleep, so the
pattern stays on the display indefinitely. Press any of the three front
buttons (GPIO 3/4/5) to redraw it - they're wired as EXT1 wake sources.
Unplugging/replugging USB also redraws.

## Per-panel adaptations

| Model  | Panel         | Palette           | Bars |
|--------|---------------|-------------------|------|
| E1001  | UC8179 800x480  | Gray4 (4 shades)   | 4 grey bars, high to low  |
| E1002  | ED2208 800x480  | Spectra E6 (6 col) | W/Y/G/B/R/K SMPTE bars    |
| E1003  | ED103TC2 1872x1404 | Gray16 (16 shades) | 16 grey bars + smooth ramp |
| E1004  | T133A01 1200x1600 | Spectra E6 (6 col) | W/Y/G/B/R/K SMPTE bars    |

All patterns use the standard SMPTE layout:

- Top 2/3: full-height bars, one per palette entry, drawn at the native
  panel code so no dither can mask a dead colour.
- Middle 1/12: "castellations" strip - each bar reversed against black,
  which surfaces obvious refresh artefacts.
- Bottom 1/4: model banner, solid black patch, solid white patch, and a
  grey ramp along the bottom edge (grayscale panels only).

## Build & flash

```powershell
# From the repo root
pio run -d tools/panel-test -e reterminal_e1001 -t upload
pio device monitor -b 115200
```

Swap `reterminal_e1001` for `_e1002`, `_e1003`, or `_e1004` to match your
board. All four environments compile and can be built together with
`pio run -d tools/panel-test`.

Serial output on UART1 (`GPIO43/44`, same as the viewer apps):

```
[panel-test] reTerminal E1003 - Gray16
[panel-test] 1872 x 1404, 16 palette entries
[panel-test] refreshing panel
[panel-test] done; entering deep sleep - press reset to redraw
```

## Reference

- SMPTE ECR 1-1978 colour bars (the classic seven-bar TV test pattern).
- E-Ink Spectra E6 palette codes: 0x0 white, 0x2 green, 0x6 red,
  0xB yellow, 0xD blue, 0xF black (matches `photo-viewer/src/main.cpp`).
