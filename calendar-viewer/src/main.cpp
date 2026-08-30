#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <climits>
#include <math.h>
#include <time.h>

#include "app_logger.h"
#include "app_logic.h"
#include "board_pins.h"
#include "calendar_config_runtime.h"
#include "calendar_config_schema.h"
#include "calendar_logic.h"
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
#include "theme.h"
#include "timestamped_logger.h"
#include "usb_screen_capture.h"
#include "version.h"
#include "weather_app_logic.h"
#include "weather_data.h"
#include "weather_summary.h"
#include "wifi_schema.h"
#include "wifi_sta.h"

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
// Increment when rendering changes without changing the underlying data.
constexpr uint32_t kCalendarFrameRevision = 8;

enum class FrameKind : uint8_t {
  None = 0,
  Calendar = 1,
  Status = 2,
};

EPaper epaper;
usb_screen_capture::Server usbScreenCapture;
Adafruit_SHT4x sht4;
sensors::Readings sensorReadings;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
bool panelStarted = false;
bool framebufferReady = false;

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
  epaper_setup::begin(epaper);
  epaper.setRotation(config::PANEL_ROTATION);
#if RETERMINAL_MODEL == 1003
  epaper.setTemp(panelWaveformTemperatureC);
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

void refreshPanel() {
  panel_watchdog::refresh(epaper);
  framebufferReady = true;
}

void powerDownAndSleep(uint64_t sleepSeconds, bool timerWakeEnabled = true) {
  wifi_sta::disable();
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

bool saveFrameState(uint64_t hash, FrameKind kind) {
  Preferences prefs;
  if (!prefs.begin(calendar_config::kNamespace, false)) return false;
  prefs.putBool(kFrameValidKey, false);
  bool written =
      prefs.putULong64(kFrameHashKey, hash) > 0 &&
      prefs.putUChar(kFrameKindKey, static_cast<uint8_t>(kind)) > 0;
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

void addRoundedSensors(calendar_logic::Fingerprint& hash,
                       const sensors::Readings& readings) {
  hash.addValue(readings.climateValid);
  if (readings.climateValid) {
    const int temperature =
        static_cast<int>(lroundf(readings.temperatureC));
    const int humidity = static_cast<int>(lroundf(readings.humidityPct));
    hash.addValue(temperature);
    hash.addValue(humidity);
  }
  hash.addValue(readings.batteryValid);
  if (readings.batteryValid) {
    hash.addValue(readings.batteryPct);
  }
  hash.addValue(readings.externalPowerValid);
  if (readings.externalPowerValid) hash.addValue(readings.externalPower);
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

uint64_t frameFingerprint(const calendar::Data& data,
                          const calendar::Window& window,
                          const WeatherData& weather,
                          const String& footer, time_t now) {
  calendar_logic::Fingerprint hash;
  hash.addValue(kCalendarFrameRevision);
  const uint64_t calendarHash = calendar_logic::dataFingerprint(data, window);
  hash.addValue(calendarHash);
  hash.addValue(data.truncated);
  hash.addValue(calendar_config::runtime::weekStart());
  hash.addValue(calendar_config::runtime::timeFormat());
  hash.addValue(
      calendar_config::runtime::showSingleCalendarBackground());
  hash.add(std::string(calendar_config::runtime::timezone()));
  hash.add(std::string(calendar_config::runtime::locationName()));
  hash.addValue(calendar_config::runtime::temperatureUnit());
  hash.addValue(calendar_config::runtime::windSpeedUnit());
  addRoundedSensors(hash, sensorReadings);
  addRoundedWeather(hash, weather);
  hash.add(std::string(footer.c_str()));
  hash.add(std::string(board::FIRMWARE_VERSION));
  struct tm localDate = {};
  if (localtime_r(&now, &localDate) != nullptr) {
    hash.addValue(localDate.tm_year);
    hash.addValue(localDate.tm_yday);
  }
  return hash.value();
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
  const GFXfont* titleFont = &FreeSansBold18pt7b;
  const GFXfont* subtitleFont = &FreeSans12pt7b;
  const GFXfont* captionFont = &FreeSansBold9pt7b;
  const GFXfont* detailFont = &FreeSans9pt7b;
#elif RETERMINAL_MODEL == 1003
  const GFXfont* titleFont = &FreeSansBold24pt7b;
  const GFXfont* subtitleFont = &FreeSans18pt7b;
  const GFXfont* captionFont = &FreeSansBold12pt7b;
  const GFXfont* detailFont = &FreeSans12pt7b;
#elif RETERMINAL_MODEL == 1004
  const GFXfont* titleFont = &FreeSansBold24pt7b;
  const GFXfont* subtitleFont = &FreeSans18pt7b;
  const GFXfont* captionFont = &FreeSansBold12pt7b;
  const GFXfont* detailFont = &FreeSans12pt7b;
#else
  const GFXfont* titleFont = &FreeSansBold18pt7b;
  const GFXfont* subtitleFont = &FreeSans12pt7b;
  const GFXfont* captionFont = &FreeSansBold9pt7b;
  const GFXfont* detailFont = &FreeSans9pt7b;
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
  };
  portal.extraTabs = kTabs;
  portal.extraTabCount = sizeof(kTabs) / sizeof(kTabs[0]);
  portal.wifiFallback = [](const char* key) -> String {
    if (strcmp(key, "ssid") == 0) return String(calendar_wifi::ssid());
    if (strcmp(key, "password") == 0) return String(calendar_wifi::password());
    return "";
  };
  portal.onWifiPasswordSaved = calendar_wifi::recordPasswordOverride;

  if (!config_portal::begin(portal)) {
    calendar_render::status(epaper, "Configuration unavailable",
                            "Could not start the settings access point.");
    refreshPanel();
    powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    return;
  }
  if (WebServer* server = config_portal::webServer()) {
    google_credentials_portal::attachRoutes(*server, portal);
  }

  config_portal::ui::RenderInfo info;
  info.modelLabel = MODEL_NAME;
  info.title = "Configure calendar";
  info.tagline = "Join the AP to set Wi-Fi, calendar, and weather";
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
  panel_watchdog::disarmCurrentTask();

  bool exitButtonArmed = digitalRead(PIN_BUTTON_GREEN);
  uint32_t buttonLowSince = 0;
  while (!config_portal::rebootRequested()) {
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
  config_portal::end();
  delay(200);
  ESP.restart();
}

enum class PrimaryGesture {
  None,
  Refresh,
  Portal,
};

PrimaryGesture primaryGesture(bool pressedAtBoot) {
  if (!pressedAtBoot) return PrimaryGesture::None;
  hardware::beep();
  constexpr uint32_t kHoldMs = 2000;
  const uint32_t started = millis();
  while (digitalRead(PIN_BUTTON_GREEN) == LOW &&
         millis() - started < kHoldMs) {
    delay(10);
  }
  if (digitalRead(PIN_BUTTON_GREEN) == LOW) {
    hardware::beep();
    LOG.println("[gesture] green held for 2s; entering configuration portal");
    return PrimaryGesture::Portal;
  }
  LOG.println("[gesture] green released before 2s; refreshing");
  return PrimaryGesture::Refresh;
}

String portalHint() {
  return "Press green to retry or hold green for 2s to configure";
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
  if (!loadFrameState(previousHash, previousKind) ||
      previousKind != FrameKind::Status || previousHash != nextHash) {
    beginPanel();
    initializePanelColorMode();
    calendar_render::status(epaper, title, detail, footer);
    refreshPanel();
    if (!saveFrameState(nextHash, FrameKind::Status)) {
      LOG.println("[display] warning: status fingerprint was not saved");
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
  if (coldBoot) invalidateFrameState();
  const uint64_t wakePins =
      buttonWake ? esp_sleep_get_ext1_wakeup_status() : 0;

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const bool greenWake =
      (wakePins & (1ULL << PIN_BUTTON_GREEN)) != 0 ||
      (buttonWake && !digitalRead(PIN_BUTTON_GREEN));

  PrimaryGesture gesture = PrimaryGesture::None;
  gesture = primaryGesture(
      greenWake || (coldBoot && !digitalRead(PIN_BUTTON_GREEN)));
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
  if (gesture == PrimaryGesture::Portal ||
      (coldBoot && (noWifi || noCalendarProvider))) {
    renderPortal();
    return;
  }

  if (app_logic::startupBeepRequired(coldBoot, buttonWake) &&
      gesture == PrimaryGesture::None) {
    hardware::beep();
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

  String networkFailure;
  const bool connected =
      wifi_sta::connectStation(calendar_wifi::ssid(), calendar_wifi::password(),
                               config::WIFI_TIMEOUT_MS, &networkFailure)
          .connected;
  if (!connected) {
    uint64_t previousHash = 0;
    FrameKind previousKind = FrameKind::None;
    if (loadFrameState(previousHash, previousKind) &&
        previousKind == FrameKind::Calendar) {
      LOG.printf("[wifi] %s; preserving existing panel\n",
                 networkFailure.c_str());
      powerDownAndSleep(config::FAILURE_RETRY_SECONDS);
    } else {
      showStatusAndSleep("Wi-Fi unavailable", networkFailure,
                         config::FAILURE_RETRY_SECONDS);
    }
    return;
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
        previousKind == FrameKind::Calendar) {
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
  if (!buttonWake && quiet_hours::active(localNow)) {
    wifi_sta::disable();
    powerDownAndSleep(quiet_hours::secondsUntilEnd(localNow));
    return;
  }

  const time_t now = time(nullptr);
  const calendar::Window window = calendar_logic::dashboardWindow(
      now, calendar_config::runtime::weekStart());
  calendar::Data calendarData;
  String calendarFailure;
  const bool calendarUpdated =
      calendar_config::runtime::calendarProvider() ==
              config::CalendarProvider::Google
          ? calendar_provider::fetchGoogle(window, calendarData,
                                           calendarFailure, buttonWake)
          : calendar_provider::fetchIcal(window, calendarData,
                                         calendarFailure, buttonWake);

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
        previousKind == FrameKind::Calendar) {
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
  const uint64_t nextHash =
      frameFingerprint(calendarData, window, weather, footer, now);
  uint64_t previousHash = 0;
  FrameKind previousKind = FrameKind::None;
  const bool havePreviousFrame =
      loadFrameState(previousHash, previousKind);
  const bool changed = !havePreviousFrame ||
                       previousKind != FrameKind::Calendar ||
                       previousHash != nextHash;
  if (changed) {
    beginPanel();
    initializePanelColorMode();
    calendar_render::calendar(
        epaper, calendarData, window, calendar_config::runtime::weekStart(),
        now, sensorReadings, weather, footer);
    refreshPanel();
    if (!saveFrameState(nextHash, FrameKind::Calendar)) {
      LOG.println("[display] warning: frame fingerprint was not saved");
    }
    LOG.println("[display] refreshed because rendered content changed");
  } else {
    LOG.println("[display] calendar, weather, and rounded sensors unchanged; "
                "skipping panel refresh");
  }

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
