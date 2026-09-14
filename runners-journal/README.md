# runners-journal

E-ink dashboard app for the Seeed reTerminal Sticky E1005. Fetches the
runner's journal and running stats from Supabase `dashboard()` RPC and
renders a portrait dashboard (week summary, recent runs, type breakdown,
history bar chart) on the 480x800 monochrome e-paper panel.

## Setup

1. Copy `include/secrets.h.example` to `include/secrets.h` and fill in your
   WiFi credentials and Supabase anon key.
2. Copy the smooth font files from `fonts/sans_bold_*.vlw` to the root of
   the SD card under `/fonts/`. These are needed to render Norwegian text
   (søndag, løp, æøå) correctly. Without them the app falls back to ASCII
   GFX bitmap fonts (dagnavn vil vises uten æøå).
3. Build for the E1005:

```bash
pio run -e reterminal_e1005
pio run -e reterminal_e1005 -t upload
pio device monitor -e reterminal_e1005
```

## Behaviour

On each wake the device connects to WiFi, calls `dashboard()`, parses the
JSON, renders the dashboard, commits the frame to the panel, then enters
deep sleep for `neste_oppvakning_s` seconds (server-scheduled wake times:
06/12/18/22 Europe/Oslo). On WiFi/fetch/parse failure it renders a status
screen and sleeps for the fallback interval (6 h).

License: GPL-2.0 (inherited from upstream danpodeanu/seeed-reterminal-E100X).
