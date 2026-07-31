# reTerminal E100X web flasher

A single-page, JavaScript-only flasher for the reTerminal E100X firmware in
this repository. It runs entirely in the browser using the Web Serial API
via [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and the
underlying [esptool-js](https://github.com/espressif/esptool-js). No backend,
no installer, no `esptool.py`.

Hosted at:

- <https://danpodeanu.github.io/seeed-reterminal-E100X/>

## What it does

- Presents two dropdowns: board (E1001 / E1002 / E1003 / E1004) and
  application (XKCD Viewer / Weather Viewer / Photo Viewer).
- Looks up the repository's latest GitHub Release through the public API.
- Finds the merged firmware asset that matches the selection
  (`firmware-<app>-<board>.bin`) and hands its download URL to the
  ESP Web Tools install button.
- The install button connects to the reTerminal over USB serial, writes the
  merged image at flash offset 0, and reboots.

## Files

| File | Purpose |
| --- | --- |
| `index.html` | Page shell, board/app dropdowns, `<esp-web-install-button>`. |
| `manifest.js` | Reads the latest release from the GitHub API and builds an in-memory ESP Web Tools manifest. |

The ESP Web Tools bundle is loaded from the `unpkg` CDN so nothing needs to
be built or versioned locally.

## How releases feed the flasher

The `.github/workflows/release.yml` workflow runs on tag pushes (`v*`). For
every application × board combination it:

1. Builds the firmware with PlatformIO.
2. Merges the bootloader, partition table, OTA selector, and application
   into a single flash image with `esptool merge_bin`.
3. Uploads the result as `firmware-<app>-<board>.bin` to the GitHub Release
   attached to the tag.

The web flasher discovers those assets at runtime; no static per-release
manifest is committed.

## Enabling GitHub Pages

In the repository settings, set Pages to serve from the `main` branch and
the `/docs` folder. That publishes this directory to
`https://<owner>.github.io/<repo>/`.

## Browser support

Web Serial is only implemented in Chromium-based browsers (Chrome, Edge,
Opera, Arc, Brave). Firefox and Safari cannot flash from this page.

## Local development

The page uses only static assets and public CDNs, so any static server
works:

```sh
cd docs
python -m http.server 8000
```

Then open <http://localhost:8000/> in Chrome or Edge. Web Serial requires
a secure context; `localhost` counts as secure so no HTTPS is needed for
local testing.
