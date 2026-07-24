#include <Arduino.h>
#include <Network.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <TFT_eSPI.h>
#include <driver/rtc_io.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"
#include "app_logic.h"
#include "image_loader.h"
#include "pcf8563_utc.h"
#include "secrets.h"
#include "timestamped_logger.h"

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
constexpr int PIN_KEY0 = 3;
constexpr int PIN_KEY1 = 4;
constexpr int PIN_KEY2 = 5;
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
constexpr uint8_t PANEL_WHITE_CODE = 0x3;
constexpr char MODEL_NAME[] = "E1001";
constexpr char COLOR_MODE_NAME[] = "Gray4";
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint8_t PANEL_WHITE_CODE = 0x0;
constexpr char MODEL_NAME[] = "E1002";
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint8_t PANEL_WHITE_CODE = 0xF;
constexpr char MODEL_NAME[] = "E1003";
constexpr char COLOR_MODE_NAME[] = "Gray16";
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint8_t PANEL_WHITE_CODE = 0x0;
constexpr char MODEL_NAME[] = "E1004";
constexpr char COLOR_MODE_NAME[] = "six-color";
#endif

constexpr uint8_t BAYER8[64] = {
    0, 48, 12, 60, 3, 51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31,
    8, 56, 4, 52, 11, 59, 7, 55, 40, 24, 36, 20, 43, 27, 39, 23,
    2, 50, 14, 62, 1, 49, 13, 61, 34, 18, 46, 30, 33, 17, 45, 29,
    10, 58, 6, 54, 9, 57, 5, 53, 42, 26, 38, 22, 41, 25, 37, 21,
};

struct Rgb {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

constexpr Rgb E6_COLORS[6] = {
    {255, 255, 255}, {29, 185, 84}, {229, 57, 53},
    {255, 216, 0},   {0, 76, 255},  {0, 0, 0},
};
constexpr uint8_t E6_CODES[6] = {0x0, 0x2, 0x6, 0xB, 0xD, 0xF};

EPaper epaper;
Adafruit_SHT4x sht4;

bool sdReady = false;
bool climateValid = false;
bool i2cReady = false;
volatile bool ntpSyncCompleted = false;
float temperatureC = NAN;
float humidityPct = NAN;
float batteryVoltage = NAN;
int batteryPct = -1;

RTC_DATA_ATTR time_t lastNtpSyncEpoch = 0;
RTC_DATA_ATTR int32_t currentPhotoIndex = -1;

uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

int32_t readLeS32(const uint8_t* bytes) {
  return static_cast<int32_t>(readLe32(bytes));
}

int clampByte(int value) {
  return value < 0 ? 0 : (value > 255 ? 255 : value);
}

int luminance(uint8_t red, uint8_t green, uint8_t blue) {
  return (2126 * red + 7152 * green + 722 * blue + 5000) / 10000;
}

int rgbDistanceSquared(const Rgb& first, const Rgb& second) {
  const int red = static_cast<int>(first.red) - second.red;
  const int green = static_cast<int>(first.green) - second.green;
  const int blue = static_cast<int>(first.blue) - second.blue;
  return red * red + green * green + blue * blue;
}

uint8_t panelCodeForRgb(uint8_t red, uint8_t green, uint8_t blue) {
#if RETERMINAL_MODEL == 1001
  return static_cast<uint8_t>(
      constrain((luminance(red, green, blue) + 42) / 85, 0, 3));
#elif RETERMINAL_MODEL == 1003
  return static_cast<uint8_t>(
      constrain((luminance(red, green, blue) + 8) / 17, 0, 15));
#else
  const Rgb input = {red, green, blue};
  int best = 0;
  int bestDistance = rgbDistanceSquared(input, E6_COLORS[0]);
  for (int index = 1; index < 6; ++index) {
    const int distance = rgbDistanceSquared(input, E6_COLORS[index]);
    if (distance < bestDistance) {
      best = index;
      bestDistance = distance;
    }
  }
  return E6_CODES[best];
#endif
}

uint8_t fallbackPanelCode(uint8_t red, uint8_t green, uint8_t blue,
                          int x, int y) {
  const int modulation = static_cast<int>(BAYER8[(y & 7) * 8 + (x & 7)]) - 32;
#if RETERMINAL_MODEL == 1001
  const int gray = clampByte(luminance(red, green, blue) + modulation);
  return static_cast<uint8_t>(constrain((gray + 42) / 85, 0, 3));
#elif RETERMINAL_MODEL == 1003
  const int gray = clampByte(luminance(red, green, blue) + modulation);
  return static_cast<uint8_t>(constrain((gray + 8) / 17, 0, 15));
#else
  return panelCodeForRgb(clampByte(red + modulation),
                        clampByte(green + modulation),
                        clampByte(blue + modulation));
#endif
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
  if (wakePins & (1ULL << PIN_KEY0)) buttons += "GPIO3";
  if (wakePins & (1ULL << PIN_KEY1)) {
    if (!buttons.isEmpty()) buttons += "+";
    buttons += "GPIO4";
  }
  if (wakePins & (1ULL << PIN_KEY2)) {
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

float batteryPercentForVoltage(float voltage) {
  static constexpr float volts[] = {
      3.27f, 3.30f, 3.41f, 3.49f, 3.58f, 3.68f,
      3.75f, 3.80f, 3.85f, 3.91f, 3.96f, 4.15f};
  static constexpr float percents[] = {
      0.0f, 5.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f,
      60.0f, 70.0f, 80.0f, 90.0f, 100.0f};
  constexpr size_t count = sizeof(volts) / sizeof(volts[0]);
  if (voltage <= volts[0]) return 0.0f;
  if (voltage >= volts[count - 1]) return 100.0f;
  for (size_t index = 1; index < count; ++index) {
    if (voltage <= volts[index]) {
      const float fraction =
          (voltage - volts[index - 1]) /
          (volts[index] - volts[index - 1]);
      return percents[index - 1] +
             fraction * (percents[index] - percents[index - 1]);
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
  uint32_t totalMillivolts = 0;
  for (int sample = 0; sample < 16; ++sample) {
    totalMillivolts += analogReadMilliVolts(PIN_BATTERY_ADC);
    delay(4);
  }
  batteryVoltage = (totalMillivolts / 16.0f) * 2.0f / 1000.0f;
  batteryPct = constrain(
      static_cast<int>(batteryPercentForVoltage(batteryVoltage) + 0.5f),
      0, 100);
  LOG.printf("[sensor] battery %.3fV -> %d%%\n",
             batteryVoltage, batteryPct);

  climateValid = false;
  for (uint8_t attempt = 0;
       attempt < config::SENSOR_READ_ATTEMPTS && !climateValid; ++attempt) {
    if (attempt > 0) {
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
      }
    }
  }
  if (!climateValid) LOG.println("[sensor] SHT4x unavailable");
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

void selectTitleFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold24pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold18pt7b);
#else
  epaper.setFreeFont(&FreeSansBold12pt7b);
#endif
}

String ellipsize(String text, int maximumWidth) {
  if (epaper.textWidth(text, 1) <= maximumWidth) return text;
  while (text.length() > 1 &&
         epaper.textWidth(text + "...", 1) > maximumWidth) {
    text.remove(text.length() - 1);
  }
  return text + "...";
}

void drawStatusBadges() {
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectStatusFont();
  const int centerY = config::ui(24);
  const int edgeInset = config::ui(6);
  epaper.setTextDatum(ML_DATUM);
  const String climate =
      climateValid
          ? String(temperatureC, 1) + "C  " + String(humidityPct, 0) + "%"
          : "--.-C  --%";
  epaper.drawString(climate, edgeInset, centerY, 1);

  const String battery =
      batteryPct >= 0 ? String(batteryPct) + "%" : "--%";
  epaper.setTextDatum(MR_DATUM);
  epaper.drawString(battery, config::PANEL_WIDTH - edgeInset, centerY, 1);
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

void updatePanel() {
  epaper.update();
}

void renderStatus(const String& message, const String& detail = "",
                  const String& lineAbove = "") {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  if (!lineAbove.isEmpty()) {
    selectStatusFont();
    epaper.drawString(
        ellipsize(lineAbove, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 - config::ui(55), 1);
  }
  selectTitleFont();
  epaper.drawString(
      ellipsize(message, config::PANEL_WIDTH - config::ui(60)),
      config::PANEL_WIDTH / 2,
      config::PANEL_HEIGHT / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectStatusFont();
    epaper.drawString(
        ellipsize(detail, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 + config::ui(22), 1);
  }
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  drawStatusBadges();
  updatePanel();
}

void beep() {
  pinMode(PIN_BUZZER, OUTPUT);
  tone(PIN_BUZZER, 1000, 100);
  delay(120);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

String wifiStationMacAddress() {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return "unavailable";
  char formatted[18] = {};
  snprintf(formatted, sizeof(formatted),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(formatted);
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
    LOG.println("[sd] mount failed; insert a FAT32 card");
    digitalWrite(PIN_SD_ENABLE, LOW);
    return false;
  }
  if (!SD.exists(config::PHOTO_DIR) && !SD.mkdir(config::PHOTO_DIR)) {
    LOG.printf("[sd] could not create %s\n", config::PHOTO_DIR);
    SD.end();
    digitalWrite(PIN_SD_ENABLE, LOW);
    return false;
  }
  LOG.printf("[sd] mounted, card=%lluMB\n",
             static_cast<unsigned long long>(
                 SD.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool supportedPhotoName(String name) {
  name.toLowerCase();
  return name.endsWith(".bmp") || name.endsWith(".png") ||
         name.endsWith(".jpg") || name.endsWith(".jpeg");
}

String baseName(String path) {
  const int slash = path.lastIndexOf('/');
  if (slash >= 0) path = path.substring(slash + 1);
  return path;
}

uint32_t countPhotos() {
  if (!sdReady) return 0;
  File directory = SD.open(config::PHOTO_DIR);
  if (!directory || !directory.isDirectory()) return 0;
  uint32_t count = 0;
  File entry = directory.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && supportedPhotoName(baseName(entry.name())))
      ++count;
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  return count;
}

bool photoPathAt(uint32_t ordinal, String& path) {
  File directory = SD.open(config::PHOTO_DIR);
  if (!directory || !directory.isDirectory()) return false;
  uint32_t seen = 0;
  File entry = directory.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && supportedPhotoName(baseName(entry.name()))) {
      if (seen == ordinal) {
        path = entry.name();
        if (!path.startsWith("/")) {
          path = String(config::PHOTO_DIR) + "/" + path;
        }
        entry.close();
        directory.close();
        return true;
      }
      ++seen;
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  return false;
}

bool renderPreparedBmp(const String& path) {
  File file = SD.open(path, FILE_READ);
  if (!file) return false;

  uint8_t fileHeader[14] = {};
  uint8_t dib[40] = {};
  if (file.read(fileHeader, sizeof(fileHeader)) != sizeof(fileHeader) ||
      fileHeader[0] != 'B' || fileHeader[1] != 'M' ||
      file.read(dib, sizeof(dib)) != sizeof(dib)) {
    file.close();
    return false;
  }

  const uint32_t dibSize = readLe32(dib);
  const int32_t width = readLeS32(dib + 4);
  const int32_t signedHeight = readLeS32(dib + 8);
  const uint16_t planes = readLe16(dib + 12);
  const uint16_t bitsPerPixel = readLe16(dib + 14);
  const uint32_t compression = readLe32(dib + 16);
  const uint32_t pixelOffset = readLe32(fileHeader + 10);
  const int32_t height = signedHeight < 0 ? -signedHeight : signedHeight;

  if (dibSize < 40 || width != config::PANEL_WIDTH ||
      height != config::PANEL_HEIGHT || planes != 1 ||
      bitsPerPixel != 4 || compression != 0 ||
      pixelOffset < 14 + dibSize + 64) {
    file.close();
    return false;
  }

  if (!file.seek(14 + dibSize)) {
    file.close();
    return false;
  }
  uint8_t paletteCodes[16] = {};
  for (int index = 0; index < 16; ++index) {
    uint8_t bgra[4] = {};
    if (file.read(bgra, sizeof(bgra)) != sizeof(bgra)) {
      file.close();
      return false;
    }
    paletteCodes[index] = panelCodeForRgb(bgra[2], bgra[1], bgra[0]);
  }

  const size_t packedSize =
      static_cast<size_t>(config::PANEL_WIDTH) * config::PANEL_HEIGHT / 2;
  uint8_t* packed = static_cast<uint8_t*>(ps_malloc(packedSize));
  if (!packed) packed = static_cast<uint8_t*>(malloc(packedSize));
  if (!packed) {
    LOG.println("[photo] prepared BMP buffer allocation failed");
    file.close();
    return false;
  }
  memset(packed,
         static_cast<uint8_t>((PANEL_WHITE_CODE << 4) | PANEL_WHITE_CODE),
         packedSize);

  const size_t sourceBytes = (config::PANEL_WIDTH + 1) / 2;
  const size_t paddedBytes = (sourceBytes + 3) & ~static_cast<size_t>(3);
  uint8_t* row = static_cast<uint8_t*>(malloc(paddedBytes));
  if (!row || !file.seek(pixelOffset)) {
    free(row);
    free(packed);
    file.close();
    return false;
  }

  bool okay = true;
  for (int fileRow = 0; fileRow < config::PANEL_HEIGHT && okay; ++fileRow) {
    okay = file.read(row, paddedBytes) == paddedBytes;
    if (!okay) break;
    const int targetY =
        signedHeight < 0 ? fileRow : config::PANEL_HEIGHT - 1 - fileRow;
    uint8_t* destination =
        packed + static_cast<size_t>(targetY) * config::PANEL_WIDTH / 2;
    for (int x = 0; x < config::PANEL_WIDTH; x += 2) {
      const uint8_t source = row[x / 2];
      const uint8_t left = paletteCodes[source >> 4];
      const uint8_t right = paletteCodes[source & 0x0F];
      destination[x / 2] =
          static_cast<uint8_t>((left << 4) | (right & 0x0F));
    }
    if ((fileRow & 31) == 0) delay(1);
  }
  free(row);
  file.close();

  if (!okay) {
    free(packed);
    LOG.printf("[photo] truncated prepared BMP: %s\n", path.c_str());
    return false;
  }

  epaper.fillSprite(PANEL_WHITE);
  epaper.pushImage(0, 0, config::PANEL_WIDTH, config::PANEL_HEIGHT,
                   reinterpret_cast<uint16_t*>(packed));
  free(packed);
  LOG.printf("[photo] prepared frame %s\n", path.c_str());
  LOG.println("[render] refreshing panel");
  updatePanel();
  LOG.println("[render] complete");
  return true;
}

bool renderGenericPhoto(const String& path) {
  RgbImage image;
  if (!load_image_from_sd(path.c_str(), 0, 0, &image)) {
    LOG.printf("[photo] decode failed: %s\n", path.c_str());
    return false;
  }
  if (image.width <= 0 || image.height <= 0) {
    image_free(&image);
    return false;
  }

  const float scale = min(
      static_cast<float>(config::PANEL_WIDTH) / image.width,
      static_cast<float>(config::PANEL_HEIGHT) / image.height);
  int targetWidth = max(2, static_cast<int>(image.width * scale));
  int targetHeight = max(1, static_cast<int>(image.height * scale));
  targetWidth = min(targetWidth, config::PANEL_WIDTH);
  targetHeight = min(targetHeight, config::PANEL_HEIGHT);
  if (targetWidth & 1) --targetWidth;
  int targetX = (config::PANEL_WIDTH - targetWidth) / 2;
  if (targetX & 1) --targetX;
  const int targetY = (config::PANEL_HEIGHT - targetHeight) / 2;

  const size_t packedSize =
      static_cast<size_t>(targetWidth) * targetHeight / 2;
  uint8_t* packed = static_cast<uint8_t*>(ps_malloc(packedSize));
  if (!packed) packed = static_cast<uint8_t*>(malloc(packedSize));
  if (!packed) {
    image_free(&image);
    LOG.println("[photo] fallback panel buffer allocation failed");
    return false;
  }

  for (int y = 0; y < targetHeight; ++y) {
    const int sourceY =
        min(image.height - 1,
            static_cast<int>((static_cast<int64_t>(y) * image.height) /
                             targetHeight));
    uint8_t* destination =
        packed + static_cast<size_t>(y) * targetWidth / 2;
    for (int x = 0; x < targetWidth; x += 2) {
      uint8_t codes[2] = {};
      for (int offset = 0; offset < 2; ++offset) {
        const int outputX = x + offset;
        const int sourceX =
            min(image.width - 1,
                static_cast<int>(
                    (static_cast<int64_t>(outputX) * image.width) /
                    targetWidth));
        const uint8_t* pixel =
            image.pixels +
            (static_cast<size_t>(sourceY) * image.width + sourceX) * 3;
        codes[offset] = fallbackPanelCode(
            pixel[0], pixel[1], pixel[2], targetX + outputX, targetY + y);
      }
      destination[x / 2] =
          static_cast<uint8_t>((codes[0] << 4) | (codes[1] & 0x0F));
    }
    if ((y & 31) == 0) delay(1);
  }
  image_free(&image);

  epaper.fillSprite(PANEL_WHITE);
  epaper.pushImage(targetX, targetY, targetWidth, targetHeight,
                   reinterpret_cast<uint16_t*>(packed));
  free(packed);
  LOG.printf("[photo] compatibility render %s at %dx%d\n",
             path.c_str(), targetWidth, targetHeight);
  LOG.println("[render] refreshing panel");
  updatePanel();
  LOG.println("[render] complete");
  return true;
}

bool renderPhoto(const String& path) {
  String lower = path;
  lower.toLowerCase();
  if (lower.endsWith(".bmp") && renderPreparedBmp(path)) return true;
  return renderGenericPhoto(path);
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
#endif
  return false;
}

bool synchronizeClock() {
  ntpSyncCompleted = false;
  sntp_set_time_sync_notification_cb(onNtpTimeSync);
  bool synchronized = startDhcpNtpIfAvailable();
  if (!synchronized) {
#if LWIP_DHCP_GET_NTP_SRV
    const ip_addr_t* server = esp_sntp_getserver(0);
    if (server == nullptr || ip_addr_isany(server)) {
      LOG.println("[ntp] DHCP supplied no NTP server; using configured servers");
    }
#else
    LOG.println("[ntp] DHCP NTP unavailable; using configured servers");
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
    LOG.println("[ntp] local time conversion failed");
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

bool prepareWakePin(int pin) {
  pinMode(pin, INPUT_PULLUP);
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  rtc_gpio_hold_dis(gpio);
  return rtc_gpio_init(gpio) == ESP_OK &&
         rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK &&
         rtc_gpio_pullup_en(gpio) == ESP_OK &&
         rtc_gpio_pulldown_dis(gpio) == ESP_OK;
}

void powerDownAndSleep(uint64_t sleepSeconds = config::SLEEP_SECONDS) {
  disableWifi();
  if (sdReady) SD.end();
  pinMode(PIN_SD_ENABLE, OUTPUT);
  digitalWrite(PIN_SD_ENABLE, LOW);
  pinMode(PIN_BATTERY_ENABLE, OUTPUT);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);

  pinMode(PIN_KEY0, INPUT_PULLUP);
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  const uint32_t releaseStarted = millis();
  while ((!digitalRead(PIN_KEY0) || !digitalRead(PIN_KEY1) ||
          !digitalRead(PIN_KEY2)) &&
         millis() - releaseStarted < 2000) {
    delay(10);
  }
  bool rtcPinsReady = true;
  rtcPinsReady = prepareWakePin(PIN_KEY0) && rtcPinsReady;
  rtcPinsReady = prepareWakePin(PIN_KEY1) && rtcPinsReady;
  rtcPinsReady = prepareWakePin(PIN_KEY2) && rtcPinsReady;

  const uint64_t wakeMask =
      (1ULL << PIN_KEY0) | (1ULL << PIN_KEY1) | (1ULL << PIN_KEY2);
  const esp_err_t buttonWakeResult =
      rtcPinsReady
          ? esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW)
          : ESP_FAIL;
  const esp_err_t timerWakeResult =
      esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
  LOG.printf("[sleep] wake config: buttons=%s timer=%s levels=%d/%d/%d\n",
             esp_err_to_name(buttonWakeResult),
             esp_err_to_name(timerWakeResult),
             digitalRead(PIN_KEY0),
             digitalRead(PIN_KEY1),
             digitalRead(PIN_KEY2));
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
  configureLocalTimezone();
  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  delay(250);

  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  const uint64_t wakePins =
      wakeCause == ESP_SLEEP_WAKEUP_EXT1
          ? esp_sleep_get_ext1_wakeup_status()
          : 0;
  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  const bool buttonWake = wakeCause == ESP_SLEEP_WAKEUP_EXT1;
  const bool key0Wake = (wakePins & (1ULL << PIN_KEY0)) != 0;
  const bool key1Wake = (wakePins & (1ULL << PIN_KEY1)) != 0;
  const bool key2Wake = (wakePins & (1ULL << PIN_KEY2)) != 0;
  if (app_logic::startupBeepRequired(coldBoot, buttonWake)) beep();

  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone Photo Viewer / %s\n",
             MODEL_NAME, COLOR_MODE_NAME);
  LOG.println("============================================");
  LOG.printf("[boot] wake cause=%d pins=0x%llx, PSRAM=%luK, "
             "KEY0=%s KEY1=%s KEY2=%s\n",
             wakeCause, static_cast<unsigned long long>(wakePins),
             static_cast<unsigned long>(ESP.getPsramSize() / 1024),
             key0Wake ? "wake" : "idle",
             key1Wake ? "wake" : "idle",
             key2Wake ? "wake" : "idle");

  bool wakeEventLogged = logWakeEvent(wakeCause, wakePins, false);
  const bool ntpDue = ntpRefreshDue(coldBoot);
  struct tm localTime = {};
  if (!ntpDue && !coldBoot && !buttonWake && localClock(localTime) &&
      quietHoursActive(localTime)) {
    LOG.printf("[quiet] photo refresh suppressed; sleeping until %s\n",
               quietEndLabel().c_str());
    powerDownAndSleep(secondsUntilQuietEnd(localTime));
    return;
  }

  if (coldBoot) {
    readSensors();
    pcf8563::Reading storedRtc;
    readAndLogHardwareRtc(storedRtc);
  }
  epaper.begin();
  sdReady = mountSd();
  const uint32_t photoCount = countPhotos();
  LOG.printf("[photo] %lu supported files in %s\n",
             static_cast<unsigned long>(photoCount), config::PHOTO_DIR);

  const bool showStartupStatus = coldBoot;
  const String stationMac = wifiStationMacAddress();
  String statusDetail;
  if (!sdReady) {
    statusDetail = "No SD card - insert a FAT32 card";
  } else if (photoCount == 0) {
    statusDetail = "No photos in /photos";
  } else {
    statusDetail = String(photoCount) + " photos ready";
  }

#if RETERMINAL_MODEL == 1001
  if (showStartupStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(WIFI_SSID), statusDetail,
                 stationMac);
  }
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

#if RETERMINAL_MODEL != 1001
  if (showStartupStatus) {
    LOG.println("[display] showing Wi-Fi connection status");
    renderStatus("Connecting to " + String(WIFI_SSID), statusDetail,
                 stationMac);
  }
#endif

  bool ntpSynchronized = false;
  if (ntpDue) {
    if (connectWifi()) ntpSynchronized = synchronizeClock();
    if (!ntpSynchronized && !coldBoot) {
      LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
      restoreClockFromHardwareRtc();
    }
    disableWifi();
  } else {
    LOG.println("[wifi] skipped; daily clock sync is not due");
  }
  configureLocalTimezone();
  if (!wakeEventLogged) {
    logWakeEvent(wakeCause, wakePins, true);
  }

  // A cold boot always replaces its temporary startup screen with a photo.
  // Automatic timer wakes inside quiet hours otherwise preserve the photo
  // already retained by the e-paper panel.
  if (!coldBoot && !buttonWake && localClock(localTime) &&
      quietHoursActive(localTime)) {
    LOG.printf("[quiet] photo refresh suppressed after clock sync; "
               "sleeping until %s\n",
               quietEndLabel().c_str());
    powerDownAndSleep(secondsUntilQuietEnd(localTime));
    return;
  }

  bool displayed = false;
  if (sdReady && photoCount > 0) {
    const int direction = app_logic::photoDirection(key1Wake);
    if (currentPhotoIndex < 0) currentPhotoIndex = 0;
    else currentPhotoIndex += direction;

    for (uint8_t attempt = 0;
         attempt < config::MAX_PHOTO_ATTEMPTS && attempt < photoCount;
         ++attempt) {
      const int32_t normalized =
          app_logic::normalizePhotoIndex(currentPhotoIndex, photoCount);
      currentPhotoIndex = normalized;
      String path;
      if (photoPathAt(static_cast<uint32_t>(normalized), path)) {
        LOG.printf("[photo] attempt %u: %ld/%lu %s\n",
                   attempt + 1, static_cast<long>(normalized + 1),
                   static_cast<unsigned long>(photoCount), path.c_str());
        if (renderPhoto(path)) {
          displayed = true;
          break;
        }
      }
      currentPhotoIndex += direction;
    }
  }

  if (!displayed) {
    renderStatus("Photo unavailable",
                 sdReady ? "Run tools/prepare_photos.py and copy files to /photos"
                         : "Insert a FAT32 SD card");
  }

  uint64_t nextSleepSeconds = config::SLEEP_SECONDS;
  if (localClock(localTime)) {
    if (quietHoursActive(localTime) ||
        nextWakeFallsInQuietHours(localTime, config::SLEEP_SECONDS)) {
      nextSleepSeconds = secondsUntilQuietEnd(localTime);
      LOG.printf("[quiet] retaining this photo until %s\n",
                 quietEndLabel().c_str());
    }
  }
  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
