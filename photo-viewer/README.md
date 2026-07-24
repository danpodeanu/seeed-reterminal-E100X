# Photo Viewer

A battery-conscious, standalone SD-card photo frame for the Seeed Studio
reTerminal E1001, E1002, E1003, and E1004. Photos fill the e-paper panel with
no permanent controls, captions, or status overlays.

The included desktop preparation tool resizes each source once and converts it
to the target panel's native resolution and full color space. It applies
Floyd-Steinberg error-diffusion dithering and writes a compact 4-bit BMP that
the device can send directly to the panel.

## Supported displays

| Target | Resolution | Output |
| --- | ---: | --- |
| `reterminal_e1001` / `e1001` | 800 × 480 | 4-level grayscale |
| `reterminal_e1002` / `e1002` | 800 × 480 | Six-color e-paper palette |
| `reterminal_e1003` / `e1003` | 1872 × 1404 | 16-level grayscale |
| `reterminal_e1004` / `e1004` | 1200 × 1600 | Six-color e-paper palette |

Use the matching firmware target and photo-preparation model. Files prepared
for one model are intentionally rejected by another when their dimensions do
not match.

## How it behaves

- A cold boot/reset displays the device MAC address, Wi-Fi SSID, SD-card photo
  count, battery level, and onboard temperature/humidity while connecting.
- The device obtains its address and optional NTP servers through DHCP.
- It synchronizes the clock on cold boot and once every 24 hours. DHCP-provided
  NTP is tried first, followed by `pool.ntp.org` and
  `time.cloudflare.com`.
- Successful NTP synchronization stores UTC in the onboard PCF8563. On a later
  deep-sleep wake, a failed NTP synchronization falls back to that clock only
  when its voltage-low (`VL`) flag is clear. An invalid or rolled-back ESP
  clock is recovered from the PCF8563 before NTP and quiet-hour decisions.
  Cold boots log its stored UTC and `VL` state. A CR1220 is needed to retain it
  across physical power-off.
- Wi-Fi is disabled immediately after time synchronization. Ordinary photo
  changes do not start the radio.
- Automatic changes occur every six hours by default. Any hardware button
  wakes the device and changes the photo.
- Every startup logs a `[wake]` line with the local time and whether it was a
  cold boot/reset, scheduled timer, or front-button wake.
- Between 01:00 and 07:00 by default, timer-driven changes are suppressed and
  the existing e-paper image remains unchanged. A button still changes it.
  Unlike the information viewers, the photo itself is never modified with a
  sleep message.
- The SD card and battery measurement circuit are powered down before deep
  sleep.

ESP32 RTC memory keeps the current photo position and last NTP-sync time across
deep sleep. A complete power loss starts again from the first directory entry;
this is separate from the PCF8563 hardware clock described above.

## Prepare the SD card

Format the card as FAT32 and create a directory named `photos` at its root.
Prepare photos on a Mac, Linux, or Windows computer:

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

The final argument can be one or more files or directories; directories are
searched recursively. Change both `--model` and the destination path for your
device.

The default `--fit cover` fills the entire display and center-crops overflow.
Use `--fit contain` to preserve every part of the photo with white letterbox
areas. Useful optional controls include:

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

Gamma values above 1 brighten midtones. Dithering is enabled by default and
normally gives the best photographic result; `--no-dither` is available for
flat artwork. E1001/E1003 conversion uses perceptual luminance for natural
monochrome skin brightness. On the six-color E1002/E1004, warm-tone protection
prevents green and blue correction dots from appearing in skin, orange, and
other warm areas. It is enabled by default;
`--no-warm-tone-protection` restores unrestricted palette dithering when exact
color mixing matters more.

Prepared files are exact-size, 4-bit BMPs. The firmware also has a convenience
fallback for ordinary JPEG, PNG, and BMP files, but on-device resizing is
lower quality and memory-intensive—especially on the E1003 and E1004.

## Configure and build

Install [PlatformIO Core](https://platformio.org/install/cli), then copy the
credentials template:

```bash
cd photo-viewer
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h`:

```cpp
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

The real file is excluded by `.gitignore`. Build and upload for the exact
model, for example:

```bash
pio run -e reterminal_e1003
pio run -e reterminal_e1003 \
  --target upload \
  --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Use `/dev/cu.usbserial-*` on macOS when that is the device's serial port.
Every application log line starts with local time in
`[YYYY-MM-DD HH:MM:SS.mmm]` format. Before the clock is synchronized, the
same format intentionally shows a 1970 date.

Run the native unit tests:

```bash
pio test -c platformio-test.ini -e native_test
```

The tests cover quiet-hour timing, daily NTP scheduling, button direction,
and photo-index wrapping. GitHub Actions runs them alongside all four
firmware builds.

## Settings

User-editable behavior is in `include/config.h`:

- `SLEEP_SECONDS`: normal automatic photo interval.
- `TIMEZONE`: POSIX timezone used for quiet hours and logs. Its offset sign is
  reversed; London can use `GMT0BST,M3.5.0/1,M10.5.0`, while Suzhou uses
  `CST-8`.
- `NTP_REFRESH_SECONDS`: internet time-sync interval.
- `QUIET_HOURS_ENABLED`, `QUIET_START_*`, and `QUIET_END_*`: overnight
  suppression period.
- `PHOTO_DIR`: SD-card directory.

## Buttons

All three front buttons are deep-sleep wake sources:

- GPIO4 selects the previous photo.
- GPIO3 and GPIO5 select the next photo.

On E1001–E1003 the green/right button is GPIO3. E1004 uses the three physical
front buttons; no touch input is required.

## SD-card errors

Without a mounted card, or when `/photos` is empty, the frame displays a clear
error and returns to deep sleep. Unreadable and incompatible files are skipped
up to `MAX_PHOTO_ATTEMPTS`; the next wake tries again.

## Privacy and network use

Photos never leave the SD card. The only normal outbound traffic is the daily
NTP synchronization; there is no photo service, cloud account, telemetry, or
background upload.
