# XKCD e-paper display for reTerminal E1001–E1004

Turn a Seeed Studio reTerminal E-series device into a battery-powered XKCD
frame. The device periodically wakes, selects a random comic, downloads and
renders it using the panel's best available palette, then switches off Wi-Fi
and returns to deep sleep. The screen continues showing the comic without
power while the device sleeps.

The display includes the comic title and hover text together with local
temperature, humidity, and battery status. It is intended for a desk, shelf,
or wall where a new readable XKCD can appear automatically throughout the day.
Only Wi-Fi access to XKCD is required.

## Example output

![XKCD Viewer displaying XKCD 699, Trimester, on a reTerminal E1003](assets/e1003-xkcd-screenshot.png)

Frame captured directly from a reTerminal E1003 using the built-in screenshot
feature. Comic: [XKCD #699 — Trimester](https://xkcd.com/699/).

## Features

- One Arduino/PlatformIO source tree for reTerminal E1001, E1002, E1003, and
  E1004.
- Native Gray4, six-color, or Gray16 rendering according to the selected
  panel.
- Model-aware image filtering and aspect-ratio-preserving scaling, including
  enlargement of small comics on high-resolution panels.
- Optional SD cache that stores original images and metadata indefinitely.
- Cache-first timer wakes that avoid Wi-Fi between six-hour archive refills.
- Live operation without an SD card using PSRAM only.
- Temperature and humidity from the built-in SHT4x sensor, plus battery
  percentage and gauge.
- Deep sleep between refreshes, with wake-and-refresh buttons and an audible
  button acknowledgement.

## Supported models

Always build the environment matching the physical device.

| PlatformIO environment | Panel | Native output |
| --- | --- | --- |
| `reterminal_e1001` | 800×480 UC8179 | Gray4 (four grays) |
| `reterminal_e1002` | 800×480 ED2208 | six-color |
| `reterminal_e1003` | 1872×1404 ED103TC2 | Gray16 (16 grays) |
| `reterminal_e1004` | 1200×1600 T133A01 | six-color |

## Requirements

- A supported Seeed Studio reTerminal E-series device.
- A 2.4 GHz Wi-Fi network with internet access.
- PlatformIO Core or the PlatformIO IDE extension.
- A USB data cable for the initial upload.
- Optional: a FAT32 or exFAT microSD card for persistent caching.

## Configure

Create the local credentials header:

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h` and set `WIFI_SSID` and `WIFI_PASSWORD`. The real file
is excluded by `.gitignore`; only the placeholder example belongs in version
control.

The clock synchronizes on cold boot and then at most once daily. Time settings
are in `include/config.h`:

```cpp
constexpr char TIMEZONE[] = "GMT0BST,M3.5.0/1,M10.5.0";
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 4000;
```

The firmware requests NTP servers through DHCP option 42 before acquiring its
Wi-Fi lease. If DHCP supplies no server, or that server does not respond within
the configured DHCP timeout, it falls back to the two servers above.

This example uses London time: GMT in winter and BST from the last Sunday in
March until the last Sunday in October. Change the POSIX `TIMEZONE` rule when
deploying the device elsewhere.

The normal interval, archive refill, and overnight quiet period are configured
in the same file:

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

An SD card may be empty. The firmware creates `/xkcd` on first use. Without a
card, each comic is downloaded into PSRAM, displayed, and discarded without
writing to internal flash.

## Build and upload

Install PlatformIO Core using its official installation instructions, then
list the available serial ports:

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

Replace the serial port with the one reported on your computer. Monitor logs
with:

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Logging uses UART1 on GPIO43/GPIO44, matching the E-series carrier's
USB-to-UART bridge. Firmware binaries are written to
`.pio/build/<environment>/firmware.bin`.

If a build reports `ModuleNotFoundError: No module named 'intelhex'`, install
it into the Python environment running PlatformIO:

```bash
PIO_PYTHON="$(head -n 1 "$(command -v pio)" | sed 's/^#!//')"
"$PIO_PYTHON" -m pip install intelhex
```

## Operation

- A cold boot or reset displays the device Wi-Fi MAC address above
  `Connecting to <SSID>` before joining Wi-Fi. Button and automatic timer
  wakes skip this extra panel refresh to conserve battery power.
- Every startup logs a `[wake]` line with the local time and whether it was a
  cold boot/reset, scheduled timer, or front-button wake.
- Every refresh selects a random XKCD. The default sleep interval is 15
  minutes.
- With an SD card containing at least 10 complete comics, cold boots, timer
  wakes, and every front-button wake select exclusively from the local cache.
  They do not download the comic being displayed. Below 10 complete entries,
  wake-ups retain live selection so the cache can bootstrap.
- Every six hours, after a normal timer refresh has already updated the panel,
  the device checks for a newly published XKCD, caches it when available, and
  adds ten uncached historical comics. This scheduled maintenance is the only
  comic-download path once the 10-comic threshold has been reached. Cold boots
  report cache progress and start a new six-hour maintenance interval; they do
  not refill the archive. Maintenance has a five-minute total deadline; any
  unfinished downloads are deferred to the next maintenance window.
- During maintenance, any front-button press cancels outstanding work,
  removes an incomplete download, switches off Wi-Fi, and displays another
  cached comic. Network operations use short cancellation-aware timeouts, so
  acknowledgement may take up to a few seconds.
- Any front button wakes the device and immediately selects another comic. A
  short GPIO45 beep acknowledges a button wake.
- NTP runs on cold boot and at most once daily. DHCP-provided NTP is tried
  first, followed by the configured public servers. A failed NTP request is
  logged but does not prevent a comic refresh.
- From 01:00 until 07:00 by default, timer wakes return directly to sleep. The
  final scheduled refresh before 01:00 changes its title to
  `sleeping until 07:00`. A cold boot or any front-button wake overrides quiet
  hours, refreshes once, and then sleeps until 07:00.
- Hold the green button while the device is sleeping. Keep holding it through
  the first beep until a second beep confirms screenshot mode. When an SD card is
  mounted, the fully composed frame is written as an indexed BMP to
  `/screenshot.bmp`, replacing the previous screenshot. Remove the card and
  open that file on a computer to retrieve it.
- The cold-boot Wi-Fi screen reports the number of complete comics in the SD
  cache. Cached files remain available when XKCD or the network cannot be
  reached, and the cache is never pruned.
- If the SD card is full or a cache write fails, the firmware logs the failure
  and downloads the selected comic into PSRAM for that refresh without
  caching it. Existing cache entries are left intact.
- Without an SD card, the latest number and comic are downloaded on every
  refresh and retained only for the current wake cycle.
- Wi-Fi is switched off as soon as downloading and decoding finish, before
  dithering and the panel refresh. SD power is switched off before deep sleep,
  and the e-paper panel retains its image while asleep.

## Image selection and rendering

PNG, baseline JPEG, and supported BMP images can be displayed. GIFs,
progressive JPEGs, corrupt files, and images requiring reduction below 65% are
skipped. Results narrower than one quarter of the selected panel are also
skipped so extreme portrait comics remain readable. Up to eight random
candidates are tried before an error is shown.

Suitability is calculated from the selected model's native resolution and its
actual header/footer area. Small comics are enlarged to fill the available
content rectangle while preserving their aspect ratio. Large comics accepted
on E1003 or E1004 may still be skipped on the smaller E1001 or E1002 panels.

With an SD card, originals and metadata are stored as `/xkcd/<number>.<ext>`
and `/xkcd/<number>.json`. Without a card, compressed originals above 2 MiB
are skipped to preserve enough PSRAM for decoding and rendering.

## Configuration

Timing, download limits, retry counts, minimum display scale, and layout
dimensions are in `include/config.h`. `platformio.ini` supplies the model
number to `src/driver.h`, which selects Seeed_GFX setup 520, 521, 522, or 523.
Model-specific power-control pins are selected automatically.

HTTPS certificate verification is disabled because the firmware does not
carry a CA bundle. Wi-Fi credentials are compiled into the firmware; keep
`include/secrets.h` private and do not publish firmware binaries containing
real credentials.

## Development

Build every supported target before submitting a change:

```bash
pio run -e reterminal_e1001 -e reterminal_e1002 \
  -e reterminal_e1003 -e reterminal_e1004
```

Run the native unit tests:

```bash
pio test -c platformio-test.ini -e native_test
```

The tests exercise the production scheduling, cache policy, archive
eligibility, and deadline helpers. The GitHub Actions workflow runs them and
builds every device target using `include/secrets.h.example`, never local
credentials.

The PNG/JPEG/BMP decoder and dithering code originates from Seeed Studio's
official reTerminal SD-card examples. Exact upstream revision and attribution
are recorded in `lib/image_pipeline/UPSTREAM.md`.

## References

- [XKCD](https://xkcd.com/)
- [Seeed PlatformIO setup](https://wiki.seeedstudio.com/epaper_work_with_platformio/)
- [Seeed reTerminal E-series Arduino display guide](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/)
- [Seeed E-series peripherals and model-specific pins](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals/)

This is an unofficial project and is not affiliated with XKCD or Seeed Studio.
