# reTerminal E100X web flasher

A single-page, JavaScript-only flasher for the reTerminal E100X firmware in
this repository. It runs entirely in the browser using the Web Serial API
via [ESP Web Tools](https://esphome.github.io/esp-web-tools/) and the
underlying [esptool-js](https://github.com/espressif/esptool-js). No backend,
no installer, no `esptool.py`.

Hosted at:

- <https://danpodeanu.github.io/seeed-reterminal-E100X/>

## What it does

- Presents two dropdowns: board (E1001 / E1002 / E1003 / E1004 /
  E1005 "Seeed Sticky") and
  application (XKCD Viewer / Weather Viewer / Photo Viewer / Games). Games is
  enabled only when E1005 is selected.
- Looks up the repository's latest GitHub Release through the public API
  so the version tag can be displayed in the status line.
- Builds an ESP Web Tools manifest from the E1001-E1004 shared boot-chain
  assets or E1005-specific 32 MB boot-chain assets plus the selected
  `firmware-<app>-<board>-ota.bin`, all bundled with this Pages deployment.
- The single **Flash** button writes those four parts at their standard
  offsets. Leaving **Erase device?** unchecked preserves NVS settings and
  SPIFFS data; checking it performs a factory-fresh full-chip erase first.

## Files

| File | Purpose |
| --- | --- |
| `index.html` | Page shell, board/app dropdowns, `<esp-web-install-button>`. |
| `manifest.js` | Reads the latest release tag from the GitHub API and builds an in-memory ESP Web Tools manifest that points at same-origin firmware URLs. |

The ESP Web Tools bundle is loaded from the `unpkg` CDN so nothing needs to
be built or versioned locally.

## Why same-origin firmware

GitHub Release downloads redirect from `github.com` to a signed URL on
`release-assets.githubusercontent.com`, which is served from Azure Blob
Storage without `Access-Control-Allow-Origin`. A cross-origin browser
`fetch()` from the flasher page therefore fails with "Failed to fetch"
before the install can start. The Pages workflow works around this by
copying the release binaries into `/firmware/latest/` at deploy time so
the flasher and the binaries share an origin.

## How releases feed the flasher

Two workflows cooperate:

1. `.github/workflows/release.yml` runs on tag pushes (`v*`). For every
   supported application x board combination it builds the firmware with
   PlatformIO, merges the bootloader, partition table, OTA selector, and
   application into a single image with `esptool merge_bin`, and
   attaches both flavours to the GitHub Release:
   `firmware-<app>-<board>-full.bin` (merged, for direct USB flashing) and
   `firmware-<app>-<board>-ota.bin` (app-only, for SD OTA and the web
   flasher). It also publishes the shared E1001-E1004 three-part boot chain,
   the E1005-specific boot chain, the complete `fonts.zip`, and the legacy
   DejaVu-only `sans_bold_fonts.zip`.
2. After all release assets are uploaded, `release.yml` dispatches
   `.github/workflows/pages.yml` on `main`. The Pages workflow also runs
   for changes under `docs/` and on manual dispatch. It downloads every
   firmware binary plus both boot chains from the latest release into
   `/firmware/latest/`, assembles them alongside `docs/`, and deploys the
   combined site to GitHub Pages.

The flasher never needs a static per-release manifest committed to the
repository.

## Enabling GitHub Pages

In the repository settings, set Pages **Source** to **GitHub Actions**.
The `pages.yml` workflow will then have permission to deploy and will
publish to `https://<owner>.github.io/<repo>/` after each run.

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
