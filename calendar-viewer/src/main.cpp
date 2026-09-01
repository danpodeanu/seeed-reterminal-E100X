#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <climits>
#include <math.h>
#include <time.h>
#include <utility>

#include "app_logger.h"
#include "app_logic.h"
#include "board_pins.h"
#include "calendar_config_runtime.h"
#include "calendar_config_schema.h"
#include "calendar_latin_font.h"
#include "calendar_logic.h"
#include "calendar_portrait_layout.h"
#include "calendar_provider.h"
#include "calendar_render.h"
#include "calendar_wifi_credentials.h"
#include "climate_sensor.h"
#include "config.h"
#include "config_portal.h"
#include "config_portal_ui.h"
#include "epaper_setup.h"
#include "google_credentials.h"
#include "google_credentials_portal.h"
#include "hardware.h"
#include "local_time.h"
#include "low_battery.h"
#include "ntp_sync.h"
#include "panel_watchdog.h"
#include "pcf8563_utc.h"
#include "peripheral_power.h"
#include "power_latch.h"
#include "quiet_hours.h"
#include "repo_qr.h"
#include "rtc_sync.h"
#include "sensors.h"
#if CALENDAR_GREEN_SCREENSHOT_ENABLED
#include "screenshot_png.h"
#endif
#include "sd_card.h"
#include "sd_web_portal.h"
#include "theme.h"
#include "timestamped_logger.h"
#include "usb_screen_capture.h"
#include "version.h"
#include "weather_app_logic.h"
#include "weather_data.h"
#include "weather_summary.h"
#include "wifi_schema.h"
#include "wifi_sta.h"
#if RETERMINAL_MODEL == 1005
#include "e1005_fast_refresh.h"
#include "gt911_touch.h"
#endif

SET_LOOP_TASK_STACK_SIZE(16U * 1024U);

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver"
#endif

TimestampedLogger appLog(Serial1);

namespace {

using namespace board;
using namespace theme;

constexpr const char* kFrameValidKey = "frame_valid";
constexpr const char* kFrameHashKey = "frame_hash";
constexpr const char* kFrameKindKey = "frame_kind";
constexpr const char* kFrameComponentsKey = "frame_parts";
// Increment when rendering changes without changing the underlying data.
constexpr uint32_t kCalendarFrameRevision = 29;

enum class FrameKind : uint8_t {
  None = 0,
  Calendar = 1,
  Status = 2,
  SleepingCalendar = 3,
};

EPaper epaper;
usb_screen_capture::Server usbScreenCapture;
Adafruit_SHT4x sht4;
sensors::Readings sensorReadings;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
#if RETERMINAL_MODEL == 1005
RTC_DATA_ATTR uint8_t retainedCalendarView =
    static_cast<uint8_t>(config::CalendarView::Today);
RTC_DATA_ATTR time_t retainedSelectedDay = 0;
TwoWire touchWire(1);
Gt911Touch touch;
E1005FastRefresh fastRefresh(epaper);
constexpr uint8_t kSsd1677BorderWaveformCommand = 0x3C;
constexpr uint8_t kSsd1677FollowLut1 = 0x01;
constexpr uint8_t kSsd1677DeepSleepCommand = 0x10;
constexpr uint8_t kSsd1677DeepSleepEnter = 0x03;

struct AwakeButtonState {
  int pin;
  const char* name;
  calendar_logic::CalendarNavigation navigation;
  int stableLevel = HIGH;
  int sampledLevel = HIGH;
  uint32_t changedAtMs = 0;
  bool armed = true;
};

AwakeButtonState awakeButtons[] = {
    {PIN_BUTTON_GREEN, "OK", calendar_logic::CalendarNavigation::Today},
    {PIN_BUTTON_RIGHT, "UP", calendar_logic::CalendarNavigation::Previous},
    {PIN_BUTTON_LEFT, "DOWN", calendar_logic::CalendarNavigation::Next},
};
#endif
bool panelStarted = false;
bool framebufferReady = false;
bool sdReady = false;
bool screenshotRequested = false;

#if RETERMINAL_MODEL == 1003
float panelWaveformTemperatureC() {
  if (!sensorReadings.climateValid) return 16.0f;
  return static_cast<float>(
      constrain(static_cast<int>(lroundf(sensorReadings.temperatureC)), 0, 50));
}
#endif

void beginPanel() {
  if (panelStarted) return;
#if RETERMINAL_MODEL == 1003
  if (!sensorReadings.climateValid) {
    sensorReadings.climateValid = climate::readSht4x(
        sht4, sensorReadings.temperatureC, sensorReadings.humidityPct,
        config::SENSOR_READ_ATTEMPTS, config::SENSOR_RETRY_DELAY_MS);
  }
#endif
#if RETERMINAL_MODEL == 1005
  if (sdReady) {
    SD.end();
    sdReady = false;
  }
  // Sticky shares panel SPI signals with the separately powered SD slot.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
#endif
  epaper_setup::begin(epaper);
  epaper.setRotation(config::PANEL_ROTATION);
#if RETERMINAL_MODEL == 1003
  epaper.setTemp(panelWaveformTemperatureC);
#endif
#if RETERMINAL_MODEL == 1005
  LOG.printf("[panel] orientation=portrait rotation=%d geometry=%dx%d\n",
             config::PANEL_ROTATION, config::PANEL_WIDTH,
             config::PANEL_HEIGHT);
#endif
  panelStarted = true;
}

void initializePanelColorMode() {
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
}

#if RETERMINAL_MODEL == 1005
void prepareE1005FullRefresh() {
  epaper.wake();
  epaper.writecommand(kSsd1677BorderWaveformCommand);
  epaper.writedata(kSsd1677FollowLut1);
}

void deepSleepE1005Panel() {
  epaper.sleep();
  epaper.writecommand(kSsd1677DeepSleepCommand);
  epaper.writedata(kSsd1677DeepSleepEnter);
  delay(100);
}
#endif

void refreshPanel() {
#if RETERMINAL_MODEL == 1005
  // Seeed_GFX uses a generic SSD1677 border value. The Sticky stock driver
  // uses LUT1 for full monochrome refreshes.
  prepareE1005FullRefresh();
#endif
  panel_watchdog::refresh(epaper);
  framebufferReady = true;
}

bool saveRequestedScreenshot() {
  if (!screenshotRequested) return false;
  bool saved = false;
#if CALENDAR_GREEN_SCREENSHOT_ENABLED
  if (!sdReady) {
    sdReady = sd_card::mount(epaper.getSPIinstance(), config::SD_ROOT);
  }
  if (sdReady) {
    saved = screenshot::saveScreenshotPng(
        epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT);
  } else {
    LOG.println("[screenshot] request ignored: SD card is unavailable");
  }
#else
  LOG.println("[screenshot] green-button capture is disabled in this build");
#endif
  screenshotRequested = false;
  return saved;
}

void powerDownAndSleep(uint64_t sleepSeconds, bool timerWakeEnabled = true) {
  wifi_sta::disable();
  if (sdReady) {
    SD.end();
    sdReady = false;
  }
#if RETERMINAL_MODEL == 1005
  if (panelStarted) {
    deepSleepE1005Panel();
    epaper.getSPIinstance().end();
  }
  pinMode(PIN_SD_CS, INPUT);
  pinMode(PIN_SD_SCK, INPUT);
  pinMode(PIN_SD_MOSI, INPUT);
  pinMode(PIN_SD_MISO, INPUT);
  peripheral_power::disableSd();
#endif
  if (panelStarted) epaper_setup::shutdownPanelPower();
  peripheral_power::disable();
  if (PIN_BATTERY_ENABLE >= 0) {
    pinMode(PIN_BATTERY_ENABLE, OUTPUT);
    digitalWrite(PIN_BATTERY_ENABLE, LOW);
  }

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const uint32_t releaseStart = millis();
  while ((!digitalRead(PIN_BUTTON_GREEN) ||
          !digitalRead(PIN_BUTTON_RIGHT) ||
          !digitalRead(PIN_BUTTON_LEFT)) &&
         millis() - releaseStart < 2000) {
    delay(10);
  }

  const bool wakePinsReady =
      hardware::configureWakePin(PIN_BUTTON_GREEN) &&
      hardware::configureWakePin(PIN_BUTTON_RIGHT) &&
      hardware::configureWakePin(PIN_BUTTON_LEFT);
  const uint64_t wakeMask =
      (1ULL << PIN_BUTTON_GREEN) | (1ULL << PIN_BUTTON_RIGHT) |
      (1ULL << PIN_BUTTON_LEFT);
  const esp_err_t buttonResult =
      wakePinsReady
          ? esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  const esp_err_t timerResult =
      timerWakeEnabled
          ? esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL)
          : esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  LOG.printf("[sleep] buttons=%s timer=%s interval=%llus\n",
             esp_err_to_name(buttonResult),
             timerWakeEnabled ? esp_err_to_name(timerResult) : "disabled",
             static_cast<unsigned long long>(sleepSeconds));
  if (buttonResult != ESP_OK &&
      (!timerWakeEnabled || timerResult != ESP_OK)) {
    LOG.println("[sleep] no wake source available; restarting");
    LOG.flush();
    delay(100);
    ESP.restart();
  }
  LOG.flush();
  hardware::setStatusLed(false);
  power_latch::holdDuringDeepSleep();
  esp_deep_sleep_start();
}

bool loadFrameState(uint64_t& hash, FrameKind& kind) {
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, true)) return false;
  const bool valid = prefs.getBool(kFrameValidKey, false);
  hash = prefs.getULong64(kFrameHashKey, 0);
  kind = static_cast<FrameKind>(
      prefs.getUChar(kFrameKindKey, static_cast<uint8_t>(FrameKind::None)));
  prefs.end();
  return valid;
}

constexpr bool isPreservableCalendarFrame(FrameKind kind) {
  return kind == FrameKind::Calendar ||
         kind == FrameKind::SleepingCalendar;
}

bool loadFrameComponents(calendar_logic::FrameComponents& components) {
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, true)) return false;
  const size_t storedSize = prefs.getBytesLength(kFrameComponentsKey);
  const size_t read =
      storedSize == sizeof(components)
          ? prefs.getBytes(kFrameComponentsKey, &components, sizeof(components))
          : 0;
  prefs.end();
  return read == sizeof(components) &&
         calendar_logic::frameComponentsCompatible(components);
}

bool saveFrameState(
    uint64_t hash, FrameKind kind,
    const calendar_logic::FrameComponents* components = nullptr) {
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, false)) return false;
  prefs.putBool(kFrameValidKey, false);
  bool written =
      prefs.putULong64(kFrameHashKey, hash) > 0 &&
      prefs.putUChar(kFrameKindKey, static_cast<uint8_t>(kind)) > 0;
  if (written && kind == FrameKind::Calendar) {
    written =
        components != nullptr &&
        prefs.putBytes(kFrameComponentsKey, components, sizeof(*components)) ==
            sizeof(*components);
  }
  if (written) written = prefs.putBool(kFrameValidKey, true) > 0;
  prefs.end();
  return written;
}

void invalidateFrameState() {
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, false)) return;
  prefs.putBool(kFrameValidKey, false);
  prefs.end();
}

void addRoundedWeather(calendar_logic::Fingerprint& hash,
                       const WeatherData& weather) {
  hash.addValue(weather.valid);
  if (!weather.valid) return;
  hash.addValue(weather.weatherCode);
  hash.addValue(weather.isDay);
  const int current = static_cast<int>(lroundf(weather.temperatureC));
  const int low = isfinite(weather.days[0].minimumC)
                      ? static_cast<int>(lroundf(weather.days[0].minimumC))
                      : INT_MIN;
  const int high = isfinite(weather.days[0].maximumC)
                       ? static_cast<int>(lroundf(weather.days[0].maximumC))
                       : INT_MIN;
  const int wind = isfinite(weather.windKmh)
                       ? static_cast<int>(lroundf(weather.windKmh))
                       : INT_MIN;
  hash.addValue(current);
  hash.addValue(low);
  hash.addValue(high);
  hash.addValue(wind);
  hash.add(std::string(weather.alertTitle.c_str()));
}

struct CalendarFrameFingerprints {
  uint64_t combined = 0;
  calendar_logic::FrameComponents components;
};

CalendarFrameFingerprints frameFingerprints(
    const calendar::Data& data, const calendar::Window& window,
    config::CalendarView view, const WeatherData& weather,
    const String& diagnosticFooter, time_t now, time_t displayDay) {
  CalendarFrameFingerprints result;
  calendar::Window visibleWindow = window;
#if RETERMINAL_MODEL == 1005
  if (view == config::CalendarView::Today) {
    visibleWindow.start = calendar_logic::localMidnight(displayDay);
    visibleWindow.end = calendar_logic::addLocalDays(visibleWindow.start, 43);
    if (visibleWindow.start < window.start) visibleWindow.start = window.start;
    if (visibleWindow.end > window.end) visibleWindow.end = window.end;
  } else {
    visibleWindow = calendar_logic::displayWindow(
        view, displayDay, calendar_config::runtime::weekStart());
  }
#endif
  const uint64_t calendarHash =
      calendar_logic::dataFingerprint(data, visibleWindow);

  calendar_logic::Fingerprint rendererHash;
  rendererHash.addValue(kCalendarFrameRevision);
  rendererHash.add(std::string(board::FIRMWARE_VERSION));
  result.components.renderer = rendererHash.value();

  calendar_logic::Fingerprint calendarHashDetails;
  calendarHashDetails.addValue(calendarHash);
  calendarHashDetails.addValue(data.truncated);
  result.components.calendar = calendarHashDetails.value();

  calendar_logic::Fingerprint presentationHash;
  presentationHash.addValue(calendar_config::runtime::weekStart());
  presentationHash.addValue(calendar_config::runtime::timeFormat());
  presentationHash.addValue(
      calendar_config::runtime::showSingleCalendarBackground());
  presentationHash.add(std::string(calendar_config::runtime::timezone()));
  presentationHash.add(std::string(calendar_config::runtime::locationName()));
  presentationHash.addValue(calendar_config::runtime::temperatureUnit());
  presentationHash.addValue(calendar_config::runtime::windSpeedUnit());
#if RETERMINAL_MODEL == 1005
  presentationHash.addValue(view);
  presentationHash.addValue(calendar_logic::localMidnight(displayDay));
#else
  static_cast<void>(view);
  static_cast<void>(displayDay);
#endif
  result.components.presentation = presentationHash.value();

  const bool environmentVisible =
#if RETERMINAL_MODEL == 1005
      view == config::CalendarView::Today;
#else
      true;
#endif
  const bool climateValid =
      environmentVisible && sensorReadings.climateValid &&
      isfinite(sensorReadings.temperatureC) &&
      isfinite(sensorReadings.humidityPct);
  result.components.indoorClimateValid = climateValid ? 1U : 0U;
  if (climateValid) {
    result.components.indoorTemperatureC = sensorReadings.temperatureC;
    result.components.indoorHumidityPct = sensorReadings.humidityPct;
  }

  const bool batteryValid =
      sensorReadings.batteryValid && sensorReadings.batteryPct >= 0 &&
      sensorReadings.batteryPct <= 100;
  result.components.batteryValid = batteryValid ? 1U : 0U;
  result.components.batteryPct =
      batteryValid ? sensorReadings.batteryPct : -1;
  result.components.externalPowerValid =
      sensorReadings.externalPowerValid ? 1U : 0U;
  result.components.externalPower =
      sensorReadings.externalPowerValid && sensorReadings.externalPower ? 1U
                                                                        : 0U;

  calendar_logic::Fingerprint weatherHash;
  if (environmentVisible) addRoundedWeather(weatherHash, weather);
  result.components.weather = weatherHash.value();

  struct tm localDate = {};
  const bool haveLocalDate = localtime_r(&now, &localDate) != nullptr;
  calendar_logic::Fingerprint dateHash;
  if (haveLocalDate) {
    dateHash.addValue(localDate.tm_year);
    dateHash.addValue(localDate.tm_yday);
  }
  result.components.date = dateHash.value();

  result.combined = calendar_logic::frameRefreshFingerprint(
      result.components, diagnosticFooter.c_str());
  return result;
}

String roundedMeasurement(float value, const char* suffix) {
  if (!isfinite(value)) return "unavailable";
  return String(static_cast<int>(lroundf(value))) + suffix;
}

void logCalendarFrameChanges(
    bool havePreviousFrame, FrameKind previousKind,
    bool havePreviousComponents,
    const calendar_logic::FrameComponents& previousComponents,
    const calendar_logic::FrameComponents& currentComponents,
    config::CalendarView view, uint16_t changes,
    const calendar::Data& data, const WeatherData& weather,
    time_t now) {
  if (!havePreviousFrame) {
    LOG.println("[display] refresh changes: full render; no previous frame");
    return;
  }
  if (previousKind != FrameKind::Calendar) {
    LOG.println(
        "[display] refresh changes: full render; previous frame was a status "
        "screen");
    return;
  }
  if (!havePreviousComponents) {
    LOG.println(
        "[display] refresh changes: full render; component history is "
        "unavailable or incompatible");
    return;
  }

  using calendar_logic::FrameComponentChange;

  if (calendar_logic::frameComponentChanged(
          changes, FrameComponentChange::Renderer)) {
    LOG.printf("[display] changed: renderer/firmware (revision=%lu, fw=%s)\n",
               static_cast<unsigned long>(kCalendarFrameRevision),
               board::FIRMWARE_VERSION);
  }
  if (calendar_logic::frameComponentChanged(
          changes, FrameComponentChange::Calendar)) {
    LOG.printf(
        "[display] changed: calendar events/sources (events=%u, sources=%u, "
        "limited=%s)\n",
        static_cast<unsigned>(data.events.size()),
        static_cast<unsigned>(data.sources.size()),
        data.truncated ? "yes" : "no");
  }
  if (calendar_logic::frameComponentChanged(
          changes, FrameComponentChange::Presentation)) {
#if RETERMINAL_MODEL == 1005
    LOG.printf("[display] changed: selected view (%s)\n",
               calendar_logic::calendarViewName(view));
#endif
    LOG.printf(
        "[display] changed: presentation settings (week=%s, clock=%s, "
        "single-calendar-background=%s, timezone=%s, location=%s, units=%s/%s)"
        "\n",
        calendar_config::runtime::weekStart() == config::WeekStart::Sunday
            ? "Sunday"
            : "Monday",
        calendar_config::runtime::timeFormat() ==
                config::TimeFormat::TwelveHour
            ? "12-hour"
            : "24-hour",
        calendar_config::runtime::showSingleCalendarBackground() ? "on"
                                                                  : "off",
        calendar_config::runtime::timezone(),
        calendar_config::runtime::locationName(),
        calendar_config::runtime::temperatureUnit() ==
                config::TemperatureUnit::Celsius
            ? "C"
            : "F",
        calendar_config::runtime::windSpeedUnit() ==
                config::WindSpeedUnit::KilometresPerHour
            ? "km/h"
            : "mph");
  }
  if (calendar_logic::frameComponentChanged(changes,
                                             FrameComponentChange::Date)) {
    char date[16] = "unavailable";
    struct tm localDate = {};
    if (localtime_r(&now, &localDate) != nullptr) {
      strftime(date, sizeof(date), "%Y-%m-%d", &localDate);
    }
    LOG.printf("[display] changed: local date (%s)\n", date);
  }
  if (calendar_logic::frameComponentChanged(
          changes, FrameComponentChange::IndoorClimate)) {
    const bool currentClimateValid =
        sensorReadings.climateValid &&
        isfinite(sensorReadings.temperatureC) &&
        isfinite(sensorReadings.humidityPct);
    if (currentClimateValid) {
      if (calendar_logic::hasValidIndoorClimate(previousComponents)) {
        LOG.printf(
            "[display] changed: indoor climate (temperature=%.1fC, "
            "previous=%.1fC, delta=%+.1fC; humidity=%.1f%%, previous=%.1f%%, "
            "delta=%+.1f%%)\n",
            sensorReadings.temperatureC,
            previousComponents.indoorTemperatureC,
            sensorReadings.temperatureC -
                previousComponents.indoorTemperatureC,
            sensorReadings.humidityPct,
            previousComponents.indoorHumidityPct,
            sensorReadings.humidityPct -
                previousComponents.indoorHumidityPct);
      } else {
        LOG.printf(
            "[display] changed: indoor climate (now available: %.1fC, %.1f%%)"
            "\n",
            sensorReadings.temperatureC, sensorReadings.humidityPct);
      }
    } else {
      LOG.println("[display] changed: indoor climate (unavailable)");
    }
  }
  if (calendar_logic::frameComponentChanged(changes,
                                             FrameComponentChange::Power)) {
    const bool batteryChanged = calendar_logic::batteryPercentageChanged(
        previousComponents, currentComponents);
    const bool sourceChanged = calendar_logic::externalPowerChanged(
        previousComponents, currentComponents);
    if (sourceChanged) {
      const char* previousSource =
          !previousComponents.externalPowerValid
              ? "unknown"
              : (previousComponents.externalPower ? "plugged in"
                                                  : "on battery");
      const char* currentSource =
          !currentComponents.externalPowerValid
              ? "unknown"
              : (currentComponents.externalPower ? "plugged in"
                                                 : "on battery");
      LOG.printf("[display] changed: power source (%s -> %s)\n",
                 previousSource, currentSource);
    }
    if (batteryChanged) {
      const String previousBattery =
          calendar_logic::hasValidBattery(previousComponents)
              ? String(previousComponents.batteryPct) + "%"
              : "unavailable";
      const String currentBattery =
          calendar_logic::hasValidBattery(currentComponents)
              ? String(currentComponents.batteryPct) + "%"
              : "unavailable";
      if (calendar_logic::hasValidBattery(previousComponents) &&
          calendar_logic::hasValidBattery(currentComponents)) {
        LOG.printf(
            "[display] changed: battery percentage (%s -> %s, delta=%+d%%)\n",
            previousBattery.c_str(), currentBattery.c_str(),
            currentComponents.batteryPct - previousComponents.batteryPct);
      } else {
        LOG.printf("[display] changed: battery percentage (%s -> %s)\n",
                   previousBattery.c_str(), currentBattery.c_str());
      }
    }
  }
  if (calendar_logic::frameComponentChanged(changes,
                                             FrameComponentChange::Weather)) {
    if (weather.valid) {
      const String temperature = roundedMeasurement(weather.temperatureC, "C");
      const String low = roundedMeasurement(weather.days[0].minimumC, "C");
      const String high = roundedMeasurement(weather.days[0].maximumC, "C");
      const String wind = roundedMeasurement(weather.windKmh, "km/h");
      LOG.printf(
          "[display] changed: weather (code=%d, day=%s, temperature=%s, "
          "low=%s, high=%s, wind=%s, alert=%s)\n",
          weather.weatherCode, weather.isDay ? "yes" : "no",
          temperature.c_str(), low.c_str(), high.c_str(), wind.c_str(),
          weather.alertTitle.isEmpty() ? "none" : weather.alertTitle.c_str());
    } else {
      LOG.println("[display] changed: weather (unavailable)");
    }
  }
  if (changes == 0) {
    LOG.println(
        "[display] changed: combined render fingerprint; component hashes "
        "matched");
  }
}

uint64_t statusFingerprint(const String& title, const String& detail,
                           const String& footer) {
  calendar_logic::Fingerprint hash;
  hash.add(std::string("status"));
  hash.add(std::string(title.c_str()));
  hash.add(std::string(detail.c_str()));
  hash.add(std::string(footer.c_str()));
  hash.add(std::string(board::FIRMWARE_VERSION));
  return hash.value();
}

bool fetchConfiguredCalendar(
    const calendar::Window& window, calendar::Data& data,
    String& failure, bool bypassHttpCache) {
  return calendar_config::runtime::calendarProvider() ==
                 config::CalendarProvider::Google
             ? calendar_provider::fetchGoogle(
                   window, data, failure, bypassHttpCache)
             : calendar_provider::fetchIcal(
                   window, data, failure, bypassHttpCache);
}

#if RETERMINAL_MODEL == 1005
config::CalendarView viewForNavigationIndex(int index) {
  switch (index) {
    case 1:
      return config::CalendarView::Week;
    case 2:
      return config::CalendarView::Month;
    default:
      return config::CalendarView::Today;
  }
}

void initializeAwakeButtons() {
  const uint32_t now = millis();
  for (AwakeButtonState& button : awakeButtons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableLevel = digitalRead(button.pin);
    button.sampledLevel = button.stableLevel;
    button.changedAtMs = now;
    button.armed = button.stableLevel == HIGH;
  }
}

bool pollAwakeButton(calendar_logic::CalendarNavigation& navigation,
                     const char*& name) {
  const uint32_t now = millis();
  for (AwakeButtonState& button : awakeButtons) {
    const int level = digitalRead(button.pin);
    if (level != button.sampledLevel) {
      button.sampledLevel = level;
      button.changedAtMs = now;
    }
    if (level == button.stableLevel ||
        now - button.changedAtMs < config::BUTTON_RELEASE_DEBOUNCE_MS) {
      continue;
    }

    button.stableLevel = level;
    if (level == HIGH) {
      button.armed = true;
      continue;
    }
    if (!button.armed) continue;

    button.armed = false;
    navigation = button.navigation;
    name = button.name;
    return true;
  }
  return false;
}

bool fetchInteractiveCalendarData(
    config::CalendarView view, time_t displayDay,
    calendar::Data& data, calendar::Window& availableWindow) {
  const calendar::Window required = calendar_logic::visibleDataWindow(
      view, displayDay, calendar_config::runtime::weekStart());
  const bool bufferedRangeContainsVisible =
      calendar_logic::containsWindow(availableWindow, required);
  if (bufferedRangeContainsVisible && !data.truncated) return true;

  calendar::Window requested =
      bufferedRangeContainsVisible
          ? required
          : calendar_logic::interactiveDataWindow(
                view, displayDay, calendar_config::runtime::weekStart());
  LOG.printf("[buttons] loading calendar data for %s navigation\n",
             calendar_logic::calendarViewName(view));

  String networkFailure;
  const bool connected =
      wifi_sta::connectStation(
          calendar_wifi::ssid(), calendar_wifi::password(),
          config::WIFI_TIMEOUT_MS, &networkFailure)
          .connected;
  if (!connected) {
    wifi_sta::disable();
    LOG.printf("[buttons] navigation unavailable: %s\n",
               networkFailure.c_str());
    hardware::beep();
    return false;
  }
  if (!WiFi.setSleep(false)) {
    LOG.println(
        "[buttons] warning: could not disable modem sleep for navigation");
  }

  calendar::Data updated;
  String calendarFailure;
  const bool fetched = fetchConfiguredCalendar(
      requested, updated, calendarFailure, true);
  if (fetched && updated.truncated &&
      (requested.start != required.start ||
       requested.end != required.end)) {
    LOG.println(
        "[buttons] buffered calendar was limited; retrying the visible period");
    calendar::Data visibleData;
    String visibleFailure;
    if (fetchConfiguredCalendar(
            required, visibleData, visibleFailure, true)) {
      updated = std::move(visibleData);
      requested = required;
    } else {
      LOG.printf(
          "[buttons] visible-period retry failed; using limited buffer: %s\n",
          visibleFailure.c_str());
    }
  }
  wifi_sta::disable();
  if (!fetched) {
    LOG.printf("[buttons] navigation calendar fetch failed: %s\n",
               calendarFailure.c_str());
    hardware::beep();
    return false;
  }

  data = std::move(updated);
  availableWindow = requested;
  return true;
}

bool fastRefreshRegion(const E1005FastRefresh::Region& region,
                       const char* reason) {
  if (!fastRefresh.ready()) return false;

  E1005FastRefresh::Timing timing;
  const E1005FastRefresh::Result result =
      fastRefresh.refresh(region, timing);
  if (result == E1005FastRefresh::Result::Ok) {
    LOG.printf(
        "[touch] %s refresh=%lu ms "
        "(prepare=%lu transfer=%lu panel=%lu reseed=%lu ms)\n",
        reason, static_cast<unsigned long>(timing.totalUs / 1000U),
        static_cast<unsigned long>(timing.prepareUs / 1000U),
        static_cast<unsigned long>(timing.transferUs / 1000U),
        static_cast<unsigned long>(timing.panelUs / 1000U),
        static_cast<unsigned long>(timing.reseedUs / 1000U));
    return true;
  }

  LOG.printf("[touch] %s fast refresh failed: %s\n", reason,
             E1005FastRefresh::resultMessage(result));
  return false;
}

void refreshInteractivePage() {
  const E1005FastRefresh::Region fullPanel = {
      0, 0, config::PANEL_WIDTH, config::PANEL_HEIGHT};
  if (fastRefreshRegion(fullPanel, "page")) return;

  fastRefresh.end();
  epaper.sleep();
  LOG.println("[touch] recovering page transition with a full refresh");
  refreshPanel();
  const E1005FastRefresh::Result result = fastRefresh.begin();
  if (result != E1005FastRefresh::Result::Ok) {
    LOG.printf("[touch] fast refresh unavailable after full refresh: %s\n",
               E1005FastRefresh::resultMessage(result));
  }
}

void renderInteractiveCalendar(
    const calendar::Data& data, const calendar::Window& window,
    config::CalendarView view, time_t now, time_t displayDay,
    const WeatherData& weather, const String& footer) {
  calendar_render::calendar(
      epaper, data, window, view, calendar_config::runtime::weekStart(), now,
      displayDay, sensorReadings, weather, footer);
  framebufferReady = true;

  refreshInteractivePage();

  const CalendarFrameFingerprints frame =
      frameFingerprints(data, window, view, weather, footer, now, displayDay);
  if (!saveFrameState(frame.combined, FrameKind::Calendar,
                      &frame.components)) {
    LOG.println("[touch] warning: selected page state was not saved");
  }
  retainedCalendarView = static_cast<uint8_t>(view);
  retainedSelectedDay = displayDay;
}

bool applyInteractiveSelection(
    calendar::Data& data, calendar::Window& window,
    config::CalendarView& view, time_t now, time_t& displayDay,
    const WeatherData& weather, const String& footer,
    const calendar_logic::CalendarSelection& selection,
    const char* inputName) {
  if (selection.view == view && selection.day == displayDay) return true;
  if (!fetchInteractiveCalendarData(
          selection.view, selection.day, data, window)) {
    return false;
  }

  LOG.printf("[input] %s -> %s\n", inputName,
             calendar_logic::calendarViewName(selection.view));
  view = selection.view;
  displayDay = selection.day;
  renderInteractiveCalendar(
      data, window, view, now, displayDay, weather, footer);
  return true;
}

void renderTouchSleepMessage() {
  calendar_render::sleepStatus(epaper);
  const E1005FastRefresh::Region navigation = {
      0, calendar_portrait_layout::NAVIGATION_TOP, config::PANEL_WIDTH,
      calendar_portrait_layout::NAVIGATION_HEIGHT};
  if (!fastRefreshRegion(navigation, "sleep message")) {
    fastRefresh.end();
    epaper.sleep();
    refreshPanel();
  }
  if (!saveFrameState(
          statusFingerprint(
              "Sleeping",
              "Press OK, UP, or DOWN to wake",
              ""),
          FrameKind::SleepingCalendar)) {
    LOG.println("[touch] warning: sleep-message state was not saved");
  }
}

void runTouchSession(
    calendar::Data& data, calendar::Window& window,
    config::CalendarView& view, time_t now, time_t& displayDay,
    const WeatherData& weather, const String& footer,
    bool framebufferMatchesPanel) {
  if (!framebufferReady) {
    beginPanel();
    initializePanelColorMode();
    calendar_render::calendar(
        epaper, data, window, view, calendar_config::runtime::weekStart(), now,
        displayDay, sensorReadings, weather, footer);
    framebufferReady = true;
  }
  if (!framebufferMatchesPanel) {
    LOG.println(
        "[touch] refreshing current page to establish the fast-refresh "
        "baseline");
    refreshPanel();
    const CalendarFrameFingerprints frame = frameFingerprints(
        data, window, view, weather, footer, now, displayDay);
    if (!saveFrameState(frame.combined, FrameKind::Calendar,
                        &frame.components)) {
      LOG.println("[touch] warning: refreshed baseline state was not saved");
    }
  }

  const E1005FastRefresh::Result refreshResult = fastRefresh.begin();
  if (refreshResult != E1005FastRefresh::Result::Ok) {
    LOG.printf("[touch] fast refresh unavailable: %s; using full refreshes\n",
               E1005FastRefresh::resultMessage(refreshResult));
  }
  const bool touchReady = touch.begin(touchWire);
  if (!touchReady) {
    LOG.println(
        "[touch] initialization failed; front buttons remain available");
  }
  initializeAwakeButtons();

  LOG.printf("[input] %s; sleeping after %lu seconds without input\n",
             touchReady ? "touch and buttons ready" : "buttons ready",
             static_cast<unsigned long>(
                 config::TOUCH_INACTIVITY_SLEEP_MS / 1000U));
  uint32_t lastActivityAt = millis();
  bool touchActive = false;
  while (static_cast<uint32_t>(millis() - lastActivityAt) <
         config::TOUCH_INACTIVITY_SLEEP_MS) {
    usbScreenCapture.poll(epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT);

    if (touchReady) {
      Gt911Touch::Point point = {};
      const Gt911Touch::PollResult touchResult = touch.poll(point);
      if (touchResult == Gt911Touch::PollResult::Release) {
        touchActive = false;
        lastActivityAt = millis();
      } else if (touchResult == Gt911Touch::PollResult::Touch) {
        lastActivityAt = millis();
        if (!touchActive) {
          touchActive = true;
          const time_t inputNow = time(nullptr);
          calendar_logic::CalendarSelection selection{
              view, displayDay};

          const int navigationIndex =
              calendar_portrait_layout::navigationIndexAt(point.x, point.y);
          if (navigationIndex >= 0) {
            selection.view = viewForNavigationIndex(navigationIndex);
            selection.day = calendar_logic::localMidnight(inputNow);
          } else if (view == config::CalendarView::Week) {
            const int dayIndex =
                calendar_portrait_layout::weekDayIndexAt(point.x, point.y);
            if (dayIndex >= 0) {
              selection.view = config::CalendarView::Today;
              selection.day = calendar_logic::addLocalDays(
                  calendar_logic::startOfWeek(
                      displayDay, calendar_config::runtime::weekStart()),
                  dayIndex);
            }
          } else if (view == config::CalendarView::Month) {
            const int dayIndex =
                calendar_portrait_layout::monthDayIndexAt(point.x, point.y);
            if (dayIndex >= 0) {
              selection.view = config::CalendarView::Today;
              selection.day = calendar_logic::addLocalDays(
                  calendar_logic::displayWindow(
                      config::CalendarView::Month, displayDay,
                      calendar_config::runtime::weekStart())
                      .start,
                  dayIndex);
            }
          }

          applyInteractiveSelection(
              data, window, view, inputNow, displayDay, weather, footer,
              selection, "touch");
        }
      }
    }

    calendar_logic::CalendarNavigation buttonNavigation;
    const char* buttonName = nullptr;
    if (pollAwakeButton(buttonNavigation, buttonName)) {
      lastActivityAt = millis();
      const time_t inputNow = time(nullptr);
      const calendar_logic::CalendarSelection selection =
          calendar_logic::navigateCalendar(
              view, displayDay, inputNow, buttonNavigation);
      applyInteractiveSelection(
          data, window, view, inputNow, displayDay, weather, footer,
          selection, buttonName);
    }
    delay(20);
  }

  LOG.println("[touch] inactivity timeout; entering deep sleep");
  renderTouchSleepMessage();
  touch.end();
  fastRefresh.end();
}
#endif

void applyRuntimeTimeSettings() {
  local_time::configureTimezone(calendar_config::runtime::timezone());
  quiet_hours::configure({
      calendar_config::runtime::quietHoursEnabled(),
      calendar_config::runtime::quietStartHour(),
      calendar_config::runtime::quietStartMinute(),
      calendar_config::runtime::quietEndHour(),
      calendar_config::runtime::quietEndMinute(),
  });
}

void renderPortal() {
  invalidateFrameState();
  rtc_sync::restoreSystemClock();
  beginPanel();
  initializePanelColorMode();

#if RETERMINAL_MODEL == 1001
  const GFXfont* titleFont = calendar_latin_font::uiFont(36);
  const GFXfont* subtitleFont = calendar_latin_font::uiFont(24);
  const GFXfont* captionFont = calendar_latin_font::uiFont(18);
  const GFXfont* detailFont = calendar_latin_font::uiFont(18);
#elif RETERMINAL_MODEL == 1003
  const GFXfont* titleFont = calendar_latin_font::uiFont(48);
  const GFXfont* subtitleFont = calendar_latin_font::uiFont(36);
  const GFXfont* captionFont = calendar_latin_font::uiFont(24);
  const GFXfont* detailFont = calendar_latin_font::uiFont(24);
#elif RETERMINAL_MODEL == 1004
  const GFXfont* titleFont = calendar_latin_font::uiFont(48);
  const GFXfont* subtitleFont = calendar_latin_font::uiFont(36);
  const GFXfont* captionFont = calendar_latin_font::uiFont(24);
  const GFXfont* detailFont = calendar_latin_font::uiFont(24);
#elif RETERMINAL_MODEL == 1005
  const GFXfont* titleFont = calendar_latin_font::uiFont(36);
  const GFXfont* subtitleFont = calendar_latin_font::uiFont(18);
  const GFXfont* captionFont = calendar_latin_font::uiFont(16);
  const GFXfont* detailFont = calendar_latin_font::uiFont(16);
#else
  const GFXfont* titleFont = calendar_latin_font::uiFont(36);
  const GFXfont* subtitleFont = calendar_latin_font::uiFont(24);
  const GFXfont* captionFont = calendar_latin_font::uiFont(18);
  const GFXfont* detailFont = calendar_latin_font::uiFont(18);
#endif

  config_portal::Config portal;
  portal.wifiSchema = &config_portal::kWifiSchema;
  portal.appSchema = &calendar_config::kSchema;
  portal.appName = "calendar viewer";
  portal.repositoryUrl = repo_qr::kUrl;
  portal.firmwareVersion = board::FIRMWARE_VERSION;
  portal.useAutoApPassword = true;
  static const config_portal::NavTab kTabs[] = {
      {"Google IAM", "/google-credentials", "google"},
      {"SD", "/browse?path=%2F", "sd"},
  };
  portal.extraTabs = kTabs;
  portal.extraTabCount = sizeof(kTabs) / sizeof(kTabs[0]);
  portal.wifiFallback = [](const char* key) -> String {
    if (strcmp(key, "ssid") == 0) return String(calendar_wifi::ssid());
    if (strcmp(key, "password") == 0) return String(calendar_wifi::password());
    return "";
  };
  portal.onWifiPasswordSaved = calendar_wifi::recordPasswordOverride;
  portal.sdFormat = [](String& error) -> bool {
    sdReady =
        sd_card::formatCard(epaper.getSPIinstance(), config::SD_ROOT, error);
    return sdReady;
  };
  portal.sdFormatWarning = "uploaded files and any existing screenshots";

  if (!config_portal::begin(portal)) {
    calendar_render::status(epaper, "Configuration unavailable",
                            "Could not start the settings access point.");
    refreshPanel();
    powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    return;
  }
  if (WebServer* server = config_portal::webServer()) {
    google_credentials_portal::attachRoutes(*server, portal);
    sd_web_portal::Config sdPortal;
    static String sdHeaderHtml;
    sdHeaderHtml = config_portal::renderHeaderHtml(portal, "sd");
    sdPortal.headerHtml = sdHeaderHtml.c_str();
    sdPortal.sdFormatEndpoint = "/format-sd.json";
    sdPortal.sdFormatWarning = portal.sdFormatWarning;
    sd_web_portal::attachRoutes(*server, sdPortal);
  }

  config_portal::ui::RenderInfo info;
  info.modelLabel = MODEL_NAME;
  info.title = "Configure calendar";
#if RETERMINAL_MODEL == 1005
  info.tagline = "Join AP for Wi-Fi, calendar, weather";
#else
  info.tagline = "Join the AP to set Wi-Fi, calendar, and weather";
#endif
  info.ssid = config_portal::currentSsid();
  info.wifiPassword = config_portal::currentApPassword();
  info.url = String("http://") + config_portal::currentIp().toString();
  info.macAddress = wifi_sta::stationMacAddress();
  info.firmwareVersion = board::FIRMWARE_VERSION;
  info.wifiPayload = config_portal::wifiQrPayload(
      info.ssid,
      info.wifiPassword.length() ? info.wifiPassword.c_str() : nullptr);
  info.urlPayload = config_portal::urlQrPayload(
      config_portal::currentIp(), config_portal::currentPort(), "/wifi");
  info.footerHint = PORTAL_EXIT_HINT;
  info.fonts.titleFont = titleFont;
  info.fonts.subtitleFont = subtitleFont;
  info.fonts.captionFont = captionFont;
  info.fonts.detailFont = detailFont;
  config_portal::ui::renderPortalScreen<EPaper>(
      epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT, PANEL_BLACK,
      PANEL_WHITE, info);
  refreshPanel();
  sdReady = sd_card::mount(epaper.getSPIinstance(), config::SD_ROOT);
  if (!sdReady) {
    LOG.println("[portal] SD mount failed; browser tab will be empty");
  }
  panel_watchdog::disarmCurrentTask();

  bool exitButtonArmed = digitalRead(PIN_BUTTON_GREEN);
  uint32_t buttonLowSince = 0;
  while (!config_portal::rebootRequested() &&
         !sd_web_portal::exitRequested()) {
    config_portal::loop();
    usbScreenCapture.poll(epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT);
    const uint32_t now = millis();
    if (digitalRead(PIN_BUTTON_GREEN)) {
      exitButtonArmed = true;
      buttonLowSince = 0;
    } else if (exitButtonArmed) {
      if (buttonLowSince == 0) buttonLowSince = now;
      if (now - buttonLowSince >= 50) {
        hardware::beep();
        break;
      }
    }
    delay(5);
  }
  if (sd_web_portal::exitRequested()) {
    const uint32_t drainStart = millis();
    while (millis() - drainStart < 400) {
      config_portal::loop();
      delay(10);
    }
  }
  sd_web_portal::end();
  config_portal::end();
  if (sdReady) {
    SD.end();
    sdReady = false;
  }
  delay(200);
  ESP.restart();
}

calendar_logic::PrimaryButtonAction primaryGesture(bool pressedAtBoot) {
  using calendar_logic::PrimaryButtonAction;
  if (!pressedAtBoot) return PrimaryButtonAction::None;
  hardware::beep();
  const uint32_t started = millis();
  const uint32_t decisionMs =
      config::GREEN_SCREENSHOT_ENABLED
          ? calendar_logic::kScreenshotHoldMs
          : calendar_logic::kConfigPortalHoldMs;
  bool released = false;
  uint32_t heldMs = decisionMs;
  while (millis() - started < decisionMs) {
    if (digitalRead(PIN_BUTTON_GREEN) != LOW) {
      heldMs = millis() - started;
      released = true;
      break;
    }
    delay(10);
  }

  const PrimaryButtonAction action =
      calendar_logic::classifyPrimaryButtonHold(
          heldMs, config::GREEN_SCREENSHOT_ENABLED);
  if (action == PrimaryButtonAction::Refresh) {
    LOG.printf("[gesture] %s released after %u ms; refreshing\n",
               PRIMARY_BUTTON_LABEL, static_cast<unsigned>(heldMs));
    return action;
  }

  hardware::beep();
  if (action == PrimaryButtonAction::Portal) {
    LOG.printf(
        "[gesture] %s held for %u ms; entering configuration portal\n",
        PRIMARY_BUTTON_LABEL, static_cast<unsigned>(heldMs));
    return action;
  }

  LOG.printf("[gesture] %s held for %u ms; capturing screenshot\n",
             PRIMARY_BUTTON_LABEL, static_cast<unsigned>(heldMs));
  if (!released) {
    const uint32_t releaseWaitStarted = millis();
    uint32_t releaseStableSince = 0;
    while (millis() - releaseWaitStarted < 3000) {
      if (digitalRead(PIN_BUTTON_GREEN) == LOW) {
        releaseStableSince = 0;
      } else if (releaseStableSince == 0) {
        releaseStableSince = millis();
      } else if (millis() - releaseStableSince >=
                 config::BUTTON_RELEASE_DEBOUNCE_MS) {
        break;
      }
      delay(5);
    }
  }
  return action;
}

String portalHint() {
  if (config::GREEN_SCREENSHOT_ENABLED) {
    return String("Press ") + PRIMARY_BUTTON_LABEL +
           " to retry; hold 2s to configure or 5s for screenshot";
  }
  return String("Press ") + PRIMARY_BUTTON_LABEL + " to retry or hold " +
         PRIMARY_BUTTON_LABEL + " for 2s to configure";
}

String googleFailureForDisplay(String failure) {
  failure.replace("; event-only fallback failed:",
                  "\nEvent-only fallback failed:");
  return failure;
}

uint64_t scheduledSleepSeconds(const struct tm& localNow) {
  uint64_t sleepSeconds = calendar_config::runtime::sleepSeconds();
  if (quiet_hours::nextWakeFallsInside(localNow, sleepSeconds)) {
    sleepSeconds = quiet_hours::secondsUntilEnd(localNow);
  }
  return sleepSeconds;
}

void showStatusAndSleep(const String& title, const String& detail,
                        uint64_t sleepSeconds) {
  const String footer = portalHint();
  const uint64_t nextHash = statusFingerprint(title, detail, footer);
  uint64_t previousHash = 0;
  FrameKind previousKind = FrameKind::None;
  const bool changed =
      !loadFrameState(previousHash, previousKind) ||
      previousKind != FrameKind::Status || previousHash != nextHash;
  if (changed || screenshotRequested) {
    beginPanel();
    initializePanelColorMode();
    calendar_render::status(epaper, title, detail, footer);
    framebufferReady = true;
    saveRequestedScreenshot();
    if (changed) {
      refreshPanel();
      if (!saveFrameState(nextHash, FrameKind::Status)) {
        LOG.println("[display] warning: status fingerprint was not saved");
      }
    }
  }
  powerDownAndSleep(sleepSeconds);
}

}  // namespace

void setup() {
  power_latch::holdOn();
  hardware::setStatusLed(true);
  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  usbScreenCapture.begin(Serial1);

  calendar_config::runtime::load();
  calendar_wifi::load();
  applyRuntimeTimeSettings();

  const esp_sleep_wakeup_cause_t wakeCause =
      esp_sleep_get_wakeup_cause();
  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  const bool buttonWake = wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  if (coldBoot) {
    invalidateFrameState();
#if RETERMINAL_MODEL == 1005
    retainedCalendarView =
        static_cast<uint8_t>(config::CalendarView::Today);
    retainedSelectedDay = 0;
#endif
  }
  const uint64_t wakePins =
      buttonWake ? esp_sleep_get_ext1_wakeup_status() : 0;

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const bool greenWake =
      (wakePins & (1ULL << PIN_BUTTON_GREEN)) != 0 ||
      (buttonWake && !digitalRead(PIN_BUTTON_GREEN));
  const bool rightWake =
      (wakePins & (1ULL << PIN_BUTTON_RIGHT)) != 0 ||
      (buttonWake && !digitalRead(PIN_BUTTON_RIGHT));
  const bool leftWake =
      (wakePins & (1ULL << PIN_BUTTON_LEFT)) != 0 ||
      (buttonWake && !digitalRead(PIN_BUTTON_LEFT));
  const bool greenPressed =
      greenWake || (coldBoot && !digitalRead(PIN_BUTTON_GREEN));
  const bool rightPressed =
      rightWake || (coldBoot && !digitalRead(PIN_BUTTON_RIGHT));
  const bool leftPressed =
      leftWake || (coldBoot && !digitalRead(PIN_BUTTON_LEFT));

  calendar_logic::PrimaryButtonAction gesture = primaryGesture(greenPressed);
  screenshotRequested =
      gesture == calendar_logic::PrimaryButtonAction::Screenshot;
  const bool noWifi = !calendar_wifi::haveCredentials();
  const config::CalendarProvider provider =
      calendar_config::runtime::calendarProvider();
  const bool googleCredentialsConfigured =
      provider == config::CalendarProvider::Google &&
      google_credentials::configured();
  const bool noCalendarProvider =
      !calendar_logic::hasConfiguredCalendarProvider(
          provider, calendar_config::runtime::icalUrl(),
          googleCredentialsConfigured);
  const bool portalRequested =
      gesture == calendar_logic::PrimaryButtonAction::Portal ||
      (coldBoot && (noWifi || noCalendarProvider));
  const bool showInitialConnectionStatus =
      calendar_logic::shouldShowInitialConnectionStatus(
          coldBoot, portalRequested);
  if (portalRequested) {
    screenshotRequested = false;
    renderPortal();
    return;
  }

#if RETERMINAL_MODEL == 1005
  const bool todaySelected =
      greenPressed &&
      gesture == calendar_logic::PrimaryButtonAction::Refresh;
#else
  static_cast<void>(rightPressed);
  static_cast<void>(leftPressed);
#endif

  if (app_logic::startupBeepRequired(coldBoot, buttonWake) &&
      gesture == calendar_logic::PrimaryButtonAction::None) {
    hardware::beep();
  }
  if (screenshotRequested) {
    LOG.printf("[screenshot] %s-button hold requested PNG export\n",
               PRIMARY_BUTTON_LABEL);
  }
  LOG.printf("[boot] Calendar Viewer %s / %s / fw %s\n",
             MODEL_NAME, COLOR_MODE_NAME, board::FIRMWARE_VERSION);

  rtc_sync::restoreSystemClock();
  struct tm localNow = {};
  const bool ntpDue =
      config::DEBUG_FORCE_NTP ||
      local_time::refreshDue(coldBoot, lastNtpSyncEpoch,
                             config::NTP_REFRESH_SECONDS);
  if (!buttonWake && !ntpDue && local_time::localClock(localNow) &&
      quiet_hours::active(localNow)) {
    powerDownAndSleep(quiet_hours::secondsUntilEnd(localNow));
    return;
  }

  sensors::readAll(PIN_BATTERY_ENABLE, PIN_BATTERY_ADC, sht4,
                   config::SENSOR_READ_ATTEMPTS,
                   config::SENSOR_RETRY_DELAY_MS, sensorReadings);
  if (low_battery::shouldWarn(calendar_config::runtime::lowBatteryWarn(),
                              sensorReadings.batteryValid,
                              sensorReadings.externalPower,
                              sensorReadings.batteryPct)) {
    showStatusAndSleep("Please recharge",
                       "Plug in USB-C, then press a button to retry.",
                       calendar_config::runtime::sleepSeconds());
    return;
  }

  if (showInitialConnectionStatus) {
    const String stationMac = wifi_sta::stationMacAddress();
    const String deviceInfo =
#if RETERMINAL_MODEL == 1005
        String("MAC ") + stationMac + "  FW " + board::FIRMWARE_VERSION;
#else
        String("MAC: ") + stationMac +
        "  Firmware: " + board::FIRMWARE_VERSION;
#endif
    const String connectionDetail =
        ntpDue ? "Synchronizing clock, calendar, and weather"
               : "Refreshing calendar and weather";
    LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());
    LOG.println("[display] showing Wi-Fi connection status");
    beginPanel();
    calendar_render::connectionStatus(
        epaper, "Connecting to " + String(calendar_wifi::ssid()),
        connectionDetail, deviceInfo,
        String("From sleep, hold ") + PRIMARY_BUTTON_LABEL +
            " for 2 seconds to configure");
    refreshPanel();
  }

  String networkFailure;
  const bool connected =
      wifi_sta::connectStation(calendar_wifi::ssid(), calendar_wifi::password(),
                               config::WIFI_TIMEOUT_MS, &networkFailure)
          .connected;
  if (!connected) {
    uint64_t previousHash = 0;
    FrameKind previousKind = FrameKind::None;
    if (loadFrameState(previousHash, previousKind) &&
        isPreservableCalendarFrame(previousKind)) {
      LOG.printf("[wifi] %s; preserving existing panel\n",
                 networkFailure.c_str());
      powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    } else {
      showStatusAndSleep("Wi-Fi unavailable", networkFailure,
                         config::FAILURE_RETRY_SECONDS);
    }
    return;
  }
  if (!WiFi.setSleep(false)) {
    LOG.println(
        "[wifi] warning: could not disable modem sleep for calendar refresh");
  } else {
    LOG.println("[wifi] modem sleep disabled for calendar refresh");
  }

  if (ntpDue) {
    ntp::synchronizeAndPersist(
        calendar_config::runtime::timezone(),
        calendar_config::runtime::ntpPrimary(),
        calendar_config::runtime::ntpSecondary(),
        config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS,
        &lastNtpSyncEpoch);
    applyRuntimeTimeSettings();
  }
  if (!local_time::clockIsValid()) {
    wifi_sta::disable();
    uint64_t previousHash = 0;
    FrameKind previousKind = FrameKind::None;
    if (loadFrameState(previousHash, previousKind) &&
        isPreservableCalendarFrame(previousKind)) {
      LOG.println("[time] clock unavailable; preserving existing panel");
      powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    } else {
      showStatusAndSleep(
          "Clock unavailable",
          "NTP and the hardware clock could not provide a valid time.",
          config::FAILURE_RETRY_SECONDS);
    }
    return;
  }

  local_time::localClock(localNow);
  if (calendar_logic::suppressPostSyncForQuietHours(
          coldBoot, buttonWake, quiet_hours::active(localNow))) {
    wifi_sta::disable();
    powerDownAndSleep(quiet_hours::secondsUntilEnd(localNow));
    return;
  }

  const time_t now = time(nullptr);
  config::CalendarView activeCalendarView = config::CalendarView::Today;
  time_t displayDay = calendar_logic::localMidnight(now);
  calendar::Window window;
#if RETERMINAL_MODEL == 1005
  calendar_logic::CalendarNavigation wakeNavigation =
      calendar_logic::CalendarNavigation::None;
  if (todaySelected) {
    wakeNavigation = calendar_logic::CalendarNavigation::Today;
  } else if (rightPressed) {
    wakeNavigation = calendar_logic::CalendarNavigation::Previous;
  } else if (leftPressed) {
    wakeNavigation = calendar_logic::CalendarNavigation::Next;
  }
  const calendar_logic::CalendarSelection wakeSelection =
      calendar_logic::navigateCalendar(
          static_cast<config::CalendarView>(retainedCalendarView),
          retainedSelectedDay, now, wakeNavigation);
  activeCalendarView = wakeSelection.view;
  displayDay = wakeSelection.day;
  retainedCalendarView = static_cast<uint8_t>(activeCalendarView);
  retainedSelectedDay = displayDay;
  window = coldBoot || buttonWake
               ? calendar_logic::interactiveDataWindow(
                     activeCalendarView, displayDay,
                     calendar_config::runtime::weekStart())
               : calendar_logic::visibleDataWindow(
                     activeCalendarView, displayDay,
                     calendar_config::runtime::weekStart());
  LOG.printf(
      "[view] %s (OK=today, UP=previous, DOWN=next)\n",
      calendar_logic::calendarViewName(activeCalendarView));
#else
  window = calendar_logic::dashboardWindow(
      now, calendar_config::runtime::weekStart());
#endif
  calendar::Data calendarData;
  String calendarFailure;
  const bool calendarUpdated = fetchConfiguredCalendar(
      window, calendarData, calendarFailure, buttonWake);
#if RETERMINAL_MODEL == 1005
  if (calendarUpdated && calendarData.truncated) {
    const calendar::Window visibleWindow =
        calendar_logic::visibleDataWindow(
            activeCalendarView, displayDay,
            calendar_config::runtime::weekStart());
    if (window.start != visibleWindow.start ||
        window.end != visibleWindow.end) {
      LOG.println(
          "[calendar] buffered range was limited; retrying the visible period");
      calendar::Data visibleData;
      String visibleFailure;
      if (fetchConfiguredCalendar(
              visibleWindow, visibleData, visibleFailure, buttonWake)) {
        calendarData = std::move(visibleData);
        window = visibleWindow;
      } else {
        LOG.printf(
            "[calendar] visible-period retry failed; using limited buffer: %s\n",
            visibleFailure.c_str());
      }
    }
  }
#endif

  WeatherData weather;
  String weatherFailure;
  const bool weatherUpdated =
      weather_summary::fetch(weather, weatherFailure, buttonWake);
  if (weatherUpdated) {
    String cacheFailure;
    if (!weather_summary::saveCached(weather, cacheFailure)) {
      LOG.printf("[weather] cache save skipped: %s\n", cacheFailure.c_str());
    }
  } else {
    String cacheFailure;
    if (!weather_summary::loadCached(
            weather, config::WEATHER_CACHE_MAX_AGE_SECONDS, cacheFailure)) {
      LOG.printf("[weather] unavailable: %s; %s\n", weatherFailure.c_str(),
                 cacheFailure.c_str());
    } else {
      LOG.printf("[weather] live update failed: %s; using NVS cache\n",
                 weatherFailure.c_str());
    }
  }
  wifi_sta::disable();

  if (!calendarUpdated) {
    if (provider == config::CalendarProvider::Google) {
      LOG.printf("[google] update failed; displaying error: %s\n",
                 calendarFailure.c_str());
      showStatusAndSleep("Google Calendar error",
                         googleFailureForDisplay(calendarFailure),
                         scheduledSleepSeconds(localNow));
      return;
    }

    uint64_t previousHash = 0;
    FrameKind previousKind = FrameKind::None;
    if (loadFrameState(previousHash, previousKind) &&
        isPreservableCalendarFrame(previousKind)) {
      LOG.printf("[calendar] %s; preserving existing panel\n",
                 calendarFailure.c_str());
      powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    } else {
      showStatusAndSleep("Calendar unavailable", calendarFailure,
                         config::FAILURE_RETRY_SECONDS);
    }
    return;
  }

  String footer;
  if (calendar_config::runtime::debugShowStatusBadges()) {
    char checkedDate[16] = {};
    strftime(checkedDate, sizeof(checkedDate), "%e %b %Y", &localNow);
    String checked = checkedDate;
    checked.trim();
    checked += " ";
    checked += calendar_logic::formatClockTime(
                  now, calendar_config::runtime::timeFormat())
                  .c_str();
    footer = String(calendar_config::runtime::calendarProviderName()) +
             " checked " + checked;
    if (weather.fromCache) footer += " / cached weather";
  }
  const CalendarFrameFingerprints nextFrame =
      frameFingerprints(calendarData, window, activeCalendarView, weather,
                        footer, now, displayDay);
  uint64_t previousHash = 0;
  FrameKind previousKind = FrameKind::None;
  const bool havePreviousFrame =
      loadFrameState(previousHash, previousKind);
  calendar_logic::FrameComponents previousComponents;
  const bool havePreviousComponents =
      havePreviousFrame && previousKind == FrameKind::Calendar &&
      loadFrameComponents(previousComponents);
  const uint16_t componentChanges =
      havePreviousComponents
          ? calendar_logic::changedFrameComponents(previousComponents,
                                                  nextFrame.components)
          : 0;
  const bool changed = calendar_logic::shouldRefreshCalendarFrame(
      havePreviousFrame, previousKind == FrameKind::Calendar, previousHash,
      nextFrame.combined, componentChanges);
  if (changed || screenshotRequested) {
    if (changed) {
      logCalendarFrameChanges(
          havePreviousFrame, previousKind, havePreviousComponents,
          previousComponents, nextFrame.components, activeCalendarView,
          componentChanges, calendarData, weather, now);
    }
    beginPanel();
    initializePanelColorMode();
    calendar_render::calendar(
        epaper, calendarData, window, activeCalendarView,
        calendar_config::runtime::weekStart(), now, displayDay, sensorReadings,
        weather, footer);
    framebufferReady = true;
    const bool screenshotSaved = saveRequestedScreenshot();
    if (changed) {
      refreshPanel();
      if (!saveFrameState(nextFrame.combined, FrameKind::Calendar,
                          &nextFrame.components)) {
        LOG.println("[display] warning: frame fingerprint was not saved");
      }
      LOG.println("[display] refresh complete");
    } else {
      if (!havePreviousComponents &&
          !saveFrameState(nextFrame.combined, FrameKind::Calendar,
                          &nextFrame.components)) {
        LOG.println(
            "[display] warning: component fingerprints were not initialized");
      }
      LOG.println(screenshotSaved
                      ? "[display] exported screenshot without refreshing "
                        "unchanged panel"
                      : "[display] screenshot export failed; panel unchanged");
    }
  } else {
    if (!havePreviousComponents &&
        !saveFrameState(nextFrame.combined, FrameKind::Calendar,
                        &nextFrame.components)) {
      LOG.println(
          "[display] warning: component fingerprints were not initialized");
    }
    LOG.println(
        "[display] calendar, weather, power, and thresholded indoor climate "
        "unchanged; skipping panel refresh");
  }

#if RETERMINAL_MODEL == 1005
  if (coldBoot || buttonWake) {
    runTouchSession(calendarData, window, activeCalendarView, now, displayDay,
                    weather, footer, changed);
  }
#endif
  powerDownAndSleep(scheduledSleepSeconds(localNow));
}

void loop() {
  if (framebufferReady) {
    usbScreenCapture.poll(epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT);
  } else {
    usbScreenCapture.pollUnavailable();
  }
  delay(1000);
}
