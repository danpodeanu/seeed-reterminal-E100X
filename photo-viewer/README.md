# Photo Viewer for reTerminal E1001–E1004

A private, standalone SD-card photo frame for the Seeed Studio
reTerminal E-series. Photos fill the e-paper panel with no permanent
controls, captions, or status overlays — just the picture, at the
panel's full native resolution and color depth. Between refreshes the
device sleeps deeply and the image stays visible for free.

Photos live on the SD card and never leave the device. You can either
prepare them on a computer with a desktop conversion tool, or upload
them from a phone through the built-in Wi-Fi upload portal.

## What it does

- Rotates through photos on `/photos/` at a configurable cadence
  (default: one photo per hour).
- Renders each photo with the panel's full native palette (Gray4,
  Gray16, or six-color).
- Overnight quiet hours (default 01:00–07:00) so the panel doesn't
  refresh while you're asleep.
- Three front buttons: previous / next photo and a green button that
  opens the Wi-Fi upload portal.
- On-device Wi-Fi setup and photo upload from a phone through a
  captive-portal web page.
- **Portrait / landscape mode on E1004** (native 1200x1600 portrait).
  Pick *Native*, *RotateCW*, or *RotateCCW* in the portal; the browser
  crops and rotates uploads to match. Changes take effect on the very
  next upload - no reboot required.
- Low-battery *please recharge* screen instead of silently refusing to
  refresh, so you can see at a glance that the frame needs charging.
- **Over-the-air firmware updates from v1.5 onward** - drop the app-only
  `firmware-*-ota.bin` from the [Releases page](https://github.com/danpodeanu/seeed-reterminal-E100X/releases)
  onto the SD card as `/update.bin` and the device applies it on the
  next wake, or reconnect USB and re-run the
  [web flasher](https://danpodeanu.github.io/seeed-reterminal-E100X/)
  with the *Erase device* checkbox left unchecked to keep Wi-Fi
  credentials and portal settings. See the top-level
  [README](../README.md#updating-firmware-sd-card) for details.
- No cloud, no account, no telemetry. NTP once every six hours is the
  only outbound traffic during normal operation.

## Supported hardware

| Environment | Panel | Native output |
| --- | --- | --- |
| `reterminal_e1001` | 800×480 | 4-level gray |
| `reterminal_e1002` | 800×480 | six-color |
| `reterminal_e1003` | 1872×1404 | 16-level gray |
| `reterminal_e1004` | 1200×1600 | six-color |

> **E1002 status:** hardware-tested and supported from v1.7, including cold
> boot, photo rendering, and reopening the QR-code upload portal after a photo
> has been displayed.

Use the matching firmware target and photo-preparation model. Files
prepared for one model are intentionally rejected by another when
their dimensions do not match.

You will also need:

- A FAT32 or exFAT microSD card (photos live here).
- A 2.4 GHz Wi-Fi network with internet access (for NTP and, if you
  use it, the upload portal).
- A data-capable USB-C cable for the first flash.
- **Optional:** a CR1220 coin cell so the onboard PCF8563 hardware
  clock keeps time while the physical power switch is off.

## Getting started

### 1. Flash the firmware

The fastest path is the web flasher — no installer, no `esptool.py`:

> [Flash your reTerminal from the browser →](https://danpodeanu.github.io/seeed-reterminal-E100X/)

Pick the reTerminal model, select **Photo Viewer**, connect over
USB-C, and Chrome or Edge writes the latest release directly. To
build from source instead, see [Building from source](#building-from-source).

### 2. Connect the device to Wi-Fi

On first boot (and any time no Wi-Fi is configured) the device raises
its own captive-portal access point and shows QR codes on the panel:

- Scan the first QR from a phone to join the AP — the SSID is
  `ReTerminal xxxxxx` with a device-specific password (also printed
  on the panel).
- Scan the second QR to open the settings page in a browser. From
  there you can pick a Wi-Fi network, set a timezone, adjust quiet
  hours, and edit the photo interval. Values persist in NVS and
  survive reflashing.

Once Wi-Fi is set, the device stops advertising the AP and normal
photo playback begins.

You can re-enter the portal at any time: **while the device is
sleeping, press the green button**. The panel switches to the portal
welcome screen with three QR codes (Wi-Fi, portal URL, online help)
and the AP comes back up. Press either arrow to exit.

### 3. Put photos on the SD card

Photos live under `/photos/` on the SD card. You have three options:

- **Web upload from your phone** — no computer required. Enter the
  portal (green button), open the portal URL, choose a photo. The
  page previews it, applies the same crop-to-cover, gamma, and
  dithering pipeline used offline, and streams the resulting 4-bit
  BMP to `/photos/`. iPhone HEIC files are transparently converted
  to JPEG by iOS Safari during selection.
- **Prepared on a computer** — best quality, especially for large
  batches. See [Preparing photos on a computer](#preparing-photos-on-a-computer)
  below.
- **Quick drop-in** — copy small PNG or BMP files directly onto
  `/photos/`. The firmware resizes them at display time. Ordinary
  JPEG is intentionally not scanned this way; use the upload portal
  or the desktop tool for JPEG.

Filenames sorted alphabetically define the display order (unless
`PHOTO_ORDER_RANDOM` is enabled).

## Using the viewer

- **Every hour** (default) the panel refreshes to the next photo.
  Cadence is configurable in the portal.
- **Buttons on the front** all wake the device:
  - **Left arrow (GPIO4)** → previous photo.
  - **Right arrow (GPIO3)** → next photo.
  - **Green (GPIO5)** → enter the Wi-Fi upload portal. Any button
    exits the portal and returns to the photo.
  - A short beep confirms every button wake.
- **Header/overlay** — none. The photo fills the whole panel.
- **Quiet hours** (default 01:00–07:00) suppress automatic photo
  changes overnight. Buttons still work; the panel simply doesn't
  advance on its own. Unlike the information viewers, the photo
  itself is never overlaid with a sleep message.
- **When the SD card is missing or empty** the panel shows a clear
  error and how to recover. Unreadable and incompatible files are
  skipped up to `MAX_PHOTO_ATTEMPTS`; the next wake tries again.
- **Between refreshes** Wi-Fi is off, the shared peripheral rail is off, the battery
  measurement circuit is off, and the e-paper image stays visible for
  free.

## Preparing photos on a computer

For large batches, or the best quality on the Spectra 6 color panels,
prepare photos on a Mac, Linux, or Windows computer:

```bash
cd photo-viewer
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt

python tools/prepare_photos.py \
  --model e1003 \
  --output /Volumes/MY_SD_CARD/photos \
  ~/Pictures/Frame
```

The final argument can be one or more files or directories;
directories are searched recursively. Change both `--model` and the
destination path for your device.

The default `--fit cover` fills the entire display and center-crops
overflow. Use `--fit contain` to preserve every part of the photo
with white letterbox areas. Useful optional controls:

```bash
python tools/prepare_photos.py --help
python tools/prepare_photos.py \
  --model e1004 \
  --fit contain \
  --gamma 1.1 \
  --overwrite \
  --output /media/user/SD/photos \
  photo.jpg
```

Gamma values above 1 brighten midtones. Dithering is enabled by
default and normally gives the best photographic result;
`--no-dither` is available for flat artwork. E1001/E1003 conversion
uses perceptual luminance for natural monochrome skin brightness. On
the six-color E1002/E1004, warm-tone protection prevents green and
blue correction dots from appearing in skin, orange, and other warm
areas. It is enabled by default; `--no-warm-tone-protection` restores
unrestricted palette dithering when exact color mixing matters more.

## The Wi-Fi upload portal

Pressing the green button while the device is asleep opens a
one-shot Wi-Fi upload path, so a new photo can be prepared and
stored on the SD card without removing it from the frame:

1. Press green. The panel switches to the portal welcome screen with
   three QR codes: the Wi-Fi network to join, the portal URL, and a
   direct link to the online help page. The tagline reads *Press
   arrow to exit*.
2. The device brings up an open access point named
   `ReTerminal <last four MAC digits>` and a captive-portal web
   server on `http://192.168.4.1`. The SSID and URL are also printed
   on the panel.
3. Connect a phone, tablet, or laptop to the AP and open the portal
   URL (the captive-portal dialog usually opens on its own). The
   upload page accepts JPG and PNG only. iOS Safari transparently
   converts HEIC files to JPEG during selection, so photos taken on
   an iPhone work directly.
4. The page previews the image, applies the same crop-to-cover,
   gamma, and dithering pipeline used by `prepare_photos.py`, and
   streams the resulting 4-bit BMP straight to `/photos/` on the SD
   card. On E1004 the crop aspect follows the *Panel orientation*
   setting in the portal (Native / RotateCW / RotateCCW), so an
   E1004 mounted portrait gets a portrait crop and a wall-mounted
   landscape unit gets a landscape crop with no re-flashing. A
   success banner ("Uploaded. Next panel refresh will show
   this photo.") persists so the next upload can be prepared without
   page reload. On the Spectra 6 panels (E1002 and E1004) the browser
   runs [`epdoptimize`](https://github.com/paperlesspaper/epdoptimize)
   with a calibrated Spectra 6 palette and its auto-detect preset
   before sending the BMP, which produces noticeably better skin
   tones and gradients than the built-in dithering path. The library
   is bundled with the firmware; no network round-trip to a CDN is
   required.
5. Pressing either arrow button exits the portal, tears down the AP,
   and returns to the normal photo view. A *Reboot to viewer* button
   on every portal page (Wi-Fi, Settings, SD, Photos, Reset) does
   the same over the network so the exit can be triggered from the
   phone after the upload completes.

The portal only starts on the green button; the device never brings
up an AP during ordinary photo changes or timer wakes. The AP is
deliberately open — its scope is one file transfer to `/photos/` on
the local SD card, no credentials are ever transmitted, and Wi-Fi is
torn down before the device returns to deep sleep in either exit
path.

## Privacy and network use

Photos never leave the SD card. The only normal outbound traffic is
the periodic NTP synchronization (at most once every six hours);
there is no photo service, cloud account, telemetry, or background
upload.

## Troubleshooting

- **Panel says "No SD card"** — the card is not inserted or is not
  FAT32/exFAT. Reformat and re-insert.
- **Panel says "No photos in /photos"** — the directory is empty or
  files are in an unsupported format. Copy in a prepared BMP or a
  small PNG.
- **Panel is stuck on "Connecting to <SSID>"** — the stored Wi-Fi
  credentials probably don't match your network. Re-enter the portal
  (press green while sleeping) and update them.
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
cd photo-viewer
cp include/secrets.h.example include/secrets.h
```

The real `secrets.h` is covered by `.gitignore` so it never lands in
version control. Editing it is optional and covered in
[Settings reference](#settings-reference) below.

Build and upload for the exact model, for example:

```bash
pio run -e reterminal_e1003
pio run -e reterminal_e1003 \
  --target upload \
  --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Use `/dev/cu.usbserial-*` on macOS when that is the device's serial
port. Every application log line starts with local time in
`[YYYY-MM-DD HH:MM:SS.mmm]` format. Before the clock is
synchronized, the same format intentionally shows a 1970 date.

Run the native unit tests:

```bash
pio test -c platformio-test.ini -e native_test
```

The tests cover quiet-hour timing, NTP scheduling, button direction,
and photo-index wrapping. GitHub Actions runs them alongside all
four firmware builds.

### Settings reference

Every runtime setting is editable from the on-device portal and
persists in NVS across reflashes, so **no compile-time configuration
is required** to build and run the firmware — a stock `pio run` will
launch the portal on first boot and let you configure Wi-Fi and
slideshow preferences from the browser.

Configuring compile-time defaults is optional but useful when you
want to flash many devices without having to run the portal on each
one (automated provisioning), or when you want the firmware to come
up already knowing the Wi-Fi network:

- **`include/secrets.h`** — Wi-Fi credentials. Required at build time
  (the copy step in [Building from source](#building-from-source)
  creates it), but editing is optional. Fill in `WIFI_SSID` /
  `WIFI_PASSWORD` to seed defaults:

  ```cpp
  #define WIFI_SSID "YOUR_WIFI_NAME"
  #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
  ```

  Leaving the placeholders untouched boots straight into the portal.

- **`include/config.h`** — user-facing behavior defaults (photo
  interval, timezone, NTP servers, quiet hours). Every entry is
  overridable from the portal at runtime; edit the header only if
  you want to change what a fresh device comes up with.

- **`include/system_config.h`** — implementation-level knobs
  (hardware timing, sensor debounce, dither internals). Included
  from the bottom of `config.h`; you should not usually need to
  touch these.

Reference of the most common `config.h` fields:

- `SLEEP_SECONDS`: normal automatic photo interval.
- `TIMEZONE`: POSIX timezone used for quiet hours and logs. Its
  offset sign is reversed; London can use
  `GMT0BST,M3.5.0/1,M10.5.0`, while Suzhou uses `CST-8`.
- `NTP_SERVER_PRIMARY`, `NTP_SERVER_SECONDARY`: fall-back time
  servers used when DHCP does not advertise one.
- `QUIET_HOURS_ENABLED`, `QUIET_START_*`, and `QUIET_END_*`:
  overnight suppression period.
- `PHOTO_DIR`: SD-card directory scanned for photos.
- `PHOTO_ORDER_RANDOM`: `true` shuffles the photo enumeration at
  each boot so successive frames feel random; `false` sorts
  alphabetically for a deterministic order across boots.
- `orientation` (E1004 only, portal-only, no compile-time default):
  `Native`, `RotateCW`, or `RotateCCW`. Controls the crop aspect the
  browser uploader offers so photos taken in portrait or landscape
  fill the physical panel correctly. E1001-E1003 are landscape-native
  and ignore this setting. Changing it in the portal takes effect
  immediately - the `/upload-photo` page's crop rectangle re-orients
  itself on the next tab focus without a reboot.

### Buttons and pins

All three front buttons are deep-sleep wake sources:

- GPIO4 (left arrow) selects the previous photo.
- GPIO3 (right arrow) selects the next photo.
- GPIO5 (green) enters the SD Wi-Fi upload portal.

On E1001–E1003 the green/right button is GPIO3. E1004 uses the three
physical front buttons; no touch input is required.

### Wake, timing, and RTC

- A cold boot/reset displays the device MAC address, Wi-Fi SSID,
  SD-card photo count, battery level, and onboard
  temperature/humidity while connecting.
- The device obtains its address and optional NTP servers through
  DHCP.
- It synchronizes the clock on cold boot and at most once every six
  hours. DHCP-provided NTP is tried first, followed by
  `pool.ntp.org` and `time.cloudflare.com`.
- Successful NTP synchronization stores UTC in the onboard PCF8563.
  On a later deep-sleep wake, a failed NTP synchronization falls
  back to that clock only when its voltage-low (`VL`) flag is clear.
  An invalid or rolled-back ESP clock is recovered from the PCF8563
  before NTP and quiet-hour decisions. Cold boots log its stored UTC
  and `VL` state. A CR1220 is needed to retain it across physical
  power-off.
- Wi-Fi is disabled immediately after time synchronization. Ordinary
  photo changes do not start the radio.
- Every startup logs a `[wake]` line with the local time and whether
  it was a cold boot/reset, scheduled timer, or front-button wake.
- The SD card and battery measurement circuit are powered down
  before deep sleep.

ESP32 RTC memory keeps the current photo position and last NTP-sync
time across deep sleep. A complete power loss starts again from the
first directory entry; this is separate from the PCF8563 hardware
clock described above.

### File formats accepted from `/photos/`

Prepared files are exact-size, 4-bit BMPs. The firmware also has a
convenience fallback for ordinary PNG and BMP files dropped straight
into `/photos/`, so you can copy a browser-sized PNG onto the SD card
without running `prepare_photos.py` first. Ordinary JPEG is
intentionally not scanned from `/photos/` — 12 MP phone photos
routinely OOM the RGB decode buffer. Use the browser uploader at
`/upload-photo` for JPEG (it transcodes to the prepared BMP layout),
and keep drop-in PNGs sensibly sized (a few hundred pixels a side).
On-device resizing is lower quality and memory-intensive —
especially on the E1003 and E1004.

### Acknowledgements

The browser-side dithering on the Spectra 6 panels (E1002 and E1004)
is powered by [**epdoptimize**](https://github.com/paperlesspaper/epdoptimize)
by [paperlesspaper](https://github.com/paperlesspaper) — a
JavaScript library for reducing image colors and dithering them for
color e-ink displays with optimal visual quality. Version 1.3.0 is
bundled verbatim in `common-sd-web/src/epdoptimize_js.cpp`
(regenerated by `tools/embed_epdoptimize.py`) and served from
`/epdoptimize.mjs`. The library is distributed under the Apache-2.0
license; the full text is mirrored at
[`common-sd-web/third_party/epdoptimize/LICENSE`](../common-sd-web/third_party/epdoptimize/LICENSE).
