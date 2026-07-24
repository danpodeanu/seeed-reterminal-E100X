# Weather display for reTerminal E1001–E1004

Turn a Seeed Studio reTerminal E-series device into a low-power weather
display. The device wakes periodically, downloads current conditions and a
three-day forecast from Open-Meteo, refreshes the e-paper panel, switches off
Wi-Fi, and returns to deep sleep.

The header preserves the same local SHT4x temperature/humidity and battery
status used by the XKCD Viewer. Outdoor weather is shown separately in the
main dashboard.

![Weather Viewer showing current conditions and a three-day forecast on a reTerminal E1003](assets/e1003-weather-screenshot.png)

Frame captured directly from a reTerminal E1003 using the built-in screenshot
export.

## Features

- One Arduino/PlatformIO source tree for E1001, E1002, E1003, and E1004.
- Gray4, six-color, or Gray16 output according to the selected panel.
- Current outdoor temperature, apparent temperature, humidity, wind, and
  WMO weather condition.
- Next likely rain time and probability from the upcoming hourly forecast.
- Three-day low/high, precipitation probability, and maximum UV forecast.
- Built-in SHT4x indoor temperature/humidity and battery gauge.
- Cold-boot screen with Wi-Fi SSID and station MAC address.
- Clearly labeled forecast date/time so stale data is easy to identify.
- Optional SD cache of the last successful forecast.
- Deep sleep between updates, with a configurable overnight quiet period.
- No API key, server, Docker container, or SD card required.

## Supported models

| PlatformIO environment | Panel | Native output |
| --- | --- | --- |
| `reterminal_e1001` | 800×480 UC8179 | Gray4 |
| `reterminal_e1002` | 800×480 ED2208 | six-color |
| `reterminal_e1003` | 1872×1404 ED103TC2 | Gray16 |
| `reterminal_e1004` | 1200×1600 T133A01 | six-color |

## Configure

Create the ignored credentials file:

```bash
cp include/secrets.h.example include/secrets.h
```

Edit `include/secrets.h` and set `WIFI_SSID` and `WIFI_PASSWORD`.

The example forecast location is London. Edit these values in
`include/config.h`:

```cpp
constexpr char LOCATION_NAME[] = "London";
constexpr double LATITUDE = 51.5074;
constexpr double LONGITUDE = -0.1278;
```

The device also synchronizes its system clock after Wi-Fi connects. Configure
the POSIX timezone and NTP servers in the same file:

```cpp
constexpr char TIMEZONE[] = "GMT0BST,M3.5.0/1,M10.5.0";
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.cloudflare.com";
constexpr uint32_t NTP_DHCP_TIMEOUT_MS = 4000;
```

The firmware requests NTP servers through DHCP option 42 before acquiring its
Wi-Fi lease. If DHCP supplies no server, or that server does not respond within
the configured DHCP timeout, it falls back to the two servers above.

The London rule uses GMT in winter and BST from the last Sunday in March until
the last Sunday in October. The centered `Weather at` timestamp still comes
from Open-Meteo and identifies the weather data's valid time; it is not the
NTP request time.

The default refresh interval is 30 minutes. Change `SLEEP_SECONDS` in the same
file if needed.

Automatic refreshes are suppressed overnight by default:

```cpp
constexpr bool QUIET_HOURS_ENABLED = true;
constexpr uint8_t QUIET_START_HOUR = 1;
constexpr uint8_t QUIET_START_MINUTE = 0;
constexpr uint8_t QUIET_END_HOUR = 7;
constexpr uint8_t QUIET_END_MINUTE = 0;
```

Rain timing examines the next 48 hourly intervals and selects the first with
at least 0.1 mm of forecast liquid precipitation and, when probability data
is available, at least 30% probability. These values are configurable:

```cpp
constexpr uint8_t RAIN_FORECAST_HOURS = 48;
constexpr float RAIN_START_THRESHOLD_MM = 0.1f;
constexpr uint8_t RAIN_PROBABILITY_THRESHOLD = 30;
```

An SD card is optional. When present, the firmware atomically stores the last
successful API response as `/weather/forecast.json`. If Wi-Fi or Open-Meteo is
unavailable later, that forecast is rendered with its original timestamp and
without an additional status label. Without a card, live weather works
normally.

Weather icons are vector graphics built into the firmware, not downloaded
images, so they consume no network traffic and require no SD cache.

## Build and upload

Build the environment matching the physical device:

```bash
pio run -e reterminal_e1001
pio run -e reterminal_e1001 --target upload \
  --upload-port /dev/cu.usbserial-11410
```

For E1003 on Linux:

```bash
pio run -e reterminal_e1003
pio run -e reterminal_e1003 --target upload --upload-port /dev/ttyUSB0
```

Monitor logs:

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Logging uses UART1 on GPIO43/GPIO44, matching the carrier USB-to-UART bridge.

## Operation

- Cold boot/reset displays the station MAC above `Connecting to <SSID>`.
- Every startup logs a `[wake]` line with the local time and whether it was a
  cold boot/reset, scheduled timer, or front-button wake.
- Timer wakes update every 30 minutes without an intermediate status refresh.
- Any front button wakes the device, beeps once, and forces an immediate live
  API update that bypasses HTTP caches.
- Hold the green GPIO3 button while the device is sleeping. Keep holding it
  through the first beep until a second beep confirms screenshot mode. With an
  SD card mounted, the newly rendered weather frame is written as an indexed
  BMP to `/screenshot.bmp`, replacing the previous screenshot. Remove the card
  and open that file on a computer to retrieve it.
- NTP runs on cold boot and at most once daily. DHCP-provided NTP is tried
  first, followed by the configured public servers. NTP failure is logged but
  does not block the weather request.
- From 01:00 until 07:00 by default, timer wakes return directly to sleep. The
  final scheduled refresh before 01:00 changes its title to
  `sleeping until 07:00`. A cold boot or any front-button wake overrides quiet
  hours, refreshes once, and then sleeps until 07:00.
- If a background update fails, an SD-cached forecast is used when available;
  otherwise the previous e-paper forecast remains visible.
- On a cold boot where no forecast has yet been shown, an error screen explains
  whether Wi-Fi or forecast download failed.
- Wi-Fi is switched off as soon as the Open-Meteo response is parsed, before
  cache writing, frame composition, and panel refresh. The battery measurement
  circuit is switched off during sleep.

## Weather data

The firmware calls Open-Meteo directly over HTTPS:

```text
https://api.open-meteo.com/v1/forecast
```

It requests current temperature, apparent temperature, relative humidity,
weather code and wind speed; hourly precipitation probability, precipitation,
rain and showers; plus daily weather code, low/high temperature, maximum UV
index, and maximum precipitation probability. Open-Meteo chooses the forecast
models appropriate for the configured coordinates.

For regions where Open-Meteo does not provide native 15-minute model data,
including Suzhou, exact rain onset remains an hourly forecast estimate rather
than a radar nowcast.

HTTPS certificate verification is disabled because the firmware does not
carry a CA bundle. Wi-Fi credentials are compiled into the firmware; keep
`include/secrets.h` private.

## Development

Build all targets before submitting changes:

```bash
pio run -e reterminal_e1001 -e reterminal_e1002 \
  -e reterminal_e1003 -e reterminal_e1004
```

Run the native unit tests:

```bash
pio test -c platformio-test.ini -e native_test
```

The tests cover quiet-hour boundaries, wake overrides, and daily refresh
timing. GitHub Actions runs them alongside all four firmware builds.

## References

- [Open-Meteo Weather Forecast API](https://open-meteo.com/en/docs)
- [Seeed PlatformIO setup](https://wiki.seeedstudio.com/epaper_work_with_platformio/)
- [Seeed reTerminal E-series Arduino guide](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/)

This is an unofficial project and is not affiliated with Open-Meteo or Seeed
Studio.
