# Calendar Viewer for reTerminal E1001-E1004

A low-power calendar dashboard for the Seeed Studio reTerminal E1001, E1002,
E1003, and E1004. It loads a public iCalendar feed or Google calendars,
renders today's agenda together with week and month calendars, shows indoor
temperature and humidity plus a weather summary, and then returns to deep sleep.

The device reconnects at the configured interval, but refreshes the e-paper
panel only when visible calendar, weather, sensor, or date content has changed.

## Features

- Public HTTP(S) `.ics` feeds, including folded text, all-day and timed events,
  exclusions, recurrence overrides, cancellation, and common daily, weekly,
  monthly, and yearly recurrence rules.
- All on-panel text uses embedded Noto Sans SemiCondensed Bold. Event titles
  include ASCII, Latin-1, Latin Extended, combining marks, punctuation, and
  currency symbols; no SD-card font files are required.
- Google Calendar through a Google Cloud service account, with optional
  Workspace domain-wide delegation.
- Calendar and event colors Floyd-Steinberg-dithered into each panel's native
  grayscale or six-color palette.
- A fixed dashboard with the current week above a six-week month grid and
  separate Today and Upcoming agendas above a compact weather card in the
  sidebar. Upcoming entries from the next six weeks include their date and
  start/end times, fill the available event space, and use a compact
  right-aligned `+N more` line only when needed. Week and month grids mark the
  current day in green on six-color panels.
- A wall-planner layout uses open column gutters, weekday/date labels inside
  the week cells, a light-blue weekday header above date-only month cells,
  horizontal week separators, rounded event bands, and clean white agenda and
  weather areas.
- Sunday is the default first day of the week; the Presentation settings can
  switch week and month grids to Monday-first.
- A Presentation toggle controls the single-Google-calendar background and
  defaults on. Plain grids shade non-current Saturdays and Sundays light gray.
- Shared Meteocons weather artwork plus native calendar, climate, location, and
  alert icons, with dithered light-blue calendar and light-yellow weather
  headers on six-color models.
- Indoor SHT4x temperature/humidity in the weather card and battery status in
  the header.
- Open-Meteo or QWeather summary, with optional severe-weather alerts.
- Captive-portal configuration for Wi-Fi, calendar, timezone, 12/24-hour time
  format, NTP, quiet hours, weather, refresh cadence, and diagnostics.
- An SD tab in the configuration portal can browse, upload, download, create,
  and delete files on the inserted microSD card.
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
button for at least two seconds, then release it before five seconds. Press it
again to leave the portal after saving. The **SD** tab exposes the inserted
microSD card through the same web UI.

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

1. In a Google Cloud project, open **APIs & Services -> Library** and enable
   **Google Calendar API**.
2. Open **IAM & Admin -> Service Accounts**, create a service account, then
   select **Keys -> Add key -> Create new key -> JSON**. Keep the downloaded
   file private.
3. In Google Calendar, open the target calendar's **Settings and sharing**.
   Under **Share with specific people or groups**, add the service account's
   `client_email` from the JSON file. Grant **Make changes to events** (or the
   equivalent **Make changes and see all event details** permission).
4. In the same calendar settings, open **Integrate calendar** and copy
   **Calendar ID**. It often resembles
   `c_abc123@group.calendar.google.com`. Do not use the service-account email as
   the Calendar ID.
5. Open the device configuration portal, select **Google** under Calendar
   source, and paste one or more comma-separated Calendar IDs into **Google
   calendar IDs**. Up to 12 calendars and 128 visible events are loaded per
   refresh. Leave this field blank only to discover calendars already present
   in the service account's CalendarList.
6. Open the portal's **Google IAM** tab, upload the downloaded JSON key, then
   select **Reboot to viewer**.

For configured IDs, Calendar Viewer requests
`https://www.googleapis.com/auth/calendar.readonly` and
`https://www.googleapis.com/auth/calendar.calendarlist`. On the first refresh,
it reads each CalendarList entry and adds a missing subscription to the service
account's list. This allows events without their own `colorId` to inherit the
calendar entry's `backgroundColor`; explicitly colored events use Google's
event-color palette. When exactly one Google calendar is loaded and that
background color is available, the week and month cells use its dithered color.
Adding the subscription does not modify calendar events.

If color-enabled authentication reports an explicit scope/authorization error,
or CalendarList access returns HTTP 401/403, the firmware continues with
`https://www.googleapis.com/auth/calendar.events.readonly`. Events still load,
using the built-in color palette and default calendar color. For Workspace
domain-wide delegation, authorize the service account's OAuth client ID for all
three scopes above, then set **Google delegated user** to the user whose
calendars and display colors should be read.

The uploaded JSON is capped at 8 KiB and parsed in RAM. Only the required
service-account fields are saved in the `calendar` NVS namespace; the private
key is never placed on SD, returned by the status endpoint, or logged. The
portal can remove the stored credential. Standard ESP32 NVS is not encrypted
unless flash encryption is separately provisioned on the device.

Under **Settings -> Presentation**, choose Sunday (the default) or Monday as the
first day of the week and select either the default 12-hour am/pm clock or a
24-hour clock. The single-calendar background toggle defaults on; turn it off
to use white weekday cells and light-gray weekend cells.
Timed entries in the Today agenda show both their start and end times.

### 4. Configure weather

Open-Meteo is the default and needs only latitude, longitude, and a location
name. QWeather uses the same Ed25519 credentials and settings as Weather
Viewer. Temperature and wind units are configurable. Weather is cached in NVS
for temporary provider failures; no SD card is required.

## Controls and refresh behavior

| Action from deep sleep | Result |
| --- | --- |
| Tap any front button | Refresh calendar and weather |
| Hold green for 2–5 seconds, then release | Open the configuration portal |
| Keep holding green for 5 seconds | Save the rendered dashboard to `/screenshot.png` on microSD |
| Wait for the timer | Check for calendar and weather updates |

Button wakes bypass HTTP caches. Scheduled wakes are suppressed during
configured quiet hours.

After a successful fetch, Calendar Viewer fingerprints the visible date
window, event data and colors, weather summary, power, and render-affecting
settings. Indoor climate refreshes only after temperature moves at least 1 C or
humidity moves at least 5 percentage points from the last rendered values. If
none of those inputs changed, panel initialization and refresh are skipped.
Before a changed frame is refreshed, the serial log identifies each changed
component and reports useful current values. The plain-white diagnostic
`Google checked` footer is updated whenever another component causes a refresh,
but its timestamp never triggers a refresh by itself. iCalendar download
failures preserve an existing calendar frame and retry after five minutes.
Google failures display the API error and retry on the normal configured
schedule; an unchanged error does not cause another panel refresh.

The five-second screenshot gesture is enabled by default for development. A
production build can disable only that gesture with
`-D CALENDAR_GREEN_SCREENSHOT_ENABLED=0`; the SD web UI remains enabled.

## Configuration storage

All runtime settings live in NVS and survive normal reflashing. The tracked
`include/secrets.h` contains placeholders only; compile-time Wi-Fi and
QWeather values are optional fallbacks for local builds. Never commit real
credentials.

Google credentials, iCalendar URLs, and calendar data are never written to the
SD card. A requested screenshot is the only Calendar Viewer file created
automatically; the SD web UI otherwise changes files only in response to the
operator.

The current NVS partition is 20 KiB. Typical Google service-account keys fit
alongside the app settings, but very large or unusually formatted IAM files
are rejected.

## Development

Run native parser, refresh-policy, and render-geometry tests:

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

Serial logging uses UART1 on GPIO43/GPIO44 at 115200 baud. Google request
stages, HTTP statuses, response sizes, sanitized API errors, page counts, and
event counts are logged. Transport failures are retried once with a fresh TLS
connection and report sanitized Wi-Fi, DNS, TLS, and heap diagnostics. Display
refresh logs list the render components that changed without exposing calendar
URLs or credentials. OAuth tokens, signed JWTs, and private keys are
deliberately omitted.
