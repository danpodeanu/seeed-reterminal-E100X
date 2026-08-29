# Calendar Viewer for reTerminal E1001-E1004

A low-power calendar dashboard for the Seeed Studio reTerminal E1001, E1002,
E1003, and E1004. It loads a public iCalendar feed or Google calendars,
renders Today, Week, and Month views with calendar colors, shows indoor
temperature and humidity plus a weather summary, and then returns to deep
sleep.

The device reconnects at the configured interval, but refreshes the e-paper
panel only when visible calendar, weather, sensor, view, or date content has
changed.

## Features

- Public HTTP(S) `.ics` feeds, including folded text, all-day and timed events,
  exclusions, recurrence overrides, cancellation, and common daily, weekly,
  monthly, and yearly recurrence rules.
- Google Calendar through a Google Cloud service account, with optional
  Workspace domain-wide delegation.
- Calendar and event colors mapped to each panel's native grayscale or
  six-color palette.
- Today, Week, and Month layouts selected with the front buttons.
- A persistent right sidebar with today's agenda above a compact weather card.
- Indoor SHT4x temperature/humidity and battery status in the header.
- Open-Meteo or QWeather summary, with optional severe-weather alerts.
- Captive-portal configuration for Wi-Fi, calendar, timezone, NTP, quiet
  hours, weather, refresh cadence, and diagnostics.
- DHCP networking, NTP synchronization, PCF8563 clock restoration, quiet
  hours, low-battery handling, and deep sleep.
- iCalendar URLs, Google IAM credentials, weather credentials, and settings
  are stored in device NVS. Calendar Viewer does not write credentials or
  calendar data to the SD card.

## Supported hardware

| Environment | Panel | Calendar colors |
| --- | --- | --- |
| `reterminal_e1001` | 800x480 landscape UC8179 | 4-level gray |
| `reterminal_e1002` | 800x480 landscape ED2208 | six-color |
| `reterminal_e1003` | 1872x1404 landscape ED103TC2 | 16-level gray |
| `reterminal_e1004` | 1600x1200 landscape T133A01 | six-color |

Use the environment matching the physical device; panel drivers and
dimensions differ between models.

## Getting started

### 1. Flash the firmware

Use the repository
[web flasher](https://danpodeanu.github.io/seeed-reterminal-E100X/) and select
**Calendar Viewer** plus the exact reTerminal model. Chrome or Edge can flash
the release directly over USB-C.

To build from source instead, install
[PlatformIO Core](https://platformio.org/install/cli), enter this directory,
and run:

```bash
pio run -e reterminal_e1001
```

Replace the environment with `reterminal_e1002`, `reterminal_e1003`, or
`reterminal_e1004` as needed.

### 2. Open the configuration portal

On first boot, the device opens its own Wi-Fi access point and displays join
and settings QR codes. Scan both codes, then save the Wi-Fi, calendar, and
weather settings.

To reopen the portal later, wake the sleeping device while holding the green
button for at least one second. Press the green button again to leave the
portal after saving.

### 3. Configure a calendar source

Choose one source under **Settings -> Calendar source**.

#### Public iCalendar feed

1. Select **Ical**.
2. Paste the complete `.ics` subscription URL into **iCalendar URL**.
3. Save and reboot.

The URL is treated as a secret: it is stored in NVS and the portal returns only
a "saved" placeholder after configuration. Prefer HTTPS, especially when a
provider embeds an access token in the URL. HTTPS certificates are verified
against the Mozilla root bundle supplied by the ESP32 framework.

Common IANA `TZID` values covered by the portal's timezone presets are
converted with their own UTC offsets and daylight-saving rules. Unknown
`TZID` values are rejected instead of being displayed at the wrong time.
UTC, floating/local, all-day, `DURATION`, `EXDATE`, `RECURRENCE-ID`, and
daily/weekly/monthly/yearly `RRULE` forms are supported, including `WKST`,
monthly `BYDAY`, and `BYMONTHDAY`. Unsupported recurrence selectors are
reported as a feed error instead of silently producing a different schedule.

#### Google Calendar

1. In Google Cloud, enable the **Google Calendar API**.
2. Create a service account and download one JSON key from
   **IAM & Admin -> Service Accounts -> Keys**.
3. Open the device portal's **Google IAM** tab and upload that JSON file.
4. Share each calendar with the service-account email shown by the portal,
   granting at least **See all event details**.
5. Select **Google** under Calendar source. Leave **Google calendar IDs** blank
   to load shared calendars, or enter a comma-separated allowlist. Up to 12
   calendars and 128 visible events are loaded per refresh.
6. Save and reboot.

For Workspace domain-wide delegation, authorize the service account's OAuth
client ID for
`https://www.googleapis.com/auth/calendar.readonly`, then set **Google
delegated user** to the user whose calendars should be read.

The uploaded JSON is capped at 8 KiB and parsed in RAM. Only the required
service-account fields are saved in the `calendar` NVS namespace; the private
key is never placed on SD, returned by the status endpoint, or logged. The
portal can remove the stored credential. Standard ESP32 NVS is not encrypted
unless flash encryption is separately provisioned on the device.

### 4. Configure weather

Open-Meteo is the default and needs only latitude, longitude, and a location
name. QWeather uses the same Ed25519 credentials and settings as Weather
Viewer. Temperature and wind units are configurable. Weather is cached in NVS
for temporary provider failures; no SD card is required.

## Controls and refresh behavior

| Action from deep sleep | Result |
| --- | --- |
| Tap green | Switch to Today and refresh |
| Tap right | Cycle Today -> Week -> Month |
| Tap left | Cycle Today -> Month -> Week |
| Hold green for at least 1 second | Open the configuration portal |
| Wait for the timer | Check for calendar and weather updates |

The selected view is persisted. Button wakes bypass HTTP caches. Scheduled
wakes are suppressed during configured quiet hours.

After a successful fetch, Calendar Viewer fingerprints the visible date
window, event data and colors, selected view, rounded indoor readings, weather
summary, and render-affecting settings. If that fingerprint matches the last
successful frame, panel initialization and refresh are skipped. If a calendar
download fails and a prior frame exists, the retained e-paper image is left
untouched and the device retries after five minutes.

## Configuration storage

All runtime settings live in NVS and survive normal reflashing. The tracked
`include/secrets.h` contains placeholders only; compile-time Wi-Fi and
QWeather values are optional fallbacks for local builds. Never commit real
credentials.

The current NVS partition is 20 KiB. Typical Google service-account keys fit
alongside the app settings, but very large or unusually formatted IAM files
are rejected.

## Development

Run native parser and calendar-logic tests:

```bash
pio test -c platformio-test.ini -e native_test
```

Build every supported model:

```bash
pio run -e reterminal_e1001
pio run -e reterminal_e1002
pio run -e reterminal_e1003
pio run -e reterminal_e1004
```

Serial logging uses UART1 on GPIO43/GPIO44 at 115200 baud. Calendar URLs,
OAuth tokens, and private keys are deliberately omitted from logs.
