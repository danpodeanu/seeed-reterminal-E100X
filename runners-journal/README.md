# runners-journal

E-ink dashboard app for the Seeed reTerminal Sticky E1005. Fetches the
runner's journal and running stats from Supabase `dashboard()` RPC and
renders a portrait dashboard (week summary, recent runs, type breakdown,
history bar chart) on the 480x800 monochrome e-paper panel.

## Flashing

This repo builds firmware in GitHub Actions (Linux, no local toolchain
required) and uploads the binary as a downloadable artifact. Download it
from the Actions tab on the PR, unzip it, then flash with `esptool.py`.

### Prerequisites

```bash
pip3 install esptool pyserial
```

`esptool.py` is a pure-Python flashing tool — it does not trigger macOS
XProtect (unlike the ESP32 cross-compiler that PlatformIO downloads).

### Flash

1. Download the `firmware-reterminal_e1005` artifact from the latest
   successful GitHub Actions run on the PR, and unzip it.
2. Put the E1005 in bootloader mode: hold the BOOT button, press RESET,
   release BOOT.
3. Find the serial port:

```bash
ls /dev/cu.usbmodem*
```

4. Flash (replace `/dev/cu.usbmodemXXXX` with your port):

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  --baud 460800 write_flash -z \
  0x0 bootloader.bin \
  0x10000 firmware.bin \
  0x8000 partitions.bin
```

5. Press RESET to boot. The device connects to WiFi, fetches the
   dashboard, renders it, and sleeps.

### SD card (for æøå rendering)

Copy `fonts/sans_bold_*.vlw` to `/fonts/` on the SD card. Without these the
app falls back to ASCII GFX bitmap fonts (dagnavn vises uten æøå).

## Behaviour

On each wake the device connects to WiFi, calls `dashboard()`, parses the
JSON, renders the dashboard, commits the frame to the panel, then enters
deep sleep for `neste_oppvakning_s` seconds (server-scheduled wake times:
06/12/18/22 Europe/Oslo). On WiFi/fetch/parse failure it renders a status
screen and sleeps for the fallback interval (6 h).

License: GPL-2.0 (inherited from upstream danpodeanu/seeed-reterminal-E100X).
