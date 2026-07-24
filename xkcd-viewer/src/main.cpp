#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <TFT_eSPI.h>
#include <driver/rtc_io.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp32-hal-psram.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "app_logic.h"
#include "dither.h"
#include "image_loader.h"
#include "pcf8563_utc.h"
#include "timestamped_logger.h"

// HTTPS, HTTPClient and the SD filesystem have a fairly deep combined call
// chain. E1003 exposed the default 8 KiB Arduino loop stack limit when the SD
// cache path added its download buffer to that stack.
SET_LOOP_TASK_STACK_SIZE(16U * 1024U);

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check src/driver.h"
#endif

namespace {

constexpr int PIN_SD_SCK = 7;
constexpr int PIN_SD_MISO = 8;
constexpr int PIN_SD_MOSI = 9;
constexpr int PIN_SD_CS = 14;
constexpr int PIN_SD_DETECT = 15;
#if RETERMINAL_MODEL == 1003
constexpr int PIN_SD_ENABLE = 39;
constexpr int PIN_BATTERY_ENABLE = 40;
#else
constexpr int PIN_SD_ENABLE = 16;
constexpr int PIN_BATTERY_ENABLE = 21;
#endif
constexpr int PIN_BUTTON_GREEN = 3;
constexpr int PIN_BUTTON_RIGHT = 4;
constexpr int PIN_BUTTON_LEFT = 5;
constexpr int PIN_BUZZER = 45;
constexpr int PIN_BATTERY_ADC = 1;
constexpr int PIN_I2C_SDA = 19;
constexpr int PIN_I2C_SCL = 20;
constexpr int PIN_LOG_RX = 44;
constexpr int PIN_LOG_TX = 43;

TimestampedLogger appLog(Serial1);
#define LOG appLog

#if RETERMINAL_MODEL == 1001
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_GRAY_2;
constexpr bool PANEL_STATUS_DITHERED = false;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_GRAY_2;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 0;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_GRAY_1;
constexpr DitherPalette PANEL_PALETTE = PAL_GRAY4;
constexpr char MODEL_NAME[] = "E1001";
constexpr char COLOR_MODE_NAME[] = "Gray4";
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
constexpr bool PANEL_STATUS_DITHERED = true;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 4;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_DARKGREY;
constexpr DitherPalette PANEL_PALETTE = PAL_E6;
constexpr char MODEL_NAME[] = "E1002";
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_GRAY_13;
constexpr bool PANEL_STATUS_DITHERED = false;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_GRAY_13;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 0;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_GRAY_5;
constexpr DitherPalette PANEL_PALETTE = PAL_GRAY16;
constexpr char MODEL_NAME[] = "E1003";
constexpr char COLOR_MODE_NAME[] = "Gray16";
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_STATUS_BACKGROUND = TFT_WHITE;
constexpr bool PANEL_STATUS_DITHERED = true;
constexpr uint32_t PANEL_STATUS_DITHER_COLOR = TFT_BLACK;
constexpr uint8_t PANEL_STATUS_DITHER_THRESHOLD = 4;
constexpr uint32_t PANEL_CACHE_STATS_COLOR = TFT_DARKGREY;
constexpr DitherPalette PANEL_PALETTE = PAL_E6;
constexpr char MODEL_NAME[] = "E1004";
constexpr char COLOR_MODE_NAME[] = "six-color";
#endif

EPaper epaper;
Adafruit_SHT4x sht4;

bool sdReady = false;
bool sdCacheWritable = true;
bool screenshotRequested = false;
bool climateValid = false;
bool i2cReady = false;
volatile bool ntpSyncCompleted = false;
float temperatureC = NAN;
float humidityPct = NAN;
float batteryVoltage = NAN;
int batteryPct = -1;
bool cacheStatsAvailable = false;
uint32_t cachedComicCountForDisplay = 0;
uint32_t totalComicCountForDisplay = 0;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
RTC_DATA_ATTR time_t lastArchiveRefreshEpoch = 0;
bool quietSleepNotice = false;
uint32_t networkOperationDeadlineMs = 0;
volatile uint8_t maintenanceButtonInterruptMask = 0;
bool maintenanceCancelled = false;

constexpr uint8_t MAINTENANCE_BUTTON_GREEN = 1U << 0;
constexpr uint8_t MAINTENANCE_BUTTON_RIGHT = 1U << 1;
constexpr uint8_t MAINTENANCE_BUTTON_LEFT = 1U << 2;

struct Comic {
  int number = 0;
  String title;
  String alt;
  String imageUrl;
  String imagePath;
};

struct ImageLayout {
  int width = 0;
  int height = 0;
  int x = 0;
  int y = 0;
  int footerDividerY = 0;
  int footerLineCount = 0;
  String footerLines[config::FOOTER_MAX_LINES];
  float scale = 0.0f;
};

void updatePanel();

void IRAM_ATTR onMaintenanceGreenButton() {
  maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_GREEN;
}

void IRAM_ATTR onMaintenanceRightButton() {
  maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_RIGHT;
}

void IRAM_ATTR onMaintenanceLeftButton() {
  maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_LEFT;
}

void armMaintenanceButtonCancellation() {
  maintenanceButtonInterruptMask = 0;
  maintenanceCancelled = false;
  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_GREEN),
                  onMaintenanceGreenButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_RIGHT),
                  onMaintenanceRightButton, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_LEFT),
                  onMaintenanceLeftButton, FALLING);
  if (!digitalRead(PIN_BUTTON_GREEN)) {
    maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_GREEN;
  }
  if (!digitalRead(PIN_BUTTON_RIGHT)) {
    maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_RIGHT;
  }
  if (!digitalRead(PIN_BUTTON_LEFT)) {
    maintenanceButtonInterruptMask |= MAINTENANCE_BUTTON_LEFT;
  }
}

void disarmMaintenanceButtonCancellation() {
  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON_GREEN));
  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON_RIGHT));
  detachInterrupt(digitalPinToInterrupt(PIN_BUTTON_LEFT));
}

bool maintenanceCancellationRequested() {
  if (networkOperationDeadlineMs == 0) return false;
  if (!maintenanceCancelled && maintenanceButtonInterruptMask != 0) {
    maintenanceCancelled = true;
    LOG.printf("[precache] button press detected (mask=0x%x); "
               "cancelling maintenance\n",
               maintenanceButtonInterruptMask);
  }
  return maintenanceCancelled;
}

bool networkDeadlineReached() {
  return app_logic::deadlineReached(millis(), networkOperationDeadlineMs);
}

bool networkOperationShouldStop() {
  return maintenanceCancellationRequested() || networkDeadlineReached();
}

uint32_t boundedNetworkTimeout(uint32_t normalTimeoutMs) {
  if (networkOperationDeadlineMs == 0) return normalTimeoutMs;
  if (normalTimeoutMs > config::ARCHIVE_CANCEL_POLL_TIMEOUT_MS) {
    normalTimeoutMs = config::ARCHIVE_CANCEL_POLL_TIMEOUT_MS;
  }
  const int32_t remaining =
      static_cast<int32_t>(networkOperationDeadlineMs - millis());
  if (remaining <= 0) return 1;
  const uint32_t remainingMs = static_cast<uint32_t>(remaining);
  return remainingMs < normalTimeoutMs ? remainingMs : normalTimeoutMs;
}

bool clockIsValid() {
  return time(nullptr) >= 1700000000;
}

void configureLocalTimezone() {
  setenv("TZ", config::TIMEZONE, 1);
  tzset();
}

bool localClock(struct tm& localTime) {
  const time_t now = time(nullptr);
  return clockIsValid() && localtime_r(&now, &localTime) != nullptr;
}

int secondsOfDay(const struct tm& localTime) {
  return app_logic::secondsOfDay(
      localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
}

int quietStartSecond() {
  return config::QUIET_START_HOUR * 3600 +
         config::QUIET_START_MINUTE * 60;
}

int quietEndSecond() {
  return config::QUIET_END_HOUR * 3600 +
         config::QUIET_END_MINUTE * 60;
}

bool quietHoursActive(const struct tm& localTime) {
  return app_logic::quietHoursActive(
      config::QUIET_HOURS_ENABLED, secondsOfDay(localTime),
      quietStartSecond(), quietEndSecond());
}

uint64_t secondsUntilTimeOfDay(int targetSecond,
                               const struct tm& localTime) {
  return app_logic::secondsUntilTimeOfDay(
      targetSecond, secondsOfDay(localTime));
}

uint64_t secondsUntilQuietEnd(const struct tm& localTime) {
  return secondsUntilTimeOfDay(quietEndSecond(), localTime);
}

bool nextWakeFallsInQuietHours(const struct tm& localTime,
                               uint64_t normalSleepSeconds) {
  return app_logic::nextWakeFallsInQuietHours(
      config::QUIET_HOURS_ENABLED, secondsOfDay(localTime),
      quietStartSecond(), quietEndSecond(), normalSleepSeconds);
}

String quietEndLabel() {
  char label[6] = {};
  snprintf(label, sizeof(label), "%02u:%02u", config::QUIET_END_HOUR,
           config::QUIET_END_MINUTE);
  return String(label);
}

String wakeReason(esp_sleep_wakeup_cause_t cause, uint64_t wakePins) {
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) return "cold boot/reset";
  if (cause == ESP_SLEEP_WAKEUP_TIMER) return "scheduled timer";
  if (cause != ESP_SLEEP_WAKEUP_EXT1) {
    return "wake cause " + String(static_cast<int>(cause));
  }

  String buttons;
  if (wakePins & (1ULL << PIN_BUTTON_GREEN)) buttons += "GPIO3";
  if (wakePins & (1ULL << PIN_BUTTON_RIGHT)) {
    if (!buttons.isEmpty()) buttons += "+";
    buttons += "GPIO4";
  }
  if (wakePins & (1ULL << PIN_BUTTON_LEFT)) {
    if (!buttons.isEmpty()) buttons += "+";
    buttons += "GPIO5";
  }
  return buttons.isEmpty() ? "front button" : "front button " + buttons;
}

bool logWakeEvent(esp_sleep_wakeup_cause_t cause, uint64_t wakePins,
                  bool logUnsynchronized) {
  const String reason = wakeReason(cause, wakePins);
  struct tm localTime = {};
  if (!localClock(localTime)) {
    if (logUnsynchronized) {
      LOG.printf("[wake] time=unavailable reason=%s\n", reason.c_str());
    }
    return false;
  }
  char formatted[40] = {};
  strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z",
           &localTime);
  LOG.printf("[wake] time=%s reason=%s\n", formatted, reason.c_str());
  return true;
}

bool ntpRefreshDue(bool coldBoot) {
  const time_t now = time(nullptr);
  return app_logic::refreshDue(
      coldBoot, clockIsValid(), now, lastNtpSyncEpoch,
      config::NTP_REFRESH_SECONDS);
}

bool archiveRefreshDue() {
  const time_t now = time(nullptr);
  return app_logic::refreshDue(
      false, clockIsValid(), now, lastArchiveRefreshEpoch,
      config::ARCHIVE_REFRESH_SECONDS);
}

void logMemory(const char* label) {
  LOG.printf("[mem] %-20s heap=%luK psram=%lu/%luK\n", label,
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024),
             static_cast<unsigned long>(ESP.getFreePsram() / 1024),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024));
}

String displayText(String value) {
  value.replace("&quot;", "\"");
  value.replace("&apos;", "'");
  value.replace("&#39;", "'");
  value.replace("&lt;", "<");
  value.replace("&gt;", ">");
  value.replace("&amp;", "&");

  String ascii;
  ascii.reserve(value.length());
  bool lastWasSpace = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c >= 32 && c <= 126) {
      const bool isSpace = c == ' ' || c == '\t' || c == '\r' || c == '\n';
      if (isSpace) {
        if (!lastWasSpace) ascii += ' ';
      } else {
        ascii += static_cast<char>(c);
      }
      lastWasSpace = isSpace;
    } else if (!lastWasSpace) {
      ascii += '?';
      lastWasSpace = false;
    }
  }
  ascii.trim();
  return ascii;
}

String ellipsize(String text, int maxWidth, uint8_t font) {
  if (epaper.textWidth(text, font) <= maxWidth) return text;
  const String suffix = "...";
  while (text.length() > 1 &&
         epaper.textWidth(text + suffix, font) > maxWidth) {
    text.remove(text.length() - 1);
  }
  text.trim();
  return text + suffix;
}

int wrapText(const String& source, String* lines, int maxLines,
             int maxWidth, uint8_t font) {
  String text = displayText(source);
  int lineCount = 0;
  int start = 0;

  while (start < static_cast<int>(text.length()) && lineCount < maxLines) {
    while (start < static_cast<int>(text.length()) && text[start] == ' ') ++start;
    if (start >= static_cast<int>(text.length())) break;

    String line;
    while (start < static_cast<int>(text.length())) {
      int end = text.indexOf(' ', start);
      if (end < 0) end = text.length();
      const String word = text.substring(start, end);
      const String candidate = line.isEmpty() ? word : line + " " + word;
      if (!line.isEmpty() && epaper.textWidth(candidate, font) > maxWidth) break;
      line = candidate;
      start = end + 1;
      if (epaper.textWidth(line, font) > maxWidth) {
        line = ellipsize(line, maxWidth, font);
        break;
      }
    }

    if (lineCount == maxLines - 1 && start < static_cast<int>(text.length())) {
      line = ellipsize(line + "...", maxWidth, font);
    }
    lines[lineCount++] = line;
  }

  if (lineCount == 0) {
    lines[0] = "xkcd";
    lineCount = 1;
  }
  return lineCount;
}

float batteryPercentForVoltage(float voltage) {
  static constexpr float volts[] = {
      3.27f, 3.30f, 3.41f, 3.49f, 3.58f, 3.68f,
      3.75f, 3.80f, 3.85f, 3.91f, 3.96f, 4.15f};
  static constexpr float percents[] = {
      0.0f, 5.0f, 10.0f, 20.0f, 30.0f, 40.0f,
      50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f};
  constexpr size_t count = sizeof(volts) / sizeof(volts[0]);

  if (voltage <= volts[0]) return 0.0f;
  if (voltage >= volts[count - 1]) return 100.0f;
  for (size_t i = 1; i < count; ++i) {
    if (voltage <= volts[i]) {
      const float fraction = (voltage - volts[i - 1]) /
                             (volts[i] - volts[i - 1]);
      return percents[i - 1] + fraction * (percents[i] - percents[i - 1]);
    }
  }
  return 0.0f;
}

void readSensors() {
  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, HIGH);
  delay(200);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
  uint32_t totalMv = 0;
  for (int i = 0; i < 16; ++i) {
    totalMv += analogReadMilliVolts(PIN_BATTERY_ADC);
    delay(4);
  }
  batteryVoltage = (totalMv / 16.0f) * 2.0f / 1000.0f;
  batteryPct = constrain(static_cast<int>(batteryPercentForVoltage(batteryVoltage) + 0.5f),
                         0, 100);
  LOG.printf("[sensor] battery %.3fV -> %d%%\n", batteryVoltage, batteryPct);

  climateValid = false;
  for (uint8_t attempt = 0;
       attempt < config::SENSOR_READ_ATTEMPTS && !climateValid; ++attempt) {
    if (attempt > 0) {
      // A sensor left powered across deep sleep can occasionally miss the
      // first transaction. Reset the ESP32 I2C peripheral before retrying.
      Wire.end();
      i2cReady = false;
      delay(config::SENSOR_RETRY_DELAY_MS);
    }
    i2cReady = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);

    if (sht4.begin(&Wire)) {
      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sensors_event_t humidity;
      sensors_event_t temperature;
      if (sht4.getEvent(&humidity, &temperature)) {
        temperatureC = temperature.temperature;
        humidityPct = humidity.relative_humidity;
        climateValid = true;
        LOG.printf("[sensor] %.1fC %.0f%% RH (attempt %u)\n",
                   temperatureC, humidityPct, attempt + 1);
        break;
      }
    }

    LOG.printf("[sensor] SHT4x attempt %u/%u failed\n", attempt + 1,
               config::SENSOR_READ_ATTEMPTS);
  }
  if (!climateValid) LOG.println("[sensor] SHT4x unavailable after retries");
}

bool ensureI2cBus() {
  if (i2cReady) return true;
  i2cReady = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!i2cReady) {
    LOG.println("[rtc] could not initialize the I2C bus");
    return false;
  }
  Wire.setClock(100000);
  return true;
}

bool readAndLogHardwareRtc(pcf8563::Reading& stored) {
  if (!ensureI2cBus()) return false;
  String error;
  if (!pcf8563::readUtc(Wire, stored, error)) {
    LOG.printf("[rtc] PCF8563 read failed: %s\n", error.c_str());
    return false;
  }
  LOG.printf("[rtc] PCF8563 stored UTC=%s, %s\n",
             pcf8563::format(stored).c_str(),
             stored.voltageLow ? "VL set - stored time is unreliable"
                               : "VL clear - stored time is valid");
  return true;
}

void saveNtpTimeToHardwareRtc(time_t now) {
  if (!ensureI2cBus()) return;
  String error;
  if (!pcf8563::writeUtc(Wire, now, error)) {
    LOG.printf("[rtc] PCF8563 NTP update failed: %s\n", error.c_str());
    return;
  }
  LOG.println("[rtc] PCF8563 updated from NTP");
  pcf8563::Reading verified;
  readAndLogHardwareRtc(verified);
}

bool restoreClockFromHardwareRtc() {
  pcf8563::Reading stored;
  if (!readAndLogHardwareRtc(stored)) return false;
  String error;
  if (!pcf8563::setSystemClock(stored, error)) {
    LOG.printf("[rtc] PCF8563 fallback rejected: %s\n", error.c_str());
    return false;
  }
  LOG.printf("[rtc] restored ESP32 clock from PCF8563 UTC=%s\n",
             pcf8563::format(stored).c_str());
  return true;
}

void selectStatusFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold12pt7b);
#else
  epaper.setFreeFont(&FreeSansBold9pt7b);
#endif
}

void selectCacheStatsFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSans9pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSans9pt7b);
#else
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
#endif
}

void selectTitleFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold24pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold18pt7b);
#else
  epaper.setFreeFont(&FreeSansBold12pt7b);
#endif
}

void selectFooterFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold12pt7b);
#else
  epaper.setFreeFont(&FreeSansBold9pt7b);
#endif
}

void fillStatusBackground(int top, int height) {
  epaper.fillRect(0, top, config::PANEL_WIDTH, height,
                  PANEL_STATUS_BACKGROUND);
  if (!PANEL_STATUS_DITHERED) return;

  static constexpr uint8_t bayer4[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  const int bottom = min(config::PANEL_HEIGHT, top + height);
  for (int y = max(0, top); y < bottom; ++y) {
    for (int x = 0; x < config::PANEL_WIDTH; ++x) {
      if (bayer4[y & 3][x & 3] < PANEL_STATUS_DITHER_THRESHOLD)
        epaper.drawPixel(x, y, PANEL_STATUS_DITHER_COLOR);
    }
  }
}

void drawBadges(uint32_t background = PANEL_WHITE,
                bool fillTextBackground = true) {
  epaper.setTextColor(PANEL_BLACK, background, fillTextBackground);
  selectStatusFont();

  const int statusCenterY = config::ui(24);
  const int edgeInset = config::ui(6);
  epaper.setTextDatum(ML_DATUM);
  String climate = "--.-C  --%";
  if (climateValid) {
    climate = String(temperatureC, 1) + "C  " + String(humidityPct, 0) + "%";
  }
  epaper.drawString(climate, edgeInset, statusCenterY, 1);

  String percent = batteryPct >= 0 ? String(batteryPct) + "%" : "--%";

  // Keep the whole battery group clear of the bezel. A fixed two-pixel optical
  // offset aligns the gauge with the percentage on the high-DPI E1003 without
  // over-scaling that adjustment.
  const int w = config::ui(22);
  const int h = config::ui(12);
  const int terminalWidth = max(3, config::ui(5));
  const int x = config::PANEL_WIDTH - edgeInset - terminalWidth - w;
  const int gaugeCenterY = statusCenterY + 2;
  const int y = gaugeCenterY - h / 2;
  const int outline = max(1, config::ui(1));
  const int terminalHeight = max(3, config::ui(5));

  epaper.setTextDatum(MR_DATUM);
  const int percentRightX = x - config::ui(9);
  epaper.drawString(percent, percentRightX, statusCenterY, 1);
  if (cacheStatsAvailable) {
    const int statsRightX =
        percentRightX - epaper.textWidth(percent, 1) - config::ui(10);
    selectCacheStatsFont();
    epaper.setTextColor(PANEL_CACHE_STATS_COLOR, background,
                        fillTextBackground);
    const int statsCenterDistance = epaper.fontHeight(1) + 1;
    const int upperStatsY = statusCenterY - statsCenterDistance / 2;
    const int lowerStatsY = upperStatsY + statsCenterDistance;
    epaper.drawString(String(cachedComicCountForDisplay), statsRightX,
                      upperStatsY, 1);
    epaper.drawString(totalComicCountForDisplay > 0
                          ? String(totalComicCountForDisplay)
                          : "--",
                      statsRightX, lowerStatsY, 1);
  }
  for (int inset = 0; inset < outline; ++inset) {
    epaper.drawRect(x + inset, y + inset, w - 2 * inset, h - 2 * inset,
                    PANEL_BLACK);
  }
  epaper.fillRect(x + w, gaugeCenterY - terminalHeight / 2,
                  terminalWidth, terminalHeight, PANEL_BLACK);
  if (batteryPct >= 0) {
    const int innerX = x + outline + 1;
    const int innerY = y + outline + 1;
    const int innerWidth = max(0, w - 2 * (outline + 1));
    const int innerHeight = max(0, h - 2 * (outline + 1));
    const int fillWidth = (innerWidth * batteryPct + 50) / 100;
    if (fillWidth > 0) {
      epaper.fillRect(innerX, innerY, fillWidth, innerHeight, PANEL_BLACK);
    }
  }

  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

String wifiStationMacAddress() {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    return "unavailable";
  }
  char formatted[18];
  snprintf(formatted, sizeof(formatted),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(formatted);
}

void renderStatus(const String& message, const String& detail = "",
                  const String& lineAbove = "") {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  if (!lineAbove.isEmpty()) {
    selectFooterFont();
    epaper.drawString(ellipsize(displayText(lineAbove),
                                config::PANEL_WIDTH - config::ui(60), 1),
                      config::PANEL_WIDTH / 2,
                      config::PANEL_HEIGHT / 2 - config::ui(55), 1);
  }
  selectTitleFont();
  epaper.drawString(ellipsize(displayText(message),
                              config::PANEL_WIDTH - config::ui(60), 1),
                    config::PANEL_WIDTH / 2,
                    config::PANEL_HEIGHT / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectFooterFont();
    epaper.drawString(ellipsize(displayText(detail),
                                config::PANEL_WIDTH - config::ui(60), 1),
                      config::PANEL_WIDTH / 2,
                      config::PANEL_HEIGHT / 2 + config::ui(22), 1);
  }
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  drawBadges();
  updatePanel();
}

void beep() {
  pinMode(PIN_BUZZER, OUTPUT);
  tone(PIN_BUZZER, 1000, 100);
  delay(120);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

bool mountSd() {
  pinMode(PIN_SD_ENABLE, OUTPUT);
  digitalWrite(PIN_SD_ENABLE, HIGH);
  pinMode(PIN_SD_DETECT, INPUT_PULLUP);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  delay(50);

  SPIClass& spi = epaper.getSPIinstance();
  spi.end();
  spi.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, -1);
  if (!SD.begin(PIN_SD_CS, spi)) {
    LOG.println("[sd] mount failed; insert a FAT32/exFAT card");
    digitalWrite(PIN_SD_ENABLE, LOW);
    return false;
  }
  if (!SD.exists(config::CACHE_DIR) && !SD.mkdir(config::CACHE_DIR)) {
    LOG.println("[sd] could not create /xkcd");
    SD.end();
    digitalWrite(PIN_SD_ENABLE, LOW);
    return false;
  }
  LOG.printf("[sd] mounted, card=%lluMB\n",
             static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool readFile(const String& path, String& output) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  output = file.readString();
  file.close();
  return !output.isEmpty();
}

bool writeFileAtomically(const String& path, const String& contents) {
  const String temporary = path + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) return false;
  const size_t written = file.print(contents);
  file.flush();
  file.close();
  if (written != contents.length()) {
    SD.remove(temporary);
    return false;
  }
  SD.remove(path);
  if (!SD.rename(temporary, path)) {
    SD.remove(temporary);
    return false;
  }
  return true;
}

bool writeLittleEndian16(File& file, uint16_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
  };
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

bool writeLittleEndian32(File& file, uint32_t value) {
  const uint8_t bytes[] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 24),
  };
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

void screenshotPaletteColor(uint8_t index, uint8_t& red, uint8_t& green,
                            uint8_t& blue) {
#if RETERMINAL_MODEL == 1001
  const uint8_t gray = index <= 3 ? static_cast<uint8_t>(index * 85) : 255;
  red = green = blue = gray;
#elif RETERMINAL_MODEL == 1003
  const uint8_t gray = index <= 15 ? static_cast<uint8_t>(index * 17) : 255;
  red = green = blue = gray;
#else
  red = green = blue = 255;
  switch (index) {
    case 0x0: red = 255; green = 255; blue = 255; break;
    case 0x2: red = 29;  green = 185; blue = 84;  break;
    case 0x6: red = 229; green = 57;  blue = 53;  break;
    case 0xB: red = 255; green = 216; blue = 0;   break;
    case 0xD: red = 0;   green = 76;  blue = 255; break;
    case 0xF: red = 0;   green = 0;   blue = 0;   break;
  }
#endif
}

bool saveScreenshotBmp() {
  constexpr char screenshotPath[] = "/screenshot.bmp";
  constexpr char temporaryPath[] = "/screenshot.bmp.part";
  constexpr uint32_t fileHeaderSize = 14;
  constexpr uint32_t dibHeaderSize = 40;
  constexpr uint32_t paletteSize = 256 * 4;
  constexpr uint32_t pixelOffset = fileHeaderSize + dibHeaderSize + paletteSize;

  const uint32_t width = config::PANEL_WIDTH;
  const uint32_t height = config::PANEL_HEIGHT;
  const uint32_t rowSize = (width + 3U) & ~3U;
  const uint32_t pixelBytes = rowSize * height;
  const uint32_t fileSize = pixelOffset + pixelBytes;

  uint8_t* row = static_cast<uint8_t*>(malloc(rowSize));
  if (!row) {
    LOG.println("[screenshot] could not allocate BMP row buffer");
    return false;
  }

  SD.remove(temporaryPath);
  File file = SD.open(temporaryPath, FILE_WRITE);
  if (!file) {
    LOG.println("[screenshot] could not create temporary BMP");
    free(row);
    return false;
  }

  bool ok = file.write(static_cast<uint8_t>('B')) == 1 &&
            file.write(static_cast<uint8_t>('M')) == 1 &&
            writeLittleEndian32(file, fileSize) &&
            writeLittleEndian32(file, 0) &&
            writeLittleEndian32(file, pixelOffset) &&
            writeLittleEndian32(file, dibHeaderSize) &&
            writeLittleEndian32(file, width) &&
            writeLittleEndian32(file, height) &&
            writeLittleEndian16(file, 1) &&
            writeLittleEndian16(file, 8) &&
            writeLittleEndian32(file, 0) &&
            writeLittleEndian32(file, pixelBytes) &&
            writeLittleEndian32(file, 2835) &&
            writeLittleEndian32(file, 2835) &&
            writeLittleEndian32(file, 256) &&
            writeLittleEndian32(file, 16);

  for (uint16_t index = 0; ok && index < 256; ++index) {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    screenshotPaletteColor(static_cast<uint8_t>(index), red, green, blue);
    const uint8_t entry[] = {blue, green, red, 0};
    ok = file.write(entry, sizeof(entry)) == sizeof(entry);
  }

  memset(row, 0, rowSize);
  // A positive BMP height stores rows bottom-up. readPixelValue() returns the
  // raw Gray4, Gray16, or E6 palette index from the composed panel sprite.
  for (int32_t y = static_cast<int32_t>(height) - 1; ok && y >= 0; --y) {
    for (uint32_t x = 0; x < width; ++x) {
      row[x] = static_cast<uint8_t>(epaper.readPixelValue(x, y));
    }
    ok = file.write(row, rowSize) == rowSize;
    if ((y & 31) == 0) delay(1);
  }

  file.flush();
  file.close();
  free(row);

  if (!ok) {
    LOG.println("[screenshot] BMP write failed");
    SD.remove(temporaryPath);
    return false;
  }

  SD.remove(screenshotPath);
  if (!SD.rename(temporaryPath, screenshotPath)) {
    LOG.println("[screenshot] could not install /screenshot.bmp");
    SD.remove(temporaryPath);
    return false;
  }

  LOG.printf("[screenshot] saved %s (%lu bytes)\n", screenshotPath,
             static_cast<unsigned long>(fileSize));
  return true;
}

void updatePanel() {
  if (screenshotRequested && sdReady) {
    saveScreenshotBmp();
    screenshotRequested = false;
  }
  epaper.update();
}

bool httpGetString(const String& url, String& body) {
  if (networkOperationShouldStop()) return false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(boundedNetworkTimeout(config::HTTP_TIMEOUT_MS));
  HTTPClient http;
  http.setConnectTimeout(boundedNetworkTimeout(config::HTTP_TIMEOUT_MS));
  http.setTimeout(boundedNetworkTimeout(config::HTTP_TIMEOUT_MS));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  const int status = http.GET();
  if (networkOperationShouldStop()) {
    http.end();
    return false;
  }
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] GET %s -> %d\n", url.c_str(), status);
    http.end();
    return false;
  }
  client.setTimeout(boundedNetworkTimeout(config::HTTP_TIMEOUT_MS));
  body = http.getString();
  http.end();
  return !networkOperationShouldStop() && !body.isEmpty();
}

bool downloadToSd(const String& url, const String& destination) {
  if (networkOperationShouldStop()) return false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(boundedNetworkTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS));
  HTTPClient http;
  http.setConnectTimeout(boundedNetworkTimeout(config::HTTP_TIMEOUT_MS));
  http.setTimeout(boundedNetworkTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] image GET -> %d\n", status);
    http.end();
    return false;
  }
  const int declaredSize = http.getSize();
  if (declaredSize > static_cast<int>(config::MAX_IMAGE_BYTES)) {
    LOG.printf("[http] image is too large to cache: %d bytes\n", declaredSize);
    http.end();
    return false;
  }

  const String temporary = destination + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) {
    LOG.printf("[cache] could not create %s\n", temporary.c_str());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  constexpr size_t bufferSize = 4096;
  uint8_t* buffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!buffer) {
    LOG.println("[http] could not allocate SD download buffer");
    file.close();
    SD.remove(temporary);
    http.end();
    return false;
  }
  size_t total = 0;
  uint32_t lastDataAt = millis();
  bool ok = true;

  while (!networkOperationShouldStop() && http.connected() &&
         (declaredSize < 0 ||
          total < static_cast<size_t>(declaredSize))) {
    const size_t available = stream->available();
    if (available > 0) {
      const size_t wanted = available < bufferSize ? available : bufferSize;
      stream->setTimeout(
          boundedNetworkTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS));
      const int received = stream->readBytes(buffer, wanted);
      if (received <= 0 || file.write(buffer, received) != static_cast<size_t>(received)) {
        ok = false;
        break;
      }
      total += received;
      lastDataAt = millis();
      if (total > config::MAX_IMAGE_BYTES) {
        LOG.println("[http] image exceeded download limit");
        ok = false;
        break;
      }
      // Let the network and idle tasks run and feed the task watchdog even
      // when both the server and SD card can sustain a continuous stream.
      delay(1);
    } else {
      if (millis() - lastDataAt > config::DOWNLOAD_IDLE_TIMEOUT_MS) {
        LOG.println("[http] image download timed out");
        ok = false;
        break;
      }
      delay(5);
    }
  }
  if (networkOperationShouldStop()) ok = false;
  if (declaredSize >= 0 && total != static_cast<size_t>(declaredSize)) ok = false;
  free(buffer);
  file.flush();
  file.close();
  http.end();

  if (!ok || total == 0) {
    LOG.printf("[cache] write/download failed for %s after %lu bytes\n",
               destination.c_str(), static_cast<unsigned long>(total));
    SD.remove(temporary);
    return false;
  }
  SD.remove(destination);
  if (!SD.rename(temporary, destination)) {
    LOG.printf("[cache] could not install %s\n", destination.c_str());
    SD.remove(temporary);
    return false;
  }
  LOG.printf("[cache] saved %s (%lu bytes)\n", destination.c_str(),
             static_cast<unsigned long>(total));
  return true;
}

bool downloadToMemory(const String& url, uint8_t*& output, size_t& outputLength) {
  output = nullptr;
  outputLength = 0;
  if (networkOperationShouldStop()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(boundedNetworkTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS));
  HTTPClient http;
  http.setConnectTimeout(boundedNetworkTimeout(config::HTTP_TIMEOUT_MS));
  http.setTimeout(boundedNetworkTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] live image GET -> %d\n", status);
    http.end();
    return false;
  }

  const int declaredSize = http.getSize();
  if (declaredSize > static_cast<int>(config::MAX_LIVE_IMAGE_BYTES)) {
    LOG.printf("[http] live image is too large for PSRAM: %d bytes\n", declaredSize);
    http.end();
    return false;
  }

  size_t capacity = declaredSize > 0 ? static_cast<size_t>(declaredSize) : 128U * 1024U;
  uint8_t* data = static_cast<uint8_t*>(ps_malloc(capacity));
  if (!data) {
    LOG.printf("[http] could not allocate %lu bytes for live image\n",
               static_cast<unsigned long>(capacity));
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t total = 0;
  uint32_t lastDataAt = millis();
  bool ok = true;

  while (!networkOperationShouldStop() && http.connected() &&
         (declaredSize < 0 ||
          total < static_cast<size_t>(declaredSize))) {
    size_t available = stream->available();
    if (available > 0) {
      if (total + available > config::MAX_LIVE_IMAGE_BYTES) {
        LOG.println("[http] live image exceeded PSRAM download limit");
        ok = false;
        break;
      }
      if (total + available > capacity) {
        size_t expanded = capacity;
        while (expanded < total + available) expanded *= 2;
        if (expanded > config::MAX_LIVE_IMAGE_BYTES) expanded = config::MAX_LIVE_IMAGE_BYTES;
        uint8_t* resized = static_cast<uint8_t*>(ps_realloc(data, expanded));
        if (!resized) {
          LOG.println("[http] could not grow live-image PSRAM buffer");
          ok = false;
          break;
        }
        data = resized;
        capacity = expanded;
      }
      const size_t wanted = available < 4096 ? available : 4096;
      stream->setTimeout(
          boundedNetworkTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS));
      const int received = stream->readBytes(data + total, wanted);
      if (received <= 0) {
        ok = false;
        break;
      }
      total += received;
      lastDataAt = millis();
    } else {
      if (millis() - lastDataAt > config::DOWNLOAD_IDLE_TIMEOUT_MS) {
        LOG.println("[http] live image download timed out");
        ok = false;
        break;
      }
      delay(5);
    }
  }
  if (networkOperationShouldStop()) ok = false;
  if (declaredSize >= 0 && total != static_cast<size_t>(declaredSize)) ok = false;
  http.end();

  if (!ok || total == 0) {
    free(data);
    return false;
  }
  if (total < capacity) {
    uint8_t* resized = static_cast<uint8_t*>(ps_realloc(data, total));
    if (resized) data = resized;
  }
  output = data;
  outputLength = total;
  LOG.printf("[http] live image held in PSRAM (%lu bytes, not cached)\n",
             static_cast<unsigned long>(total));
  return true;
}

bool parseComic(const String& json, Comic& comic) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, json);
  if (error) {
    LOG.printf("[json] %s\n", error.c_str());
    return false;
  }
  comic.number = document["num"] | 0;
  const char* title = document["safe_title"];
  if (!title || !*title) title = document["title"];
  if (!title || !*title) title = "xkcd";
  comic.title = displayText(String(title));
  comic.alt = displayText(String(document["alt"] | ""));
  comic.imageUrl = String(document["img"] | "");
  return comic.number > 0 && !comic.imageUrl.isEmpty();
}

String metadataPath(int number) {
  return String(config::CACHE_DIR) + "/" + number + ".json";
}

String imageExtension(const String& url) {
  String path = url;
  const int query = path.indexOf('?');
  if (query >= 0) path.remove(query);
  const int dot = path.lastIndexOf('.');
  if (dot < 0) return "";
  String extension = path.substring(dot);
  extension.toLowerCase();
  if (extension == ".png" || extension == ".jpg" ||
      extension == ".jpeg" || extension == ".bmp") return extension;
  return "";
}

bool comicFullyCached(int number) {
  if (!sdReady || number <= 0 || number == 404) return false;
  String json;
  Comic comic;
  if (!readFile(metadataPath(number), json) || !parseComic(json, comic))
    return false;
  const String extension = imageExtension(comic.imageUrl);
  if (extension.isEmpty()) return false;
  return SD.exists(String(config::CACHE_DIR) + "/" + number + extension);
}

uint32_t countCachedComics() {
  if (!sdReady) return 0;
  File directory = SD.open(config::CACHE_DIR);
  if (!directory || !directory.isDirectory()) return 0;
  uint32_t count = 0;
  File entry = directory.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      const int dot = name.lastIndexOf('.');
      if (dot > 0) {
        String extension = name.substring(dot);
        extension.toLowerCase();
        if (extension == ".png" || extension == ".jpg" ||
            extension == ".jpeg" || extension == ".bmp") {
          String numberText = name.substring(0, dot);
          bool numeric = !numberText.isEmpty();
          for (size_t i = 0; i < numberText.length(); ++i)
            numeric &= isDigit(numberText[i]);
          if (numeric &&
              SD.exists(metadataPath(numberText.toInt()))) {
            ++count;
          }
        }
      }
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  return count;
}

bool getComic(int number, bool networkAvailable, Comic& comic) {
  const String metaPath = metadataPath(number);
  String json;
  bool metadataCached = readFile(metaPath, json) && parseComic(json, comic);

  if (!metadataCached) {
    if (!networkAvailable || number == 404) return false;
    const String url = "https://xkcd.com/" + String(number) + "/info.0.json";
    if (!httpGetString(url, json) || !parseComic(json, comic)) return false;
    if (writeFileAtomically(metaPath, json)) {
      LOG.printf("[cache] saved metadata #%d\n", number);
    } else {
      sdCacheWritable = false;
      LOG.printf("[cache] metadata #%d not stored; continuing without it\n",
                 number);
    }
  }

  const String extension = imageExtension(comic.imageUrl);
  if (extension.isEmpty()) {
    LOG.printf("[comic] #%d uses an unsupported image format\n", comic.number);
    return false;
  }
  comic.imagePath = String(config::CACHE_DIR) + "/" + comic.number + extension;
  if (!SD.exists(comic.imagePath)) {
    if (!networkAvailable) return false;
    if (!sdCacheWritable) {
      LOG.printf("[cache] skipping image write for #%d after an SD write failure\n",
                 comic.number);
      return false;
    }
    LOG.printf("[comic] downloading #%d from %s\n", comic.number, comic.imageUrl.c_str());
    if (!downloadToSd(comic.imageUrl, comic.imagePath)) {
      sdCacheWritable = false;
      LOG.printf("[cache] image #%d not stored; PSRAM fallback available\n",
                 comic.number);
      return false;
    }
  } else {
    LOG.printf("[cache] using %s\n", comic.imagePath.c_str());
  }
  return true;
}

bool getLatestNumber(bool networkAvailable, int& latest,
                     bool refreshOnline = false) {
  String json;
  Comic comic;
  const bool shouldCheckOnline = networkAvailable &&
      (refreshOnline || !SD.exists(config::LATEST_CACHE));
  if (shouldCheckOnline) {
    if (httpGetString(config::XKCD_LATEST_URL, json) && parseComic(json, comic)) {
      latest = comic.number;
      if (!writeFileAtomically(config::LATEST_CACHE, json)) {
        sdCacheWritable = false;
        LOG.println("[cache] latest metadata not stored; using live value");
      }
      if (sdCacheWritable &&
          !writeFileAtomically(metadataPath(latest), json)) {
        sdCacheWritable = false;
        LOG.printf("[cache] metadata #%d not stored; using live value\n",
                   latest);
      }
      LOG.printf("[xkcd] latest is #%d\n", latest);
      totalComicCountForDisplay =
          app_logic::publishedComicCount(latest);
      return true;
    }
  }
  if (readFile(config::LATEST_CACHE, json) && parseComic(json, comic)) {
    latest = comic.number;
    totalComicCountForDisplay =
        app_logic::publishedComicCount(latest);
    LOG.printf("[xkcd] cached latest is #%d\n", latest);
    return true;
  }
  return false;
}

void refreshArchiveCache() {
  if (!sdReady || WiFi.status() != WL_CONNECTED) return;

  int previousLatest = 0;
  getLatestNumber(false, previousLatest);
  int latest = 0;
  if (!getLatestNumber(true, latest, true)) {
    if (maintenanceCancellationRequested()) {
      LOG.println("[precache] maintenance cancelled by button");
    } else if (networkDeadlineReached()) {
      LOG.println("[precache] five-minute maintenance deadline reached");
    } else {
      LOG.println("[precache] latest XKCD lookup failed");
    }
    return;
  }

  uint8_t latestAdded = 0;
  if ((latest > previousLatest || !comicFullyCached(latest)) &&
      latest != 404) {
    Comic newest;
    if (getComic(latest, true, newest) && comicFullyCached(latest)) {
      latestAdded = 1;
      LOG.printf("[precache] cached newest comic #%d\n", latest);
    }
  }

  uint8_t oldAdded = 0;
  uint16_t attempts = 0;
  const uint16_t requestedAttempts =
      static_cast<uint16_t>(config::ARCHIVE_OLD_COMICS_PER_REFRESH) * 20U;
  const uint16_t maxAttempts =
      requestedAttempts > 80U ? requestedAttempts : 80U;
  while (oldAdded < config::ARCHIVE_OLD_COMICS_PER_REFRESH &&
         attempts++ < maxAttempts && latest > 1 && sdCacheWritable &&
         !networkOperationShouldStop()) {
    const int number = random(1, latest);
    if (number == 404 || comicFullyCached(number)) continue;
    Comic historical;
    if (getComic(number, true, historical) && comicFullyCached(number)) {
      ++oldAdded;
      LOG.printf("[precache] cached historical comic %u/%u: #%d\n",
                 oldAdded, config::ARCHIVE_OLD_COMICS_PER_REFRESH, number);
    }
  }

  if (maintenanceCancellationRequested()) {
    LOG.println("[precache] maintenance cancelled; remaining downloads deferred");
  } else if (networkDeadlineReached()) {
    LOG.println("[precache] five-minute maintenance deadline reached; "
                "remaining downloads deferred");
  }
  LOG.printf("[precache] refill complete: %u newest, %u historical, "
             "%lu total cached\n",
             latestAdded, oldAdded,
             static_cast<unsigned long>(countCachedComics()));
}

bool getLatestNumberWithoutSd(bool networkAvailable, int& latest) {
  if (!networkAvailable) return false;

  String json;
  Comic latestComic;
  if (httpGetString(config::XKCD_LATEST_URL, json) && parseComic(json, latestComic)) {
    latest = latestComic.number;
    LOG.printf("[xkcd] live latest is #%d (not cached)\n", latest);
    return true;
  }
  return false;
}

bool getComicWithoutSd(int number, Comic& comic, uint8_t*& compressed,
                       size_t& compressedLength) {
  compressed = nullptr;
  compressedLength = 0;
  if (number <= 0 || number == 404) return false;

  String json;
  const String metadataUrl = "https://xkcd.com/" + String(number) + "/info.0.json";
  if (!httpGetString(metadataUrl, json) || !parseComic(json, comic)) return false;
  if (imageExtension(comic.imageUrl).isEmpty()) {
    LOG.printf("[comic] #%d uses an unsupported image format\n", comic.number);
    return false;
  }
  LOG.printf("[comic] downloading #%d directly to PSRAM\n", comic.number);
  return downloadToMemory(comic.imageUrl, compressed, compressedLength);
}

int pickRandomCachedNumber() {
  File directory = SD.open(config::CACHE_DIR);
  if (!directory || !directory.isDirectory()) return 0;
  int selected = 0;
  uint32_t seen = 0;
  File entry = directory.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      const int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      const int dot = name.lastIndexOf('.');
      if (dot > 0) {
        String extension = name.substring(dot);
        extension.toLowerCase();
        if (extension == ".png" || extension == ".jpg" ||
            extension == ".jpeg" || extension == ".bmp") {
          const String numberText = name.substring(0, dot);
          bool numeric = !numberText.isEmpty();
          for (size_t i = 0; i < numberText.length(); ++i)
            numeric &= isDigit(numberText[i]);
          const int number = numeric ? numberText.toInt() : 0;
          if (!numeric || number <= 0 || number == 404 ||
              !SD.exists(metadataPath(number))) {
            entry.close();
            entry = directory.openNextFile();
            continue;
          }
          ++seen;
          if (random(static_cast<long>(seen)) == 0) selected = number;
        }
      }
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  return selected;
}

ImageLayout calculateLayout(const Comic& comic, int sourceWidth, int sourceHeight) {
  ImageLayout layout;
  const String footer = comic.alt.isEmpty() ? comic.title : comic.alt;
  selectFooterFont();
  layout.footerLineCount = wrapText(footer, layout.footerLines,
                                    config::FOOTER_MAX_LINES,
                                    config::PANEL_WIDTH - config::ui(24), 1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  layout.footerDividerY =
      config::FOOTER_BOTTOM -
      layout.footerLineCount * config::FOOTER_LINE_HEIGHT -
      2 * config::FOOTER_VERTICAL_PADDING;
  const int maxWidth = config::PANEL_WIDTH - 2 * config::CONTENT_MARGIN_X;
  const int maxHeight = layout.footerDividerY - config::CONTENT_TOP - config::ui(6);
  // Contain the comic in this model's actual content rectangle. Unlike the
  // original E1001-only layout, this also enlarges small source comics so they
  // remain readable on high-resolution E1003/E1004 panels.
  layout.scale = min(static_cast<float>(maxWidth) / sourceWidth,
                     static_cast<float>(maxHeight) / sourceHeight);
  layout.width = max(2, static_cast<int>(sourceWidth * layout.scale));
  layout.height = max(2, static_cast<int>(sourceHeight * layout.scale));
  // Packed 4bpp requires an even row width (two pixels per byte), but the
  // number of rows may be odd. Preserving an odd height also preserves source
  // images whose final rule or border is drawn on their last row.
  if (layout.width & 1) --layout.width;
  layout.x = (config::PANEL_WIDTH - layout.width) / 2;
  if (layout.x & 1) --layout.x;
  layout.y = config::CONTENT_TOP + (maxHeight - layout.height) / 2;
  return layout;
}

bool layoutIsSuitable(const ImageLayout& layout, int comicNumber) {
  if (layout.scale < config::MIN_DISPLAY_SCALE) {
    LOG.printf("[comic] #%d skipped: it needs reduction below %.0f%%\n",
               comicNumber, config::MIN_DISPLAY_SCALE * 100.0f);
    return false;
  }
  if (layout.width < config::MIN_RENDERED_WIDTH) {
    LOG.printf("[comic] #%d skipped: rendered width %d is below the "
               "%d-pixel minimum for this panel\n",
               comicNumber, layout.width, config::MIN_RENDERED_WIDTH);
    return false;
  }
  return true;
}

bool loadUsableComic(int number, bool networkAvailable, Comic& comic,
                     RgbImage& image, ImageLayout& layout) {
  comic = Comic{};
  const bool sdImageReady = getComic(number, networkAvailable, comic);
  bool decoded = false;

  if (sdImageReady) {
    logMemory("before decode");
    decoded = load_image_from_sd(comic.imagePath.c_str(), 0, 0, &image);
    if (!decoded) {
      LOG.printf("[comic] cached #%d could not be decoded\n", number);
      image_free(&image);
    }
  }

  // A mounted SD card can still be read-only, full, or have a failed/corrupt
  // cache entry. Keep the live PSRAM path available instead of making the
  // mounted-card path an all-or-nothing choice.
  if (!decoded && networkAvailable && comic.number > 0 &&
      !comic.imageUrl.isEmpty() && !imageExtension(comic.imageUrl).isEmpty()) {
    LOG.printf("[comic] loading #%d live into PSRAM without caching\n", number);
    uint8_t* compressed = nullptr;
    size_t compressedLength = 0;
    if (downloadToMemory(comic.imageUrl, compressed, compressedLength)) {
      decoded = load_image_from_memory(
          compressed, compressedLength, comic.imageUrl.c_str(), 0, 0, &image);
      free(compressed);
    }
  }

  if (!decoded) {
    if (!sdImageReady && !networkAvailable) {
      LOG.printf("[comic] #%d is not fully cached\n", number);
    } else if (comic.number <= 0 || comic.imageUrl.isEmpty()) {
      LOG.printf("[comic] #%d metadata or image is unavailable\n", number);
    } else {
      LOG.printf("[comic] #%d could not be decoded from cache or live data\n",
                 number);
    }
    return false;
  }
  layout = calculateLayout(comic, image.width, image.height);
  LOG.printf("[layout] #%d source=%dx%d target=%dx%d scale=%.3f\n",
             comic.number, image.width, image.height, layout.width, layout.height,
             layout.scale);
  if (!layoutIsSuitable(layout, comic.number)) {
    image_free(&image);
    return false;
  }
  return true;
}

bool acquireComic(bool networkAvailable, Comic& comic, RgbImage& image,
                  ImageLayout& layout) {
  if (networkAvailable) {
    int latest = 0;
    if (getLatestNumber(true, latest)) {
      for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS;
           ++attempt) {
        int number = random(1, latest + 1);
        if (number == 404) continue;
        LOG.printf("[comic] random attempt %u: #%d\n", attempt + 1, number);
        if (loadUsableComic(number, true, comic, image, layout)) return true;
        image_free(&image);
      }
    }
    LOG.println("[comic] live selection failed; trying the local SD cache");
  } else {
    LOG.println("[comic] offline wake; selecting from the local SD cache");
  }

  for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS; ++attempt) {
    const int number = pickRandomCachedNumber();
    if (number <= 0 || number == 404) continue;
    LOG.printf("[cache] random local attempt %u: #%d\n", attempt + 1, number);
    if (loadUsableComic(number, false, comic, image, layout)) return true;
    image_free(&image);
  }
  return false;
}

bool acquireComicWithoutSd(bool networkAvailable, Comic& comic, RgbImage& image,
                           ImageLayout& layout) {
  int latest = 0;
  if (!getLatestNumberWithoutSd(networkAvailable, latest) || !networkAvailable) {
    return false;
  }

  for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS; ++attempt) {
    const int number = random(1, latest + 1);
    if (number == 404) continue;
    LOG.printf("[comic] live attempt %u: #%d\n", attempt + 1, number);

    uint8_t* compressed = nullptr;
    size_t compressedLength = 0;
    if (!getComicWithoutSd(number, comic, compressed, compressedLength)) continue;
    const bool decoded = load_image_from_memory(
        compressed, compressedLength, comic.imageUrl.c_str(), 0, 0, &image);
    free(compressed);
    if (!decoded) {
      LOG.printf("[comic] live #%d could not be decoded\n", number);
      continue;
    }

    layout = calculateLayout(comic, image.width, image.height);
    LOG.printf("[layout] live #%d source=%dx%d target=%dx%d scale=%.3f\n",
               comic.number, image.width, image.height, layout.width, layout.height,
               layout.scale);
    if (layoutIsSuitable(layout, comic.number)) return true;
    image_free(&image);
  }
  return false;
}

void pack4bppInPlace(uint8_t* indices, int width, int height) {
  for (int y = 0; y < height; ++y) {
    const uint8_t* source = indices + static_cast<size_t>(y) * width;
    uint8_t* destination = indices + static_cast<size_t>(y) * (width / 2);
    for (int x = 0; x < width; x += 2) {
      destination[x / 2] = static_cast<uint8_t>(((source[x] & 0x0F) << 4) |
                                                (source[x + 1] & 0x0F));
    }
  }
}

bool renderComic(const Comic& comic, RgbImage& image, ImageLayout layout) {
  const bool scaling = layout.width != image.width || layout.height != image.height;
  const size_t pixelCount = static_cast<size_t>(layout.width) * layout.height;
  uint8_t* indices = static_cast<uint8_t*>(ps_malloc(pixelCount));
  if (!indices) indices = static_cast<uint8_t*>(malloc(pixelCount));
  if (!indices) return false;

#if RETERMINAL_MODEL == 1003 || RETERMINAL_MODEL == 1004
  // A full E1003/E1004 RGB888 resize can consume most or all of the 8 MB
  // PSRAM. Resample and Bayer-dither directly into the indexed panel buffer.
  if (scaling) {
    LOG.printf("[render] %s direct indexed resize %dx%d -> %dx%d, %lu pixels\n",
               COLOR_MODE_NAME, image.width, image.height,
               layout.width, layout.height,
               static_cast<unsigned long>(pixelCount));
    const bool rendered = dither_resized_image(
        image.pixels, image.width, image.height, layout.width, layout.height,
        PANEL_PALETTE, config::DITHER_GAMMA, false, indices);
    image_free(&image);
    if (!rendered) {
      free(indices);
      return false;
    }
  } else
#endif
  {
    if (scaling) {
      logMemory("before resize");
      if (!resize_image(&image, layout.width, layout.height)) {
        free(indices);
        return false;
      }
    }
    LOG.printf("[render] %s Floyd-Steinberg dither, %lu pixels\n",
               COLOR_MODE_NAME, static_cast<unsigned long>(pixelCount));
    if (!dither_image(image.pixels, layout.width, layout.height, PANEL_PALETTE,
                      DITHER_FS, config::DITHER_GAMMA, false, indices)) {
      free(indices);
      return false;
    }
    image_free(&image);
  }

  pack4bppInPlace(indices, layout.width, layout.height);

  epaper.fillSprite(PANEL_WHITE);
  epaper.pushImage(layout.x, layout.y, layout.width, layout.height,
                   reinterpret_cast<uint16_t*>(indices));
  free(indices);

  epaper.fillRect(0, 0, config::PANEL_WIDTH, config::ui(44), PANEL_WHITE);
  fillStatusBackground(layout.footerDividerY,
                       config::PANEL_HEIGHT - layout.footerDividerY);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  const String heading =
      quietSleepNotice
          ? "XKCD #" + String(comic.number) + " - sleeping until " +
                quietEndLabel()
          : "XKCD #" + String(comic.number) + " - " + comic.title;
  selectTitleFont();
  epaper.drawString(ellipsize(heading, config::PANEL_WIDTH - config::ui(380), 1),
                    config::PANEL_WIDTH / 2, config::ui(24), 1);
  epaper.setFreeFont(nullptr);
  epaper.drawFastHLine(config::CONTENT_MARGIN_X, config::ui(43),
                       config::PANEL_WIDTH - 2 * config::CONTENT_MARGIN_X,
                       PANEL_BLACK);
  epaper.drawFastHLine(config::CONTENT_MARGIN_X, layout.footerDividerY,
                       config::PANEL_WIDTH - 2 * config::CONTENT_MARGIN_X,
                       PANEL_BLACK);

  selectFooterFont();
  epaper.setTextColor(PANEL_BLACK, PANEL_STATUS_BACKGROUND,
                      !PANEL_STATUS_DITHERED);
  epaper.setTextDatum(MC_DATUM);
  int footerY =
      (layout.footerDividerY + config::FOOTER_BOTTOM) / 2 -
      (layout.footerLineCount - 1) * config::FOOTER_LINE_HEIGHT / 2;
  for (int i = 0; i < layout.footerLineCount; ++i) {
    epaper.drawString(layout.footerLines[i], config::PANEL_WIDTH / 2, footerY, 1);
    footerY += config::FOOTER_LINE_HEIGHT;
  }
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  drawBadges(PANEL_WHITE, true);

  LOG.println("[render] refreshing panel");
  updatePanel();
  LOG.println("[render] complete");
  return true;
}

bool connectWifi() {
  if (strcmp(WIFI_SSID, "YOUR_WIFI_NAME") == 0) {
    LOG.println("[wifi] edit include/secrets.h first");
    return false;
  }
  WiFi.persistent(false);
  WiFi.setSleep(true);
  WiFi.mode(WIFI_STA);
#if LWIP_DHCP_GET_NTP_SRV
  // DHCP option 42 is accepted only when enabled before the lease is
  // acquired. Clear any server names left by an earlier fallback attempt so
  // synchronizeClock() can reliably tell whether DHCP supplied a server.
  if (esp_sntp_enabled()) esp_sntp_stop();
  for (uint8_t index = 0; index < SNTP_MAX_SERVERS; ++index)
    esp_sntp_setservername(index, nullptr);
  esp_sntp_servermode_dhcp(true);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < config::WIFI_TIMEOUT_MS &&
         !networkOperationShouldStop()) {
    delay(250);
  }
  if (networkOperationShouldStop()) {
    LOG.println("[wifi] connection cancelled");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG.println("[wifi] connection timed out");
    return false;
  }
  LOG.printf("[wifi] connected, IP=%s RSSI=%d\n",
             WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

void disableWifi() {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  LOG.println("[wifi] powered down");
}

void onNtpTimeSync(struct timeval*) {
  ntpSyncCompleted = true;
}

bool waitForNtpSync(uint32_t timeoutMs) {
  const uint32_t started = millis();
  while (!ntpSyncCompleted && millis() - started < timeoutMs) delay(50);
  return ntpSyncCompleted;
}

bool startDhcpNtpIfAvailable() {
#if LWIP_DHCP_GET_NTP_SRV
  const ip_addr_t* server = esp_sntp_getserver(0);
  if (server == nullptr || ip_addr_isany(server)) return false;

  char address[48] = {};
  ipaddr_ntoa_r(server, address, sizeof(address));
  LOG.printf("[ntp] trying DHCP server %s\n", address);
  ntpSyncCompleted = false;
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_init();
  if (waitForNtpSync(config::NTP_DHCP_TIMEOUT_MS)) return true;
  LOG.println("[ntp] DHCP server timed out; trying configured servers");
  return false;
#else
  return false;
#endif
}

bool synchronizeClock() {
  ntpSyncCompleted = false;
  sntp_set_time_sync_notification_cb(onNtpTimeSync);
  bool synchronized = startDhcpNtpIfAvailable();
  if (!synchronized) {
#if LWIP_DHCP_GET_NTP_SRV
    const ip_addr_t* dhcpServer = esp_sntp_getserver(0);
    if (dhcpServer == nullptr || ip_addr_isany(dhcpServer)) {
      LOG.println("[ntp] DHCP supplied no NTP server; using configured servers");
    }
#else
    LOG.println("[ntp] DHCP NTP is unavailable; using configured servers");
#endif
    ntpSyncCompleted = false;
    configTzTime(config::TIMEZONE, config::NTP_SERVER_PRIMARY,
                 config::NTP_SERVER_SECONDARY);
    synchronized = waitForNtpSync(config::NTP_SYNC_TIMEOUT_MS);
  }
  if (!synchronized) {
    LOG.println("[ntp] synchronization timed out; continuing");
    return false;
  }

  const time_t now = time(nullptr);
  struct tm localTime = {};
  if (now <= 0 || localtime_r(&now, &localTime) == nullptr) {
    LOG.println("[ntp] synchronized but local time conversion failed");
    return false;
  }
  char formatted[40] = {};
  strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S %Z",
           &localTime);
  lastNtpSyncEpoch = now;
  LOG.printf("[ntp] synchronized: %s\n", formatted);
  saveNtpTimeToHardwareRtc(now);
  return true;
}

void powerDownAndSleep(uint64_t sleepSeconds = config::SLEEP_SECONDS) {
  disableWifi();
  if (sdReady) SD.end();
  pinMode(PIN_SD_ENABLE, OUTPUT);
  digitalWrite(PIN_SD_ENABLE, LOW);
  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const uint32_t releaseStarted = millis();
  while ((!digitalRead(PIN_BUTTON_GREEN) || !digitalRead(PIN_BUTTON_RIGHT) ||
          !digitalRead(PIN_BUTTON_LEFT)) &&
         millis() - releaseStarted < 2000) {
    delay(10);
  }

  const int wakePins[] = {
      PIN_BUTTON_GREEN, PIN_BUTTON_RIGHT, PIN_BUTTON_LEFT};
  bool rtcPinsReady = true;
  for (const int pin : wakePins) {
    const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
    rtc_gpio_hold_dis(gpio);
    rtcPinsReady =
        rtc_gpio_init(gpio) == ESP_OK &&
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK &&
        rtc_gpio_pullup_en(gpio) == ESP_OK &&
        rtc_gpio_pulldown_dis(gpio) == ESP_OK &&
        rtcPinsReady;
  }

  const uint64_t wakeMask =
      (1ULL << PIN_BUTTON_GREEN) | (1ULL << PIN_BUTTON_RIGHT) |
      (1ULL << PIN_BUTTON_LEFT);
  const esp_err_t buttonWakeResult =
      rtcPinsReady
          ? esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  const esp_err_t timerWakeResult =
      esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  LOG.printf("[sleep] wake config: buttons=%s timer=%s levels=%d/%d/%d\n",
             esp_err_to_name(buttonWakeResult),
             esp_err_to_name(timerWakeResult),
             digitalRead(PIN_BUTTON_GREEN),
             digitalRead(PIN_BUTTON_RIGHT),
             digitalRead(PIN_BUTTON_LEFT));
  LOG.printf("[sleep] %llu seconds; GPIO3/GPIO4/GPIO5 wake enabled\n",
             static_cast<unsigned long long>(sleepSeconds));
  if (buttonWakeResult != ESP_OK && timerWakeResult != ESP_OK) {
    LOG.println("[sleep] no wake source could be configured; restarting");
    LOG.flush();
    delay(250);
    ESP.restart();
  }
  LOG.flush();
  delay(50);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  const uint64_t wakePins = wakeCause == ESP_SLEEP_WAKEUP_EXT1
                                ? esp_sleep_get_ext1_wakeup_status()
                                : 0;
  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUTTON_LEFT, INPUT_PULLUP);
  const bool greenWokeDevice =
      (wakePins & (1ULL << PIN_BUTTON_GREEN)) != 0;
  const bool rightWokeDevice =
      (wakePins & (1ULL << PIN_BUTTON_RIGHT)) != 0;
  const bool leftWokeDevice =
      (wakePins & (1ULL << PIN_BUTTON_LEFT)) != 0;
  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  const bool buttonWake = wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool timerWake = wakeCause == ESP_SLEEP_WAKEUP_TIMER;

  configureLocalTimezone();
  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  if (app_logic::startupBeepRequired(coldBoot, buttonWake)) {
    // Acknowledge cold boots and button wakes immediately. Holding the green
    // button through this beep and the interval below requests a screenshot.
    beep();
  }

  bool greenHeldLongEnough = false;
  if (greenWokeDevice) {
    const uint32_t holdStarted = millis();
    uint32_t releaseStarted = 0;
    while (millis() - holdStarted < config::SCREENSHOT_LONG_PRESS_MS) {
      if (!digitalRead(PIN_BUTTON_GREEN)) {
        releaseStarted = 0;
      } else if (releaseStarted == 0) {
        releaseStarted = millis();
      } else if (millis() - releaseStarted >=
                 config::BUTTON_RELEASE_DEBOUNCE_MS) {
        break;
      }
      delay(5);
    }
    greenHeldLongEnough =
        millis() - holdStarted >= config::SCREENSHOT_LONG_PRESS_MS;
  }
  screenshotRequested = greenHeldLongEnough;
  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone XKCD / %s\n", MODEL_NAME, COLOR_MODE_NAME);
  LOG.println("============================================");

  LOG.printf("[boot] wake cause=%d pins=0x%llx, PSRAM=%luK, "
             "GPIO3=%s GPIO4=%s GPIO5=%s\n",
             wakeCause, static_cast<unsigned long long>(wakePins),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024),
             greenWokeDevice
                 ? (greenHeldLongEnough ? "long-press" : "short-press")
                 : "idle",
             rightWokeDevice ? "wake" : "idle",
             leftWokeDevice ? "wake" : "idle");
  if (screenshotRequested) {
    LOG.println("[screenshot] green-button long press requested export");
    delay(80);
    beep();
  }

  const time_t startupTime = time(nullptr);
  const bool schedulingClockSuspicious =
      !clockIsValid() ||
      (lastNtpSyncEpoch > 0 && startupTime < lastNtpSyncEpoch) ||
      (lastArchiveRefreshEpoch > 0 &&
       startupTime < lastArchiveRefreshEpoch);
  const bool hardwareRtcCheckedEarly = schedulingClockSuspicious;
  if (schedulingClockSuspicious) {
    LOG.println("[rtc] schedule clock is invalid or behind retained state; "
                "trying PCF8563");
    restoreClockFromHardwareRtc();
  }

  bool wakeEventLogged = logWakeEvent(wakeCause, wakePins, false);
  const bool ntpDue = ntpRefreshDue(coldBoot);
  struct tm localTime = {};
  if (!coldBoot && !ntpDue && !buttonWake && localClock(localTime) &&
      quietHoursActive(localTime)) {
    const uint64_t quietSleepSeconds = secondsUntilQuietEnd(localTime);
    LOG.printf("[quiet] refresh suppressed; sleeping until %s\n",
               quietEndLabel().c_str());
    powerDownAndSleep(quietSleepSeconds);
    return;
  }

  randomSeed(esp_random());
  readSensors();

  if (coldBoot && !hardwareRtcCheckedEarly) {
    pcf8563::Reading storedRtc;
    readAndLogHardwareRtc(storedRtc);
  }

  epaper.begin();
  sdReady = mountSd();
  if (screenshotRequested && !sdReady) {
    LOG.println("[screenshot] request ignored: SD card is unavailable");
    screenshotRequested = false;
  }

  const uint32_t cachedComicCount = sdReady ? countCachedComics() : 0;
  cacheStatsAvailable = sdReady;
  cachedComicCountForDisplay = cachedComicCount;
  if (sdReady) {
    int cachedLatest = 0;
    getLatestNumber(false, cachedLatest);
  }
  const bool cacheOnly = app_logic::cacheOnly(
      sdReady, cachedComicCount, config::MIN_COMICS_FOR_CACHE_ONLY);

  // Archive maintenance is deliberately limited to normal timer wakes. Cold
  // boots only report cache progress. Once the cache has a useful local pool,
  // every kind of wake selects from it and button wakes do not start Wi-Fi.
  const bool networkPlanned =
      app_logic::networkPlanned(cacheOnly, ntpDue);
  if (sdReady && coldBoot) {
    LOG.printf("[cache] %lu complete comics available; display mode=%s\n",
               static_cast<unsigned long>(cachedComicCount),
               cacheOnly ? "local only" : "live until cache reaches threshold");
  }
  String connectionDetail;
  if (sdReady) {
    connectionDetail = String(cachedComicCount) + " comics cached";
    if (ntpDue) {
      connectionDetail += " - synchronizing clock";
    } else if (!cacheOnly) {
      connectionDetail += " - building local cache";
    }
  } else {
    connectionDetail = "No SD cache - downloading live";
  }

  const bool showConnectionStatus = coldBoot && networkPlanned;
  const String stationMac = wifiStationMacAddress();
  LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());

#if RETERMINAL_MODEL == 1001
  // UC8179 Gray4 initialization expects a native 1-bit update first. Rendering
  // the cold-boot status before switching modes makes that screen the required
  // first update instead of having the first Gray4 frame disappear.
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(WIFI_SSID), connectionDetail,
                 stationMac);
  }
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

#if RETERMINAL_MODEL != 1001
  if (showConnectionStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(WIFI_SSID), connectionDetail,
                 stationMac);
  }
#endif

  bool networkAvailable = false;
  if (networkPlanned) {
    networkAvailable = connectWifi();
  } else {
    LOG.println("[wifi] skipped; using the local XKCD cache");
  }
  const bool ntpSynchronized =
      networkAvailable && ntpDue && synchronizeClock();
  if (ntpDue && !ntpSynchronized && !coldBoot) {
    LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
    restoreClockFromHardwareRtc();
  }
  configureLocalTimezone();
  if (!wakeEventLogged) {
    logWakeEvent(wakeCause, wakePins, true);
  }

  // Establish or repair the six-hour baseline only after NTP/PCF recovery.
  // This prevents an invalid pre-sync clock from latching maintenance due.
  const time_t scheduleTime = time(nullptr);
  const time_t normalizedArchiveBaseline =
      static_cast<time_t>(app_logic::normalizeRefreshBaseline(
          coldBoot, clockIsValid(), scheduleTime,
          lastArchiveRefreshEpoch));
  if (normalizedArchiveBaseline != lastArchiveRefreshEpoch) {
    lastArchiveRefreshEpoch = normalizedArchiveBaseline;
    LOG.println("[cache] archive maintenance scheduled in six hours");
  }
  const bool archiveDue = app_logic::archiveMaintenanceDue(
      sdReady, timerWake, clockIsValid() && archiveRefreshDue());

  if (!coldBoot && !buttonWake && localClock(localTime) &&
      quietHoursActive(localTime)) {
    const uint64_t quietSleepSeconds = secondsUntilQuietEnd(localTime);
    LOG.printf("[quiet] refresh suppressed after clock sync; sleeping until %s\n",
               quietEndLabel().c_str());
    disableWifi();
    powerDownAndSleep(quietSleepSeconds);
    return;
  }

  Comic comic;
  RgbImage image;
  ImageLayout layout;
  bool displayed = false;
  bool acquired =
      sdReady
          ? acquireComic(cacheOnly ? false : networkAvailable,
                         comic, image, layout)
          : acquireComicWithoutSd(networkAvailable, comic, image, layout);

  if (sdReady && networkAvailable) {
    cachedComicCountForDisplay = countCachedComics();
  }

  // A replaced/empty/corrupt card may have no usable local comic even when
  // the six-hour maintenance interval has not elapsed. Recover live once only
  // while the card is still below the cache-only threshold.
  if (!acquired && sdReady && !cacheOnly && !networkAvailable) {
    LOG.println("[cache] no usable local comic; trying one live refresh");
    networkAvailable = connectWifi();
    if (networkAvailable) {
      if (ntpRefreshDue(coldBoot)) synchronizeClock();
      acquired = acquireComic(true, comic, image, layout);
    }
  }

  // PNG decoding is complete at this point. Keep the radio off during
  // dithering and the comparatively slow e-paper update.
  disableWifi();

  uint64_t nextSleepSeconds = config::SLEEP_SECONDS;
  if (localClock(localTime)) {
    if (quietHoursActive(localTime) ||
        nextWakeFallsInQuietHours(localTime, config::SLEEP_SECONDS)) {
      quietSleepNotice = true;
      nextSleepSeconds = secondsUntilQuietEnd(localTime);
      LOG.printf("[quiet] this is the final refresh; sleeping until %s\n",
                 quietEndLabel().c_str());
    }
  }

  if (acquired) {
    displayed = renderComic(comic, image, layout);
  }
  image_free(&image);

  if (!displayed) {
    const String reason = sdReady
                              ? "No usable cached or downloadable comic"
                              : "Live download failed; check Wi-Fi or insert an SD card";
    renderStatus("XKCD refresh failed", reason);
  }

  // The panel retains the newly rendered frame without power. Do scheduled
  // cache maintenance only now, so downloads never delay or replace the UI.
  if (archiveDue) {
    LOG.println("[cache] starting scheduled maintenance after panel refresh");
    armMaintenanceButtonCancellation();
    networkOperationDeadlineMs =
        millis() + config::ARCHIVE_MAINTENANCE_DEADLINE_MS;
    if (connectWifi()) {
      refreshArchiveCache();
      if (clockIsValid()) lastArchiveRefreshEpoch = time(nullptr);
    } else if (maintenanceCancellationRequested()) {
      LOG.println("[cache] scheduled maintenance cancelled by button");
    } else {
      LOG.println("[cache] scheduled maintenance deferred: Wi-Fi unavailable");
    }
    const bool maintenanceWasCancelled = maintenanceCancellationRequested();
    if (maintenanceWasCancelled && clockIsValid()) {
      // A user cancellation is intentional; do not retry maintenance on every
      // 15-minute timer wake.
      lastArchiveRefreshEpoch = time(nullptr);
    }
    disarmMaintenanceButtonCancellation();
    disableWifi();
    networkOperationDeadlineMs = 0;

    if (maintenanceWasCancelled) {
      LOG.println("[button] maintenance stopped; displaying another cached comic");
      beep();
      Comic replacement;
      RgbImage replacementImage;
      ImageLayout replacementLayout;
      if (acquireComic(false, replacement, replacementImage,
                       replacementLayout)) {
        renderComic(replacement, replacementImage, replacementLayout);
      } else {
        LOG.println("[button] no replacement cached comic was usable; "
                    "keeping the current frame");
      }
      image_free(&replacementImage);
    }
  }

  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
