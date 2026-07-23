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

- A cold boot displays the device Wi-Fi MAC address above
  `Connecting to <SSID>` before joining Wi-Fi.
- Every refresh selects a random XKCD. The default sleep interval is 15
  minutes.
- The green GPIO3 button and right GPIO4 button wake the device and request a
  new comic. A short GPIO45 beep acknowledges a button wake.
- Hold the green button while the device is sleeping. Keep holding it through
  the first beep until a second beep confirms screenshot mode. When an SD card is
  mounted, the fully composed frame is written as an indexed BMP to
  `/screenshot.bmp`, replacing the previous screenshot. Remove the card and
  open that file on a computer to retrieve it.
- With an SD card, cached files are preferred and remain available when XKCD
  or the network cannot be reached. The latest comic number is checked at most
  once every six hours, and the cache is never pruned.
- If the SD card is full or a cache write fails, the firmware logs the failure
  and downloads the selected comic into PSRAM for that refresh without
  caching it. Existing cache entries are left intact.
- Without an SD card, the latest number and comic are downloaded on every
  refresh and retained only for the current wake cycle.
- Wi-Fi and SD power are switched off before deep sleep. The e-paper panel
  retains its image while asleep.

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

The GitHub Actions workflow performs the same checks using
`include/secrets.h.example`, never local credentials.

The PNG/JPEG/BMP decoder and dithering code originates from Seeed Studio's
official reTerminal SD-card examples. Exact upstream revision and attribution
are recorded in `lib/image_pipeline/UPSTREAM.md`.

## References

- [XKCD](https://xkcd.com/)
- [Seeed PlatformIO setup](https://wiki.seeedstudio.com/epaper_work_with_platformio/)
- [Seeed reTerminal E-series Arduino display guide](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/)
- [Seeed E-series peripherals and model-specific pins](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals/)

This is an unofficial project and is not affiliated with XKCD or Seeed Studio.
