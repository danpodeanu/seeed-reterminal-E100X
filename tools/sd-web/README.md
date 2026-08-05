# tools/sd-web — SD-card Wi-Fi portal

A one-file tool that boots the reTerminal into a Wi-Fi access point and
exposes the inserted SD card through a small web portal. Handy for
managing photos, fonts, cached data, or logs on the card without
pulling it out.

## What it does

1. Verifies an SD card is inserted (`PIN_SD_DETECT`); if not, draws an
   error screen and deep-sleeps.
2. Mounts the card.
3. Brings up a soft-AP on `192.168.1.1/24`, SSID
   `ReTerminal <first 6 hex chars of the AP MAC>`, **open** (no
   password).
4. Starts an HTTP server on port 80.
5. Renders two QR codes on the panel: one joins the Wi-Fi, one opens
   `http://192.168.1.1/`.

## Web portal

- **Browse** folders, showing name, size, and modification time.
- **Enter / up** navigation via breadcrumbs and folder links.
- **Download** files by clicking their name.
- **Delete** any file or folder (folders are removed recursively).
- **Create folder** by name.
- **Upload** files (streamed straight to the card; existing files are
  overwritten).

## Build

```sh
pio run -d tools/sd-web -e reterminal_e1001 -t upload
```

Boards: `reterminal_e1001`, `reterminal_e1002`, `reterminal_e1003`,
`reterminal_e1004`.

## Embedding in another app

The portal core lives in [`common-sd-web/`](../../common-sd-web/) so
any viewer app can bolt this on:

1. Add `symlink://../../common-sd-web` to the target app's
   `platformio.ini` `lib_deps`, plus `ricmoo/QRCode @ ^0.0.1`.
2. Start the panel with `epaper_setup::begin()`, render its status screen,
   mount SD with `sd_card::mount`, then call `sd_web_portal::begin()`.
3. Pump `sd_web_portal::loop()` from your Arduino `loop()`.
4. Use the `sd_web_portal::ui::` template helpers to draw the QR codes
   on the panel (or roll your own UI).

Panel rendering is kept as a template header (`sd_web_portal_ui.h`) so
the library never has to link against Seeed_GFX — the caller passes in
their own `EPaper` instance.

## Notes

- The AP is **open** on purpose, matching the "scan the QR to join"
  UX. Do not run this on a network you care about; anyone in range
  can browse and mutate the SD card while it is up.
- SD "creation time" is not exposed by the Arduino SD API on FAT/exFAT;
  the portal shows the last-modified timestamp instead.
- The DHCP pool is whatever `WiFi.softAPConfig(192.168.1.1, ...)`
  configures on the current ESP-IDF (typically `192.168.1.2` onwards,
  bounded by `maxConnections`). Four clients is plenty for a
  maintenance tool.
