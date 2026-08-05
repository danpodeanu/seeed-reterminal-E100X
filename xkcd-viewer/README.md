# XKCD Viewer for reTerminal E1001–E1004

A battery-powered, always-on XKCD comic frame. The device wakes on a
schedule, picks a random XKCD, renders it on the e-paper panel, then
switches Wi-Fi off and returns to deep sleep. The image stays visible
without power between refreshes, and can run for weeks on a charge.

Header shows the comic title and hover text alongside indoor
temperature/humidity from the built-in SHT4x sensor, battery
percentage, and (when a microSD card is inserted) the size of the
locally cached comic archive.

![XKCD Viewer displaying XKCD 2346, COVID Risk Comfort Zone, on a reTerminal E1003](assets/e1003-xkcd-screenshot.png)

*Frame captured directly from a reTerminal E1003 using the built-in
screenshot feature.*

## What it does

- Picks a random XKCD every 15 minutes (default) and shows it on the
  panel.
- Optional SD-card archive that stores comics indefinitely, so most
  refreshes are entirely offline once the cache is populated.
- Header shows comic title, hover text, indoor temperature/humidity,
  battery, and a USB-power indicator.
- Overnight quiet hours (default 01:00–07:00) so the device doesn't
  refresh while you're asleep.
- Any front button wakes the device and jumps to a new comic.
- On-device Wi-Fi setup via a captive portal with QR codes — no
  computer needed after the initial flash.
- **SD-card firmware updates** - drop the app-only
  `firmware-*-ota.bin` from the [Releases page](https://github.com/danpodeanu/seeed-reterminal-E100X/releases)
  onto the SD card as `/update.bin` and the device applies it on the
  next wake, or reconnect USB and re-run the
  [web flasher](https://danpodeanu.github.io/seeed-reterminal-E100X/)
  with the *Erase device* checkbox left unchecked to keep Wi-Fi
  credentials, portal config, and the offline archive. See the top-level
  [README](../README.md#updating-firmware-sd-card) for details.
- Unicode-safe titles and alt text.

## Supported hardware

| Environment | Panel | Native output |
| --- | --- | --- |
| `reterminal_e1001` | 800×480 UC8179 | 4-level gray |
| `reterminal_e1002` | 800×480 ED2208 | six-color |
| `reterminal_e1003` | 1872×1404 ED103TC2 | 16-level gray |
| `reterminal_e1004` | 1200×1600 T133A01 | six-color |

Use the firmware environment matching your device; panel drivers and
dimensions differ between models.

You will also need:

- A 2.4 GHz Wi-Fi network with internet access.
- A data-capable USB-C cable for the first flash.
- **Optional but recommended:** a FAT32 or exFAT microSD card for the
  offline archive, Unicode fonts, and screenshot capture.
- **Optional:** a CR1220 coin cell so the onboard PCF8563 hardware
  clock keeps time while the physical power switch is off.

## Getting started

### 1. Flash the firmware

The fastest path is the web flasher — no installer, no `esptool.py`:

> [Flash your reTerminal from the browser →](https://danpodeanu.github.io/seeed-reterminal-E100X/)

Pick the reTerminal model, select **XKCD Viewer**, connect over USB-C,
and Chrome or Edge writes the latest release directly. To build from
source instead, see [Building from source](#building-from-source).

### 2. Connect to Wi-Fi

On first boot (and any time no Wi-Fi is configured) the device raises
its own captive-portal access point and shows two QR codes on the
panel:

- Scan the first QR from a phone to join the AP — the SSID is
  `ReTerminal xxxxxx` with a device-specific password (also printed on
  the panel).
- Scan the second QR to open the settings page in a browser.

The settings page lets you pick a Wi-Fi network, set a timezone, edit
quiet hours, choose the refresh cadence, and more. Values persist in
NVS and survive reflashing.

You can re-enter the portal at any time: **while the device is
sleeping, hold the green button for 1–5 seconds**. Wait for the first
beep and keep holding — when the panel switches to the QR-code portal
you're in.

### 3. Optional: seed an SD card

XKCD Viewer works fine without a card, but a card unlocks the parts
that make the display genuinely feel offline:

- Comics are cached under `/xkcd/`. Once ≥10 comics are stored,
  every timer wake picks from the cache without touching Wi-Fi.
- Unicode fonts (`/fonts/sans_bold_*.vlw`) render exotic characters
  in titles and alt strings correctly.
- Screenshots are written to `/screenshot.bmp`.

You can either let the device build the cache slowly over time (it
grabs the newest comic + up to ten old ones every six hours), or seed
the card in one shot from a computer:

```bash
python3 tools/preload_sd.py /Volumes/XKCD --with-fonts
```

The details are in [Pre-populating an SD card](#pre-populating-an-sd-card).

## Using the viewer

- **Every 15 minutes** (default) the device wakes, picks a new comic,
  refreshes the panel, and returns to sleep. Cadence is configurable in
  the portal.
- **Buttons on the front** all wake the device:
  - Any front button → jump to a new random comic.
  - **Green button + 1–5 s hold from sleep** → open the Wi-Fi
    configuration portal (QR codes on the panel).
  - **Green button + longer hold (>5 s) from sleep** → save a
    screenshot of the current frame to `/screenshot.bmp`.
  - A short GPIO45 beep confirms every button wake.
- **Header readouts** update on every refresh:
  - Indoor temperature and humidity (SHT4x).
  - Battery percentage plus a lightning bolt when USB power is
    connected (SY6974B-equipped boards only; older revisions with
    ETA6003 silently omit the icon).
  - With an SD card: two stacked archive counts show complete cached
    comics over total published comics.
- **Quiet hours** (default 01:00–07:00) suppress automatic refreshes
  overnight. Cold boots and button wakes still refresh; the last
  scheduled frame before quiet hours re-labels itself
  `sleeping until 07:00`.
- **Cache maintenance** runs every six hours: the device catches up on
  the newest comic and adds up to ten unseen historical comics. Any
  button press cancels the maintenance work and shows another comic
  immediately.
- **Between refreshes** Wi-Fi is off, the shared peripheral rail is off, and the e-paper
  image stays visible for free.

## Troubleshooting

- **Panel is stuck on "Connecting to <SSID>"** — the stored Wi-Fi
  credentials probably don't match your network. Re-enter the portal
  (hold green 1–5 s while sleeping) and update them.
- **Titles or alt text show empty boxes or dropped characters** —
  the DejaVu Sans smooth fonts are not on the SD card. Regenerate them
  with `python3 tools/preload_sd.py /Volumes/XKCD --with-fonts`.
- **Same comic keeps reappearing** — the SD cache has fewer than 10
  entries and Wi-Fi is unreachable. Let it refill over the next few
  wake cycles, or preload the card.
- **Log timestamps show 1970-01-01** — the clock hasn't synced yet;
  NTP runs on cold boot and at most once every six hours after that.

---

## Technical details

Everything below is for developers building, extending, or debugging
the firmware.

### Building from source

Install [PlatformIO Core](https://platformio.org/install/cli), then
create the credentials header from the tracked template — the
firmware includes it directly, so this step is required even if you
leave the placeholders alone:

```bash
cd xkcd-viewer
cp include/secrets.h.example include/secrets.h
```

The real `secrets.h` is covered by `.gitignore` so it never lands in
version control. Editing it is optional and covered in
[Compile-time configuration](#compile-time-configuration) below.

List the available serial ports:

```bash
pio device list
```

For E1001 on macOS:

```bash
pio run -e reterminal_e1001
pio run -e reterminal_e1001 --target upload --upload-port /dev/cu.usbserial-11410
```

For E1003 on Linux:

```bash
pio run -e reterminal_e1003
pio run -e reterminal_e1003 --target upload --upload-port /dev/ttyUSB0
```

Replace the serial port with the one reported on your computer. Monitor
logs with:

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Logging uses UART1 on GPIO43/GPIO44, matching the E-series carrier's
USB-to-UART bridge. Firmware binaries are written to
`.pio/build/<environment>/firmware.bin`. Every application log line
starts with local time in `[YYYY-MM-DD HH:MM:SS.mmm]` format. Before
the clock is synchronized, the same format intentionally shows a 1970
date.

If a build reports `ModuleNotFoundError: No module named 'intelhex'`,
install it into the Python environment running PlatformIO:

```bash
PIO_PYTHON="$(head -n 1 "$(command -v pio)" | sed 's/^#!//')"
"$PIO_PYTHON" -m pip install intelhex
```

### Compile-time configuration

Every runtime setting is editable from the on-device portal and
persists in NVS across reflashes, so **no compile-time configuration
is required** to build and run the firmware — a stock `pio run` will
launch the portal on first boot and let you configure Wi-Fi and
comic preferences from the browser.

Configuring compile-time defaults is optional but useful when you
want to flash many devices without having to run the portal on each
one (automated provisioning), or when you want the firmware to come
up already knowing the Wi-Fi network:

- **`include/secrets.h`** — Wi-Fi credentials. Required at build time
  (the copy step in [Building from source](#building-from-source)
  creates it), but editing is optional. If you fill in `WIFI_SSID` /
  `WIFI_PASSWORD` those act as defaults when NVS has no stored SSID
  (typical for a fresh chip); leaving the placeholders untouched
  boots straight into the portal.

- **`include/config.h`** — user-facing behavior defaults (sleep
  interval, timezone, NTP servers, quiet hours, comic pool). Every
  entry is overridable from the portal at runtime; edit the header
  only if you want to change what a fresh device comes up with.

- **`include/system_config.h`** — implementation-level knobs (panel
  geometry, timing budgets, PSRAM/image caps, cache paths, layout
  dimensions). Included from the bottom of `config.h`; you should
  not usually need to touch these.

Reference of the most common `config.h` fields:

- `SLEEP_SECONDS`: interval between automatic refreshes.
- `TIMEZONE`: POSIX timezone used for quiet hours and logs. Its offset
  sign is reversed; London can use `GMT0BST,M3.5.0/1,M10.5.0`, while
  Suzhou uses `CST-8`.
- `NTP_SERVER_PRIMARY`, `NTP_SERVER_SECONDARY`: fall-back time servers
  used when DHCP does not advertise one.
- `QUIET_HOURS_ENABLED`, `QUIET_START_*`, and `QUIET_END_*`: overnight
  suppression period. Cold boots and any front-button wake still
  refresh immediately.
- `MIN_DISPLAY_SCALE`: minimum fraction of the source pixels the panel
  will accept before a comic is considered illegible and skipped. Raise
  toward `1.0` to be pickier; lower toward `0.5` to display more
  comics at the cost of readability.
- `DATE_LOCALE`: order for the publication date shown at the bottom-
  right of the image area. `DateLocale::DMY` (default) renders
  `14-03-2025`, `DateLocale::MDY` renders `03-14-2025`, and
  `DateLocale::YMD` renders `2025-03-14`. Day and month are always
  zero-padded and the separator is always `-`.
- `DEBUG_FORCE_COMIC`: when non-zero, the next cold-boot pick short-
  circuits random selection and loads that specific comic straight
  from the local cache. Intended only for reproducing a render bug on
  a known-bad comic; leave at `0` for normal operation.

`platformio.ini` supplies the model number to `../common/include/driver.h`,
which selects Seeed_GFX setup 520, 521, 522, or 523. Model-specific
power-control pins are selected automatically.

HTTPS certificate verification is disabled because the firmware does
not carry a CA bundle. Any Wi-Fi credentials you add to
`include/secrets.h` are compiled into the binary — keep the file
private (it is `.gitignore`d) and do not publish firmware binaries
built from a customised copy.

### Time and NTP

Default time settings in `include/config.h`:

```cpp
constexpr char TIMEZONE[] = "GMT0BST,M3.5.0/1,M10.5.0";
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 6000;
```

The firmware requests NTP servers through DHCP option 42 before
acquiring its Wi-Fi lease. If DHCP supplies no server, or that server
does not respond within the configured DHCP timeout, it falls back to
the two servers above. The six-second default includes up to five
seconds of randomized SNTP startup delay in Arduino-ESP32; it is not
solely a server-response timeout.

After a successful NTP synchronization, the firmware stores UTC in the
onboard PCF8563 hardware RTC. If a later deep-sleep wake cannot
synchronize with NTP, the PCF8563 restores the ESP32 clock only when
its voltage-low (`VL`) flag is clear. Cold boots log the stored UTC
value and `VL` state. A CR1220 coin cell is required for the PCF8563
to retain reliable time while the physical power switch is off; the
main battery powers it while the device remains on or in deep sleep.

This example uses London time: GMT in winter and BST from the last
Sunday in March until the last Sunday in October. Change the POSIX
`TIMEZONE` rule when deploying the device elsewhere.

The archive refresh cadence and quiet-hour period are configured in
the same file:

```cpp
constexpr uint64_t SLEEP_SECONDS = 15ULL * 60ULL;
constexpr uint32_t ARCHIVE_REFRESH_SECONDS = 6UL * 60UL * 60UL;
constexpr uint8_t ARCHIVE_OLD_COMICS_PER_REFRESH = 10;
constexpr bool QUIET_HOURS_ENABLED = true;
constexpr uint8_t QUIET_START_HOUR = 1;
constexpr uint8_t QUIET_START_MINUTE = 0;
constexpr uint8_t QUIET_END_HOUR = 7;
constexpr uint8_t QUIET_END_MINUTE = 0;
```

### Image selection and rendering

PNG, baseline JPEG, and supported BMP images can be displayed. GIFs,
progressive JPEGs, corrupt files, and images requiring reduction below
65% are skipped. Results narrower than one quarter of the selected
panel are also skipped so extreme portrait comics remain readable. Up
to eight random candidates are tried before an error is shown.

Suitability is calculated from the selected model's native resolution
and its actual header/footer area. Small comics are enlarged to fill
the available content rectangle while preserving their aspect ratio.
Large comics accepted on E1003 or E1004 may still be skipped on the
smaller E1001 or E1002 panels.

With an SD card, image originals are stored as
`/xkcd/<number>.<ext>` and all per-comic metadata (title, alt text,
extension, image URL) lives in a single manifest at
`/xkcd/index.jsonl`. The firmware loads the manifest once on wake,
parsing one tiny JSON object per line so the parse tree never exceeds
a few hundred bytes, and never opens a per-comic metadata file during
picking or rendering — which keeps the cached-selection path to a
single SD open per comic on FAT32 volumes with thousands of siblings.
Scheduled cache maintenance verifies image files still exist and
drops stale entries. Without a card, compressed originals above 2 MiB
are skipped to preserve enough PSRAM for decoding and rendering.

The firmware cannot reconstruct the manifest on its own — it needs
the title, alt text, and source URL that only xkcd's info API
returns. Re-run `tools/preload_sd.py` to rebuild it if it goes
missing.

### Pre-populating an SD card

`tools/preload_sd.py` can download the complete historical archive
directly to a mounted SD card. It uses only the Python standard
library and can be rerun safely: valid existing entries are skipped
and incomplete entries are retried.

On macOS:

```bash
python3 tools/preload_sd.py /Volumes/XKCD
```

On Linux:

```bash
python3 tools/preload_sd.py /media/$USER/XKCD
```

The argument is the SD-card root; the script creates its `xkcd`
directory. XKCD #404 is intentionally absent and is skipped. Four
downloads run in parallel by default; use `--workers 1` for a slower,
strictly sequential download. Run with `--help` for range, retry,
timeout, and force-download options. On first run the script also
migrates older cache layouts (per-comic `<n>.json`, `latest.json`,
pre-JSON `index.txt`) into the single manifest and deletes the legacy
files. Safely eject the card after the script reports completion.

### Unicode titles and alt text (smooth fonts)

Some xkcd titles and alt strings contain non-ASCII characters (e.g.
#1647 "forté", #2071 uses em dashes and curly quotes). To render those
correctly, the firmware loads TFT_eSPI `.vlw` smooth fonts from the SD
card at `/fonts/sans_bold_<size>.vlw` (every integer size from 16 to
40 px; the generator writes 12–48 by default so future firmware
tweaks don't require regenerating the card). The pixel sizes are
selected per model so the cap-height matches the previous GFX
FreeFont sizes.

Generate the fonts with the preloader:

```bash
python3 tools/preload_sd.py /Volumes/XKCD --with-fonts
```

`--with-fonts` bakes the `.vlw` files from
`../tools/fonts/DejaVuSans-Bold.ttf` (~110 MB total for all 37 sizes,
~5,900 glyphs each); `--fonts-ttf <path>` overrides the source. The
generator is also available as a standalone tool at the repo root:

```bash
python3 ../tools/fonts/make_vlw.py /Volumes/XKCD/fonts
```

By default the generator uses `../tools/fonts/DejaVuSans-Bold.ttf`
and writes every integer size from 12 to 48 px. Pass `--ttf <path>`
or `--size <n>` (repeatable) to override.

Both paths require Pillow and fontTools:

```bash
pip install pillow fonttools
```

**Prebuilt fonts (no Python needed).** The repository ships the
prebuilt `.vlw` set under [`fonts/`](../fonts/), and every tagged
release attaches a `sans_bold_fonts.zip` bundle. Grab either source
and copy the files into `/fonts/` on the SD card:

```bash
# From a clone:
cp -r fonts/*.vlw /Volumes/XKCD/fonts/
# Or from a release:
#   1. Download sans_bold_fonts.zip from
#      https://github.com/danpodeanu/seeed-reterminal-E100X/releases
#   2. Unzip it into /Volumes/XKCD/fonts/
```

DejaVu Sans is licensed under the Bitstream Vera Fonts License; see
`../tools/fonts/LICENSE.dejavu`.

**Fallback.** If `/fonts/sans_bold_<size>.vlw` is missing on the card,
the firmware falls back to the built-in GFX FreeFonts for that size —
text still renders, but any non-ASCII codepoints in the title or alt
strings are dropped.

**4-byte UTF-8.** Codepoints above U+FFFF (e.g. the math-italic
script letters in #2912) are dropped from title and alt rendering.
TFT_eSPI's UTF-8 decoder handles only 1-3 byte sequences, and DejaVu
Sans Bold covers only a small fraction of that block anyway.

### Development

Build every supported target before submitting a change:

```bash
pio run -e reterminal_e1001 -e reterminal_e1002 \
  -e reterminal_e1003 -e reterminal_e1004
```

Run the native unit tests:

```bash
pio test -c platformio-test.ini -e native_test
python3 -m unittest discover -s tools/tests
```

The tests exercise the production scheduling, cache policy, archive
eligibility, deadline helpers, and SD preloader cache compatibility.
The GitHub Actions workflow runs both test suites and builds every
device target using `include/secrets.h.example`, never local
credentials.

The PNG/JPEG/BMP decoder and dithering code originates from Seeed
Studio's official reTerminal SD-card examples. Exact upstream
revision and attribution are recorded in
`lib/image_pipeline/UPSTREAM.md`.

### References

- [XKCD](https://xkcd.com/)
- [Seeed PlatformIO setup](https://wiki.seeedstudio.com/epaper_work_with_platformio/)
- [Seeed reTerminal E-series Arduino display guide](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/)
- [Seeed E-series peripherals and model-specific pins](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals/)

This is an unofficial project and is not affiliated with XKCD or Seeed
Studio.
