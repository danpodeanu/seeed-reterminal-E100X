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
#include <math.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "app_logic.h"

#if RETERMINAL_MODEL == 1003
#include "fonts/Roboto_Bold90pt7b.h"
#elif RETERMINAL_MODEL == 1004
#include "fonts/Roboto_Bold72pt7b.h"
#else
#include "fonts/Roboto_Bold48pt7b.h"
#endif

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

#define LOG Serial1

#if RETERMINAL_MODEL == 1001
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_LIGHT = TFT_GRAY_2;
constexpr uint32_t PANEL_MUTED = TFT_GRAY_1;
constexpr uint32_t COLOR_SUN = TFT_GRAY_0;
constexpr uint32_t COLOR_RAIN = TFT_GRAY_1;
constexpr uint32_t COLOR_ALERT = TFT_GRAY_0;
constexpr char MODEL_NAME[] = "E1001";
constexpr char COLOR_MODE_NAME[] = "Gray4";
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
constexpr uint32_t PANEL_MUTED = TFT_BLACK;
constexpr uint32_t COLOR_SUN = TFT_YELLOW;
constexpr uint32_t COLOR_RAIN = TFT_BLUE;
constexpr uint32_t COLOR_ALERT = TFT_RED;
constexpr char MODEL_NAME[] = "E1002";
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint32_t PANEL_LIGHT = TFT_GRAY_12;
constexpr uint32_t PANEL_MUTED = TFT_GRAY_6;
constexpr uint32_t COLOR_SUN = TFT_GRAY_2;
constexpr uint32_t COLOR_RAIN = TFT_GRAY_5;
constexpr uint32_t COLOR_ALERT = TFT_GRAY_0;
constexpr char MODEL_NAME[] = "E1003";
constexpr char COLOR_MODE_NAME[] = "Gray16";
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint32_t PANEL_LIGHT = TFT_WHITE;
constexpr uint32_t PANEL_MUTED = TFT_BLACK;
constexpr uint32_t COLOR_SUN = TFT_YELLOW;
constexpr uint32_t COLOR_RAIN = TFT_BLUE;
constexpr uint32_t COLOR_ALERT = TFT_RED;
constexpr char MODEL_NAME[] = "E1004";
constexpr char COLOR_MODE_NAME[] = "six-color";
#endif

EPaper epaper;
Adafruit_SHT4x sht4;

bool sdReady = false;
bool screenshotRequested = false;
bool climateValid = false;
float indoorTemperatureC = NAN;
float indoorHumidityPct = NAN;
float batteryVoltage = NAN;
int batteryPct = -1;
volatile bool ntpSyncCompleted = false;
RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
bool quietSleepNotice = false;

struct DailyForecast {
  String date;
  int weatherCode = -1;
  float minimumC = NAN;
  float maximumC = NAN;
  float uvMaximum = NAN;
  int precipitationProbability = -1;
};

struct WeatherData {
  bool valid = false;
  bool fromCache = false;
  bool rainTimingAvailable = false;
  bool rainExpected = false;
  String updateTime;
  String nextRainTime;
  float temperatureC = NAN;
  float apparentC = NAN;
  float humidityPct = NAN;
  float windKmh = NAN;
  float nextRainMm = NAN;
  int nextRainProbability = -1;
  int weatherCode = -1;
  bool isDay = true;
  DailyForecast days[config::FORECAST_DAYS];
};

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
  constexpr uint32_t pixelOffset =
      fileHeaderSize + dibHeaderSize + paletteSize;

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
      return percents[i - 1] +
             fraction * (percents[i] - percents[i - 1]);
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
  batteryPct = constrain(
      static_cast<int>(batteryPercentForVoltage(batteryVoltage) + 0.5f),
      0, 100);
  LOG.printf("[sensor] battery %.3fV -> %d%%\n", batteryVoltage, batteryPct);

  climateValid = false;
  for (uint8_t attempt = 0;
       attempt < config::SENSOR_READ_ATTEMPTS && !climateValid; ++attempt) {
    if (attempt > 0) {
      Wire.end();
      delay(config::SENSOR_RETRY_DELAY_MS);
    }
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(100000);
    if (sht4.begin(&Wire)) {
      sht4.setPrecision(SHT4X_HIGH_PRECISION);
      sensors_event_t humidity;
      sensors_event_t temperature;
      if (sht4.getEvent(&humidity, &temperature)) {
        indoorTemperatureC = temperature.temperature;
        indoorHumidityPct = humidity.relative_humidity;
        climateValid = true;
        LOG.printf("[sensor] %.1fC %.0f%% RH (attempt %u)\n",
                   indoorTemperatureC, indoorHumidityPct, attempt + 1);
        break;
      }
    }
    LOG.printf("[sensor] SHT4x attempt %u/%u failed\n", attempt + 1,
               config::SENSOR_READ_ATTEMPTS);
  }
  if (!climateValid) LOG.println("[sensor] SHT4x unavailable after retries");
}

void selectSmallFont() {
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold12pt7b);
#else
  epaper.setFreeFont(&FreeSansBold9pt7b);
#endif
}

void selectMediumFont() {
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold24pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold18pt7b);
#else
  epaper.setFreeFont(&FreeSansBold12pt7b);
#endif
}

void selectLargeTemperatureFont() {
  // Each panel gets a native-resolution numeral font matching the physical
  // size of the formerly magnified 24-point font, without bitmap scaling.
  epaper.setTextSize(1);
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&Roboto_Bold90pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&Roboto_Bold72pt7b);
#else
  epaper.setFreeFont(&Roboto_Bold48pt7b);
#endif
}

String ellipsize(String text, int maxWidth, uint8_t font = 1) {
  if (epaper.textWidth(text, font) <= maxWidth) return text;
  const String suffix = "...";
  while (text.length() > 1 &&
         epaper.textWidth(text + suffix, font) > maxWidth) {
    text.remove(text.length() - 1);
  }
  text.trim();
  return text + suffix;
}

void drawBadges() {
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectSmallFont();

  const int statusCenterY = config::ui(24);
  const int edgeInset = config::ui(6);
  epaper.setTextDatum(ML_DATUM);
  String climate = "--.-C  --%";
  if (climateValid) {
    climate = String(indoorTemperatureC, 1) + "C  " +
              String(indoorHumidityPct, 0) + "%";
  }
  epaper.drawString(climate, edgeInset, statusCenterY, 1);

  const String percent = batteryPct >= 0 ? String(batteryPct) + "%" : "--%";
  const int w = config::ui(22);
  const int h = config::ui(12);
  const int terminalWidth = max(3, config::ui(5));
  const int x = config::PANEL_WIDTH - edgeInset - terminalWidth - w;
  const int gaugeCenterY = statusCenterY + 2;
  const int y = gaugeCenterY - h / 2;
  const int outline = max(1, config::ui(1));
  const int terminalHeight = max(3, config::ui(5));

  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(percent, x - config::ui(9), statusCenterY, 1);
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
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

String wifiStationMacAddress() {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return "unavailable";
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
    selectSmallFont();
    epaper.drawString(
        ellipsize(lineAbove, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 - config::ui(55), 1);
  }
  selectMediumFont();
  epaper.drawString(
      ellipsize(message, config::PANEL_WIDTH - config::ui(60)),
      config::PANEL_WIDTH / 2,
      config::PANEL_HEIGHT / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectSmallFont();
    epaper.drawString(
        ellipsize(detail, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 + config::ui(25), 1);
  }
  epaper.setTextSize(1);
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
    LOG.println("[sd] unavailable; weather will run without persistent cache");
    digitalWrite(PIN_SD_ENABLE, LOW);
    return false;
  }
  if (!SD.exists(config::CACHE_DIR) && !SD.mkdir(config::CACHE_DIR)) {
    LOG.println("[sd] could not create /weather; cache disabled");
    SD.end();
    digitalWrite(PIN_SD_ENABLE, LOW);
    return false;
  }
  LOG.printf("[sd] mounted, card=%lluMB\n",
             static_cast<unsigned long long>(
                 SD.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool readFile(const String& path, String& contents) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  contents = file.readString();
  file.close();
  return !contents.isEmpty();
}

bool writeFileAtomically(const String& path, const String& contents) {
  const String temporary = path + ".part";
  SD.remove(temporary);
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) {
    LOG.printf("[cache] could not create %s\n", temporary.c_str());
    return false;
  }
  const size_t written = file.print(contents);
  file.flush();
  file.close();
  if (written != contents.length()) {
    LOG.printf("[cache] short write for %s\n", path.c_str());
    SD.remove(temporary);
    return false;
  }
  SD.remove(path);
  if (!SD.rename(temporary, path)) {
    LOG.printf("[cache] could not install %s\n", path.c_str());
    SD.remove(temporary);
    return false;
  }
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
  // DHCP option 42 must be enabled before the station acquires its lease.
  // Remove any configured fallback servers left by a prior attempt so a
  // populated slot below unambiguously came from DHCP.
  if (esp_sntp_enabled()) esp_sntp_stop();
  for (uint8_t index = 0; index < SNTP_MAX_SERVERS; ++index)
    esp_sntp_setservername(index, nullptr);
  esp_sntp_servermode_dhcp(true);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - started < config::WIFI_TIMEOUT_MS) {
    delay(250);
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
  return true;
}

String forecastUrl() {
  String url =
      "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(config::LATITUDE, 4);
  url += "&longitude=";
  url += String(config::LONGITUDE, 4);
  url +=
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
      "is_day,weather_code,wind_speed_10m"
      "&hourly=precipitation_probability,precipitation,rain,showers"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
      "uv_index_max,precipitation_probability_max"
      "&temperature_unit=celsius&wind_speed_unit=kmh&timezone=auto"
      "&forecast_hours=";
  url += String(config::RAIN_FORECAST_HOURS);
  url += "&forecast_days=";
  url += String(config::FORECAST_DAYS);
  return url;
}

bool parseWeather(const String& body, WeatherData& weather) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, body);
  if (error) {
    LOG.printf("[weather] JSON: %s\n", error.c_str());
    return false;
  }
  if (document["error"] | false) {
    LOG.printf("[weather] API error: %s\n",
               String(document["reason"] | "unknown").c_str());
    return false;
  }
  JsonObject current = document["current"];
  JsonObject daily = document["daily"];
  JsonArray dates = daily["time"];
  JsonArray codes = daily["weather_code"];
  JsonArray maxima = daily["temperature_2m_max"];
  JsonArray minima = daily["temperature_2m_min"];
  JsonArray uv = daily["uv_index_max"];
  JsonArray rain = daily["precipitation_probability_max"];
  if (current.isNull() || dates.size() < config::FORECAST_DAYS ||
      codes.size() < config::FORECAST_DAYS ||
      maxima.size() < config::FORECAST_DAYS ||
      minima.size() < config::FORECAST_DAYS ||
      uv.size() < config::FORECAST_DAYS ||
      rain.size() < config::FORECAST_DAYS) {
    LOG.println("[weather] response is missing required fields");
    return false;
  }

  weather.temperatureC = current["temperature_2m"] | NAN;
  weather.apparentC = current["apparent_temperature"] | NAN;
  weather.humidityPct = current["relative_humidity_2m"] | NAN;
  weather.windKmh = current["wind_speed_10m"] | NAN;
  weather.weatherCode = current["weather_code"] | -1;
  weather.isDay = (current["is_day"] | 1) != 0;
  weather.updateTime = String(current["time"] | "");
  weather.rainTimingAvailable = false;
  weather.rainExpected = false;
  weather.nextRainTime = "";
  weather.nextRainMm = NAN;
  weather.nextRainProbability = -1;

  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    weather.days[i].date = String(dates[i] | "");
    weather.days[i].weatherCode = codes[i] | -1;
    weather.days[i].maximumC = maxima[i] | NAN;
    weather.days[i].minimumC = minima[i] | NAN;
    weather.days[i].uvMaximum = uv[i] | NAN;
    weather.days[i].precipitationProbability = rain[i] | -1;
  }

  JsonObject hourly = document["hourly"];
  JsonArray hourlyTimes = hourly["time"];
  JsonArray hourlyProbability = hourly["precipitation_probability"];
  JsonArray hourlyPrecipitation = hourly["precipitation"];
  JsonArray hourlyRain = hourly["rain"];
  JsonArray hourlyShowers = hourly["showers"];
  const size_t hourlyCount =
      min(hourlyTimes.size(), hourlyPrecipitation.size());
  if (!hourly.isNull() && hourlyCount > 0) {
    weather.rainTimingAvailable = true;
    for (size_t i = 0; i < hourlyCount; ++i) {
      const String slotTime = String(hourlyTimes[i] | "");
      if (slotTime.isEmpty() ||
          (!weather.updateTime.isEmpty() &&
           strcmp(slotTime.c_str(), weather.updateTime.c_str()) <= 0)) {
        continue;
      }

      const float precipitation = hourlyPrecipitation[i] | 0.0f;
      const float rainAmount =
          i < hourlyRain.size() ? hourlyRain[i] | 0.0f : 0.0f;
      const float showerAmount =
          i < hourlyShowers.size() ? hourlyShowers[i] | 0.0f : 0.0f;
      const float liquidRain =
          max(precipitation, rainAmount + showerAmount);
      const int probability =
          i < hourlyProbability.size() && !hourlyProbability[i].isNull()
              ? hourlyProbability[i].as<int>()
              : -1;
      const bool probabilityQualifies =
          probability < 0 ||
          probability >= config::RAIN_PROBABILITY_THRESHOLD;
      if (liquidRain >= config::RAIN_START_THRESHOLD_MM &&
          probabilityQualifies) {
        weather.rainExpected = true;
        weather.nextRainTime = slotTime;
        weather.nextRainMm = liquidRain;
        weather.nextRainProbability = probability;
        break;
      }
    }
  }

  weather.valid = isfinite(weather.temperatureC) &&
                  isfinite(weather.apparentC) &&
                  isfinite(weather.humidityPct) &&
                  weather.weatherCode >= 0;
  if (!weather.valid) {
    LOG.println("[weather] current values are invalid");
    return false;
  }
  LOG.printf("[weather] %.1fC, feels %.1fC, %.0f%% RH, code=%d\n",
             weather.temperatureC, weather.apparentC, weather.humidityPct,
             weather.weatherCode);
  if (weather.rainExpected) {
    LOG.printf("[weather] next rain around %s, %.1fmm, probability=%d%%\n",
               weather.nextRainTime.c_str(), weather.nextRainMm,
               weather.nextRainProbability);
  } else if (weather.rainTimingAvailable) {
    LOG.printf("[weather] no qualifying rain in the next %u hours\n",
               config::RAIN_FORECAST_HOURS);
  } else {
    LOG.println("[weather] hourly rain timing unavailable");
  }
  return true;
}

bool fetchWeather(WeatherData& weather, String& responseBody,
                  bool bypassHttpCache = false) {
  responseBody = "";
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const String url = forecastUrl();
  if (!http.begin(client, url)) {
    LOG.println("[weather] could not start HTTPS request");
    return false;
  }
  if (bypassHttpCache) {
    http.addHeader("Cache-Control", "no-cache, no-store");
    http.addHeader("Pragma", "no-cache");
    LOG.println("[weather] button wake: forcing live API refresh");
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[weather] HTTP GET -> %d\n", status);
    http.end();
    return false;
  }
  responseBody = http.getString();
  http.end();
  LOG.printf("[weather] received %u bytes from Open-Meteo\n",
             static_cast<unsigned>(responseBody.length()));
  weather.fromCache = false;
  return parseWeather(responseBody, weather);
}

bool loadCachedWeather(WeatherData& weather) {
  String body;
  if (!sdReady || !readFile(config::FORECAST_CACHE, body)) return false;
  if (!parseWeather(body, weather)) {
    LOG.println("[cache] saved forecast is invalid");
    return false;
  }
  weather.fromCache = true;
  LOG.printf("[cache] using forecast updated %s\n",
             weather.updateTime.c_str());
  return true;
}

String conditionName(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
    return "Rain";
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))
    return "Snow";
  if (code >= 95) return "Thunderstorm";
  return "Mixed weather";
}

String uvDescription(float uv) {
  if (!isfinite(uv)) return "--";
  if (uv < 3.0f) return "Low";
  if (uv < 6.0f) return "Moderate";
  if (uv < 8.0f) return "High";
  if (uv < 11.0f) return "Very high";
  return "Extreme";
}

String updateClock(const String& isoTime) {
  const int separator = isoTime.indexOf('T');
  if (separator >= 0 && isoTime.length() >= static_cast<size_t>(separator + 6))
    return isoTime.substring(separator + 1, separator + 6);
  return "";
}

String updateStamp(const String& isoTime) {
  const String clock = updateClock(isoTime);
  if (isoTime.length() >= 10 && !clock.isEmpty()) {
    return isoTime.substring(5, 10) + " " + clock;
  }
  return clock;
}

String dayLabel(uint8_t index, const String& date) {
  if (index == 0) return "Today";
  if (index == 1) return "Tomorrow";
  if (date.length() >= 10) return date.substring(5);
  return "Day " + String(index + 1);
}

String nextRainWhen(const WeatherData& weather) {
  const String clock = updateClock(weather.nextRainTime);
  if (clock.isEmpty()) return "";
  if (weather.nextRainTime.length() >= 10) {
    const String date = weather.nextRainTime.substring(0, 10);
    if (!weather.days[0].date.isEmpty() && date == weather.days[0].date)
      return clock;
    if (!weather.days[1].date.isEmpty() && date == weather.days[1].date)
      return "tomorrow " + clock;
    return weather.nextRainTime.substring(5, 10) + " " + clock;
  }
  return clock;
}

String rainSummary(const WeatherData& weather) {
  if (weather.rainExpected) {
    const String when = nextRainWhen(weather);
    if (when.startsWith("tomorrow "))
      return "Rain tomorrow " + when.substring(9);
    String summary = "Rain " + when;
    if (weather.nextRainProbability >= 0) {
      summary += " (" + String(weather.nextRainProbability) + "%)";
    }
    return summary;
  }
  if (weather.rainTimingAvailable) {
    return "No rain forecast in " +
           String(config::RAIN_FORECAST_HOURS) + "h";
  }
  return "";
}

void thickLine(int x1, int y1, int x2, int y2, int thickness,
               uint32_t color) {
  if (thickness <= 1) {
    epaper.drawLine(x1, y1, x2, y2, color);
    return;
  }
  // Axis-aligned rays use rectangles so their opposing directions have
  // exactly the same rasterized width. Triangle edge rules can otherwise
  // make a right-to-left horizontal line one pixel thinner.
  if (y1 == y2) {
    const int left = min(x1, x2);
    epaper.fillRect(left, y1 - thickness / 2,
                    abs(x2 - x1) + 1, thickness, color);
    return;
  }
  if (x1 == x2) {
    const int top = min(y1, y2);
    epaper.fillRect(x1 - thickness / 2, top,
                    thickness, abs(y2 - y1) + 1, color);
    return;
  }
  const float dx = static_cast<float>(x2 - x1);
  const float dy = static_cast<float>(y2 - y1);
  const float length = sqrtf(dx * dx + dy * dy);
  if (length < 1.0f) {
    epaper.fillCircle(x1, y1, max(1, thickness / 2), color);
    return;
  }
  const float half = thickness / 2.0f;
  const int px = static_cast<int>(roundf(-dy * half / length));
  const int py = static_cast<int>(roundf(dx * half / length));
  epaper.fillTriangle(x1 + px, y1 + py, x1 - px, y1 - py,
                      x2 + px, y2 + py, color);
  epaper.fillTriangle(x1 - px, y1 - py, x2 - px, y2 - py,
                      x2 + px, y2 + py, color);
}

void drawSun(int cx, int cy, int radius, uint32_t color) {
  // Keep the solar disc open and bright, with the panel color used only for
  // its outline and rays.
  epaper.fillCircle(cx, cy, radius, PANEL_WHITE);
  const int outline = max(1, radius / 12);
  for (int inset = 0; inset < outline; ++inset) {
    epaper.drawCircle(cx, cy, radius - inset, color);
  }
  const int inner = radius + max(3, radius / 2);
  const int outer = radius + max(7, radius);
  const int thickness = max(1, radius / 8);
  for (int angle = 0; angle < 360; angle += 45) {
    const float radians = angle * PI / 180.0f;
    thickLine(cx + cosf(radians) * inner, cy + sinf(radians) * inner,
              cx + cosf(radians) * outer, cy + sinf(radians) * outer,
              thickness, color);
  }
}

void drawMoon(int cx, int cy, int radius) {
  epaper.fillCircle(cx, cy, radius, PANEL_BLACK);
  epaper.fillCircle(cx + radius / 2, cy - radius / 3, radius, PANEL_WHITE);
}

void drawCloud(int cx, int cy, int size) {
  const int baseY = cy + size / 7;
  epaper.fillCircle(cx - size / 5, cy, size / 5, PANEL_LIGHT);
  epaper.fillCircle(cx + size / 10, cy - size / 8, size / 4, PANEL_LIGHT);
  epaper.fillCircle(cx + size / 3, cy + size / 20, size / 6, PANEL_LIGHT);
  epaper.fillRect(cx - size / 3, cy, size * 2 / 3, size / 4, PANEL_LIGHT);
  epaper.drawCircle(cx - size / 5, cy, size / 5, PANEL_BLACK);
  epaper.drawCircle(cx + size / 10, cy - size / 8, size / 4, PANEL_BLACK);
  epaper.drawCircle(cx + size / 3, cy + size / 20, size / 6, PANEL_BLACK);
  thickLine(cx - size / 3, baseY, cx + size / 2, baseY,
            max(1, size / 25), PANEL_BLACK);
}

void drawWeatherIcon(int cx, int cy, int size, int code, bool isDay = true) {
  // For a clear sky, the ray tips now span approximately the full requested
  // size instead of occupying less than half of the icon box.
  const int radius = max(4, size / 4);
  if (code == 0) {
    if (isDay) drawSun(cx, cy, radius, COLOR_SUN);
    else drawMoon(cx, cy, radius);
    return;
  }
  if (code == 1 || code == 2) {
    drawSun(cx - size / 5, cy - size / 5, max(3, radius * 3 / 4),
            COLOR_SUN);
    drawCloud(cx + size / 10, cy + size / 10, size);
    return;
  }

  drawCloud(cx, cy - size / 8, size);
  if (code == 45 || code == 48) {
    for (int i = 0; i < 3; ++i) {
      const int y = cy + size / 4 + i * max(3, size / 8);
      thickLine(cx - size / 3, y, cx + size / 3, y,
                max(1, size / 30), PANEL_MUTED);
    }
  } else if ((code >= 51 && code <= 67) ||
             (code >= 80 && code <= 82)) {
    for (int i = -1; i <= 1; ++i) {
      const int x = cx + i * size / 4;
      thickLine(x, cy + size / 5, x - size / 12, cy + size / 2,
                max(1, size / 24), COLOR_RAIN);
    }
  } else if ((code >= 71 && code <= 77) ||
             (code >= 85 && code <= 86)) {
    for (int i = -1; i <= 1; ++i) {
      const int x = cx + i * size / 4;
      const int y = cy + size / 3;
      thickLine(x - size / 12, y, x + size / 12, y,
                max(1, size / 30), COLOR_RAIN);
      thickLine(x, y - size / 12, x, y + size / 12,
                max(1, size / 30), COLOR_RAIN);
    }
  } else if (code >= 95) {
    const int top = cy + size / 8;
    epaper.fillTriangle(cx, top, cx - size / 8, top + size / 4,
                        cx + size / 16, top + size / 4, COLOR_ALERT);
    epaper.fillTriangle(cx + size / 16, top + size / 4,
                        cx - size / 16, top + size / 2,
                        cx + size / 5, top + size / 5, COLOR_ALERT);
  }
}

void drawLargeTemperature(float temperature, int cx, int cy) {
  const int rounded = static_cast<int>(roundf(temperature));
  const bool negative = rounded < 0;
  const String value = String(abs(rounded));
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  selectLargeTemperatureFont();
  const int textWidth = epaper.textWidth(value, 1);
  const int textHeight = epaper.fontHeight(1);
  const int minusWidth = negative ? max(4, textHeight / 3) : 0;
  const int minusGap = negative ? max(2, textHeight / 12) : 0;
  const int valueCenterX = cx + (minusWidth + minusGap) / 2;
  epaper.drawString(value, valueCenterX, cy, 1);
  if (negative) {
    const int totalWidth = minusWidth + minusGap + textWidth;
    const int minusLeft = cx - totalWidth / 2;
    thickLine(minusLeft, cy, minusLeft + minusWidth, cy,
              max(2, textHeight / 18), PANEL_BLACK);
  }
  const int degreeRadius = max(3, textHeight / 15);
  epaper.drawCircle(valueCenterX + textWidth / 2 + degreeRadius * 2,
                    cy - textHeight / 3, degreeRadius, PANEL_BLACK);
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(1);
}

void drawHeader(const WeatherData& weather) {
  drawBadges();
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  selectSmallFont();
  String heading;
  const String stamp = updateStamp(weather.updateTime);
  if (!stamp.isEmpty()) {
    heading = "Weather at " + stamp;
  } else {
    heading = "Weather";
  }
  if (quietSleepNotice) {
    heading = "Weather - sleeping until " + quietEndLabel();
  }
  epaper.drawString(
      ellipsize(heading, config::PANEL_WIDTH - config::ui(380)),
      config::PANEL_WIDTH / 2, config::ui(24), 1);
  epaper.drawFastHLine(config::ui(10), config::ui(44),
                       config::PANEL_WIDTH - config::ui(20), PANEL_BLACK);
}

void drawForecastCard(const DailyForecast& day, uint8_t index,
                      int left, int top, int width, int height) {
  const int centerX = left + width / 2;
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(TC_DATUM);
  selectMediumFont();
  epaper.drawString(dayLabel(index, day.date), centerX,
                    top + config::ui(7), 1);

  const int iconSize = min(width / 7, config::ui(32));
  drawWeatherIcon(centerX, top + config::ui(55), iconSize,
                  day.weatherCode, true);

  selectSmallFont();
  epaper.drawString(
      ellipsize(conditionName(day.weatherCode), width - config::ui(12)),
      centerX, top + config::ui(83), 1);
  const String range =
      String(static_cast<int>(roundf(day.minimumC))) + "C  /  " +
      String(static_cast<int>(roundf(day.maximumC))) + "C";
  epaper.drawString(range, centerX, top + config::ui(107), 1);
  const String extra =
      "Rain " + String(day.precipitationProbability) + "%   UV " +
      String(day.uvMaximum, 1) + " " + uvDescription(day.uvMaximum);
  epaper.drawString(ellipsize(extra, width - config::ui(12)), centerX,
                    top + config::ui(130), 1);
}

void renderLandscape(const WeatherData& weather) {
  const int mainTop = config::ui(48);
  const int mainBottom = config::PANEL_HEIGHT * 62 / 100;
  const int mainCenterY = (mainTop + mainBottom) / 2;
  const int iconX = config::PANEL_WIDTH * 19 / 100;
  const int temperatureX = config::PANEL_WIDTH * 49 / 100;
  const int detailX = config::PANEL_WIDTH * 83 / 100;

  drawWeatherIcon(iconX, mainCenterY - config::ui(4),
                  min(config::PANEL_WIDTH * 27 / 100,
                      (mainBottom - mainTop) * 72 / 100),
                  weather.weatherCode, weather.isDay);
  epaper.drawFastVLine(config::PANEL_WIDTH * 34 / 100,
                       mainTop + config::ui(12),
                       mainBottom - mainTop - config::ui(24), PANEL_MUTED);

  drawLargeTemperature(weather.temperatureC, temperatureX,
                       mainCenterY - config::ui(13));
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(TC_DATUM);
  selectSmallFont();
  epaper.drawString("Outdoor temperature", temperatureX,
                    mainCenterY + config::ui(57), 1);
  epaper.drawString(conditionName(weather.weatherCode), temperatureX,
                    mainCenterY + config::ui(82), 1);

  epaper.drawFastVLine(config::PANEL_WIDTH * 66 / 100,
                       mainTop + config::ui(12),
                       mainBottom - mainTop - config::ui(24), PANEL_MUTED);
  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(
      String(static_cast<int>(roundf(weather.apparentC))) + "C",
      detailX, mainCenterY - config::ui(66), 1);
  selectSmallFont();
  epaper.drawString("Feels like", detailX,
                    mainCenterY - config::ui(40), 1);

  selectMediumFont();
  epaper.drawString(
      String(static_cast<int>(roundf(weather.humidityPct))) + "%",
      detailX, mainCenterY + config::ui(5), 1);
  selectSmallFont();
  epaper.drawString("Outdoor humidity", detailX,
                    mainCenterY + config::ui(31), 1);

  const int detailWidth = config::PANEL_WIDTH * 31 / 100;
  epaper.drawString(
      ellipsize(rainSummary(weather), detailWidth),
      detailX, mainCenterY + config::ui(67), 1);
  epaper.drawString(
      "Wind " + String(weather.windKmh, 0) + " km/h", detailX,
      mainCenterY + config::ui(101), 1);

  epaper.drawFastHLine(config::ui(10), mainBottom,
                       config::PANEL_WIDTH - config::ui(20), PANEL_MUTED);
  const int forecastTop = mainBottom + config::ui(4);
  const int footerTop = config::PANEL_HEIGHT - config::ui(30);
  const int cardWidth =
      (config::PANEL_WIDTH - config::ui(20)) / config::FORECAST_DAYS;
  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    const int left = config::ui(10) + i * cardWidth;
    if (i > 0) {
      epaper.drawFastVLine(left, forecastTop + config::ui(8),
                           footerTop - forecastTop - config::ui(12),
                           PANEL_LIGHT);
    }
    drawForecastCard(weather.days[i], i, left, forecastTop, cardWidth,
                     footerTop - forecastTop);
  }
}

void drawPortraitForecastRow(const DailyForecast& day, uint8_t index,
                             int top, int height) {
  const int margin = config::ui(14);
  const int iconX = config::PANEL_WIDTH * 18 / 100;
  const int textX = config::PANEL_WIDTH * 37 / 100;
  const int valuesX = config::PANEL_WIDTH * 78 / 100;
  const int centerY = top + height / 2;

  epaper.drawFastHLine(margin, top, config::PANEL_WIDTH - 2 * margin,
                       PANEL_LIGHT);
  drawWeatherIcon(iconX, centerY, min(height / 5, config::ui(40)),
                  day.weatherCode, true);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(ML_DATUM);
  selectMediumFont();
  epaper.drawString(dayLabel(index, day.date), textX,
                    centerY - config::ui(28), 1);
  selectSmallFont();
  epaper.drawString(
      ellipsize(conditionName(day.weatherCode),
                config::PANEL_WIDTH * 36 / 100),
      textX, centerY + config::ui(4), 1);
  epaper.drawString(
      "Rain " + String(day.precipitationProbability) + "%  UV " +
          String(day.uvMaximum, 1),
      textX, centerY + config::ui(31), 1);

  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(
      String(static_cast<int>(roundf(day.minimumC))) + "C / " +
          String(static_cast<int>(roundf(day.maximumC))) + "C",
      valuesX, centerY - config::ui(8), 1);
  selectSmallFont();
  epaper.drawString("Low / High", valuesX,
                    centerY + config::ui(25), 1);
}

void renderPortrait(const WeatherData& weather) {
  const int mainTop = config::ui(50);
  const int mainBottom = config::PANEL_HEIGHT * 43 / 100;
  const int mainCenterY = (mainTop + mainBottom) / 2;

  drawWeatherIcon(config::PANEL_WIDTH * 24 / 100,
                  mainCenterY - config::ui(25),
                  min(config::PANEL_WIDTH * 35 / 100,
                      (mainBottom - mainTop) * 72 / 100),
                  weather.weatherCode, weather.isDay);
  drawLargeTemperature(weather.temperatureC,
                       config::PANEL_WIDTH * 67 / 100,
                       mainCenterY - config::ui(38));

  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  selectMediumFont();
  epaper.drawString(conditionName(weather.weatherCode),
                    config::PANEL_WIDTH / 2,
                    mainCenterY + config::ui(72), 1);
  selectSmallFont();
  const String details =
      "Feels " + String(weather.apparentC, 0) + "C   Humidity " +
      String(weather.humidityPct, 0) + "%   Wind " +
      String(weather.windKmh, 0) + " km/h";
  epaper.drawString(
      ellipsize(details, config::PANEL_WIDTH - config::ui(40)),
      config::PANEL_WIDTH / 2, mainCenterY + config::ui(108), 1);
  epaper.drawString(
      ellipsize(rainSummary(weather),
                config::PANEL_WIDTH - config::ui(40)),
      config::PANEL_WIDTH / 2, mainCenterY + config::ui(142), 1);

  const int footerTop = config::PANEL_HEIGHT - config::ui(34);
  const int rowHeight =
      (footerTop - mainBottom) / config::FORECAST_DAYS;
  for (uint8_t i = 0; i < config::FORECAST_DAYS; ++i) {
    drawPortraitForecastRow(weather.days[i], i,
                            mainBottom + i * rowHeight, rowHeight);
  }
}

void renderFooter() {
  const int top = config::PANEL_HEIGHT - config::ui(30);
  epaper.drawFastHLine(config::ui(10), top,
                       config::PANEL_WIDTH - config::ui(20), PANEL_MUTED);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectSmallFont();
  epaper.setTextDatum(ML_DATUM);
  epaper.drawString("Weather data: Open-Meteo", config::ui(12),
                    config::PANEL_HEIGHT - config::ui(14), 1);
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(config::LOCATION_NAME,
                    config::PANEL_WIDTH - config::ui(12),
                    config::PANEL_HEIGHT - config::ui(14), 1);
}

void renderWeather(const WeatherData& weather) {
  epaper.fillSprite(PANEL_WHITE);
  drawHeader(weather);
#if RETERMINAL_MODEL == 1004
  renderPortrait(weather);
#else
  renderLandscape(weather);
#endif
  renderFooter();
  epaper.setTextSize(1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  LOG.println("[render] refreshing weather panel");
  updatePanel();
  LOG.println("[render] complete");
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
  while ((!digitalRead(PIN_BUTTON_GREEN) ||
          !digitalRead(PIN_BUTTON_RIGHT) ||
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
  const esp_sleep_wakeup_cause_t wakeCause =
      esp_sleep_get_wakeup_cause();
  const bool buttonWake = wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  const uint64_t wakePins =
      buttonWake
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
  LOG.printf(" reTerminal %s standalone Weather / %s\n",
             MODEL_NAME, COLOR_MODE_NAME);
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

  configureLocalTimezone();
  bool wakeEventLogged = logWakeEvent(wakeCause, wakePins, false);
  const bool ntpDue = ntpRefreshDue(coldBoot);
  struct tm localTime = {};
  const bool haveLocalTime = localClock(localTime);
  if (app_logic::suppressForQuietHours(
          coldBoot, buttonWake, ntpDue, haveLocalTime,
          haveLocalTime && quietHoursActive(localTime))) {
    const uint64_t quietSleepSeconds = secondsUntilQuietEnd(localTime);
    LOG.printf("[quiet] refresh suppressed; sleeping until %s\n",
               quietEndLabel().c_str());
    powerDownAndSleep(quietSleepSeconds);
    return;
  }

  readSensors();
  epaper.begin();
  sdReady = mountSd();
  if (screenshotRequested && !sdReady) {
    LOG.println("[screenshot] request ignored: SD card is unavailable");
    screenshotRequested = false;
  }

  const bool showConnectionStatus = coldBoot;
  const String connectionDetail =
      sdReady
          ? (SD.exists(config::FORECAST_CACHE)
                 ? "Saved forecast available"
                 : "SD cache ready for the first forecast")
          : "No SD cache - using live weather";
  const String stationMac = wifiStationMacAddress();
  LOG.printf("[wifi] station MAC=%s\n", stationMac.c_str());

#if RETERMINAL_MODEL == 1001
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

  WeatherData weather;
  const bool networkAvailable = connectWifi();
  if (networkAvailable && ntpDue) synchronizeClock();
  configureLocalTimezone();
  if (!wakeEventLogged) {
    logWakeEvent(wakeCause, wakePins, true);
  }

  if (!coldBoot && !buttonWake && localClock(localTime) &&
      quietHoursActive(localTime)) {
    const uint64_t quietSleepSeconds = secondsUntilQuietEnd(localTime);
    LOG.printf("[quiet] refresh suppressed after clock sync; sleeping until %s\n",
               quietEndLabel().c_str());
    disableWifi();
    powerDownAndSleep(quietSleepSeconds);
    return;
  }

  String liveResponse;
  const bool liveUpdated =
      networkAvailable &&
      fetchWeather(weather, liveResponse, buttonWake);
  // The response has already been parsed. Do not keep the radio associated
  // while writing the cache, composing the frame, or refreshing the panel.
  disableWifi();

  if (liveUpdated && sdReady) {
    if (writeFileAtomically(config::FORECAST_CACHE, liveResponse)) {
      LOG.println("[cache] saved latest forecast");
    } else {
      LOG.println("[cache] forecast not stored; continuing with live data");
    }
  }
  const bool haveWeather = liveUpdated || loadCachedWeather(weather);
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

  if (haveWeather) {
    renderWeather(weather);
  } else if (showConnectionStatus) {
    renderStatus("Weather unavailable",
                 networkAvailable ? "Forecast download failed"
                                  : "Check Wi-Fi configuration");
  } else {
    LOG.println("[weather] refresh failed; retaining previous panel image");
    if (screenshotRequested) {
      LOG.println("[screenshot] not saved: no weather frame was rendered");
      screenshotRequested = false;
    }
  }

  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
