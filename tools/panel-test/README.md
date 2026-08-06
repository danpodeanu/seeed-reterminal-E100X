# panel-test

Full-screen e-paper and touch test, one build per reTerminal E-series model.

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

On E1001-E1004, the sketch draws once and enters deep sleep; any front
button redraws it. On E1005, it remains interactive: each GT911 touch is
logged, beeps, and temporarily inverts the complete pattern block containing
the contact using two partial refreshes. Press and release **OK** to sleep;
OK, UP, or DOWN wakes and redraws the test.
An inserted E1005 SD card can remain in place: the sketch powers and
deselects it so it cannot interfere with the display's shared SPI bus.
E1005 portrait output keeps the USB connector at the bottom.

## Per-panel adaptations

| Model  | Panel         | Palette           | Bars |
|--------|---------------|-------------------|------|
| E1001  | UC8179 800x480  | Gray4 (4 shades)   | 4 grey bars, high to low  |
| E1002  | ED2208 800x480  | Spectra E6 (6 col) | W/Y/G/B/R/K SMPTE bars    |
| E1003  | ED103TC2 1872x1404 | Gray16 (16 shades) | 16 grey bars + smooth ramp |
| E1004  | T133A01 1200x1600 | Spectra E6 (6 col) | W/Y/G/B/R/K SMPTE bars    |
| E1005  | SSD1677 800x480, rotated portrait | Monochrome | W/K bars + interactive GT911 inversion |

All patterns use the standard SMPTE layout:

- Top 2/3: full-height bars, one per palette entry, drawn at the native
  panel code so no dither can mask a dead colour.
- Middle 1/12: "castellations" strip - each bar reversed against black,
  which surfaces obvious refresh artefacts.
- Bottom 1/4: full-width white banner with model name and geometry
  (top 2/3), then a full-width shade ramp (bottom 1/3) separated by a
  1px black hairline. On Gray4/Gray16 panels every intermediate LUT
  entry is visible; on six-colour panels the ramp collapses to black
  and white, which still verifies the extreme bit-depth codes.

## Build & flash

```powershell
# From the repo root
pio run -d tools/panel-test -e reterminal_e1001 -t upload
pio device monitor -b 115200
```

Swap `reterminal_e1001` for `_e1002`, `_e1003`, `_e1004`, or `_e1005` to
match your board. All five environments compile and can be built together with
`pio run -d tools/panel-test`.

Serial output on UART1 (`GPIO43/44`, same as the viewer apps):

```
[panel-test] reTerminal E1003 - Gray16
[panel-test] 1872 x 1404, 16 palette entries
[panel-test] refreshing panel
[panel-test] done; sleeping - press any front button to redraw
```

E1005 additionally reports GT911 startup and touch coordinates:

```
[touch] GT911 ready at 0x5D, sensor=480x800
[touch] x=241 y=397 size=18 id=0
```

## Reference

- SMPTE ECR 1-1978 colour bars (the classic seven-bar TV test pattern).
- E-Ink Spectra E6 palette codes: 0x0 white, 0x2 green, 0x6 red,
  0xB yellow, 0xD blue, 0xF black (matches `photo-viewer/src/main.cpp`).
