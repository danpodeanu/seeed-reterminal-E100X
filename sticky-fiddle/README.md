# Sticky Fiddle

Sticky Fiddle is a deliberately pointless collection of nine tactile activities
for the reTerminal E1005 ("Seeed Sticky"). It is designed for idle hands during
meetings: no scores, failures, puzzles, or concentration required.

## Activities

| Activity | Interaction |
| --- | --- |
| Bubble Wrap | Tap the 6x8 sheet to pop individual bubbles. |
| Zen Rake | Drag through the sand to leave three parallel grooves. |
| Flip-Dot Board | Tap or drag across the 8x12 grid to flip dots. |
| Ripple Pond | Tap the pond to add concentric ripples that gradually fade. |
| Pointless Counter | Tap the large button to increment a persistent counter. |
| Kaleidoscope | Drag to draw four mirrored strokes at once. |
| Inkblot | Tap or drag to grow a symmetric inkblot. |
| Pebble Stack | Tap left or right to add pleasantly uneven pebbles. |
| Worry Stone | Rub the stone to deepen its central groove. |

Eight activities appear on the first picker page and one on the second. The
on-screen arrows and the UP/DOWN buttons page through the picker. Within an
activity, UP and DOWN move directly between activities. Each activity has a
reset or clear button.

## Shared controls and power behavior

- Tap an activity card to open it.
- Press OK for less than two seconds to close help or return to the picker.
  A short OK press on the picker does nothing.
- Hold OK for at least two seconds to enter deep sleep.
- Five minutes without input also enters deep sleep.
- Press OK to resume with the current activity and state restored from RTC
  memory.
- Battery status is sampled once per minute. Below 5%, the app saves state,
  shows the standard Sticky low-battery screen, and sleeps unless USB-C power
  is present.

The app uses the E1005 differential-refresh path for interaction and light
sleep between input events. USB framebuffer capture and SD-card firmware
updates through `/update.bin` are also available.

## Build

Install [PlatformIO](https://platformio.org/), then run:

```sh
pio run -d sticky-fiddle -e reterminal_e1005
```

Run the native activity-state tests with:

```sh
cd sticky-fiddle
pio test -c platformio-test.ini -e native_test
```

## Upload

Connect the E1005 over USB-C and run:

```sh
pio run -d sticky-fiddle -e reterminal_e1005 -t upload
```

Sticky Fiddle is E1005-only because it depends on the integrated touch screen,
480x800 portrait layout, battery gauge, and E1005 fast-refresh controller.
