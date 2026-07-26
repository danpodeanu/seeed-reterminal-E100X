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

#include <algorithm>
#include <vector>

#include "config.h"
#include "app_logic.h"
#include "app_logger.h"
#include "battery_gauge.h"
#include "ntp_sync.h"
#include "board_pins.h"
#include "hardware.h"
#include "local_time.h"
#include "wake_report.h"
#include "rtc_sync.h"
#include "wifi_sta.h"
#include "climate_sensor.h"
#include "sd_card.h"
#include "text_render.h"
#include "photo_manifest.h"
#include "quiet_hours.h"
#include "sensors.h"
#include "image_loader.h"
#include "pcf8563_utc.h"
#include "secrets.h"
#include "timestamped_logger.h"

SET_LOOP_TASK_STACK_SIZE(16U * 1024U);

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select a reTerminal E-series driver; check common/include/driver.h"
#endif

TimestampedLogger appLog(Serial1);
// LOG is provided by app_logger.h.

namespace {

using namespace ::board;
constexpr int PIN_KEY0 = 3;
constexpr int PIN_KEY1 = 4;
constexpr int PIN_KEY2 = 5;

#if RETERMINAL_MODEL == 1001
constexpr uint32_t PANEL_WHITE = TFT_GRAY_3;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint8_t PANEL_WHITE_CODE = 0x3;
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint8_t PANEL_WHITE_CODE = 0x0;
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr uint8_t PANEL_WHITE_CODE = 0xF;
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr uint8_t PANEL_WHITE_CODE = 0x0;
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
std::vector<String> photoList;
sensors::Readings sensorReadings;

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

// batteryPercentForVoltage() and the 16-sample averaging block used to be
// inline here; they now live in common/include/battery_gauge.h and are
// invoked via battery::measureBatteryFromAdc().

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

void drawStatusBadges() {
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectStatusFont();
  const int centerY = config::ui(24);
  const int edgeInset = config::ui(6);
  epaper.setTextDatum(ML_DATUM);
  const String climate =
      sensorReadings.climateValid
          ? String(sensorReadings.temperatureC, 1) + "C  " + String(sensorReadings.humidityPct, 0) + "%"
          : "--.-C  --%";
  epaper.drawString(climate, edgeInset, centerY, 1);

  // Battery gauge layout mirrors weather/xkcd: percentage text sits
  // just left of the gauge icon, which itself carries the charging
  // bolt overlay when external power is present.
  const String percent =
      sensorReadings.batteryPct >= 0 ? String(sensorReadings.batteryPct) + "%" : "--%";
  const int w = config::ui(22);
  const int h = config::ui(12);
  const int terminalWidth = max(3, config::ui(5));
  const int x = config::PANEL_WIDTH - edgeInset - terminalWidth - w;
  const int gaugeCenterY = centerY + 2;
  const int y = gaugeCenterY - h / 2;
  const int outline = max(1, config::ui(1));
  const int terminalHeight = max(3, config::ui(5));

  epaper.setTextDatum(MR_DATUM);
  const int percentRightX = x - config::ui(9);
  epaper.drawString(percent, percentRightX, centerY, 1);
  text_render::drawBatteryGauge(
      epaper, x, y, w, h, sensorReadings.batteryPct, outline, terminalWidth,
      terminalHeight, PANEL_BLACK, PANEL_WHITE,
      sensorReadings.chargerValid && sensorReadings.externalPower);
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
        text_render::ellipsize(epaper, lineAbove, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 - config::ui(55), 1);
  }
  selectTitleFont();
  epaper.drawString(
      text_render::ellipsize(epaper, message, config::PANEL_WIDTH - config::ui(60)),
      config::PANEL_WIDTH / 2,
      config::PANEL_HEIGHT / 2 - config::ui(15), 1);
  if (!detail.isEmpty()) {
    selectStatusFont();
    epaper.drawString(
        text_render::ellipsize(epaper, detail, config::PANEL_WIDTH - config::ui(60)),
        config::PANEL_WIDTH / 2,
        config::PANEL_HEIGHT / 2 + config::ui(22), 1);
  }
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  drawStatusBadges();
  updatePanel();
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
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return 0;
  }
  photoList.clear();
  File entry = directory.openNextFile();
  while (entry) {
    if (!entry.isDirectory() && supportedPhotoName(baseName(entry.name()))) {
      String path = entry.name();
      if (!path.startsWith("/")) {
        path = String(config::PHOTO_DIR) + "/" + path;
      }
      photoList.emplace_back(std::move(path));
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  if (config::PHOTO_ORDER_RANDOM) {
    // Shuffle at boot so rotation feels random rather than following FAT32
    // directory order. esp_random() draws from the hardware RNG.
    app_logic::shuffleInPlace(
        photoList, [](size_t upperExclusive) -> size_t {
          return static_cast<size_t>(esp_random()) % upperExclusive;
        });
  } else {
    // Alphabetical sort makes ordinal-based rotation deterministic across
    // boots and independent of FAT32 directory ordering.
    std::sort(photoList.begin(), photoList.end(),
              [](const String& a, const String& b) {
                return strcmp(a.c_str(), b.c_str()) < 0;
              });
  }
  return static_cast<uint32_t>(photoList.size());
}

bool photoPathAt(uint32_t ordinal, String& path) {
  if (ordinal >= photoList.size()) return false;
  path = photoList[ordinal];
  return true;
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
  // Negating INT32_MIN is undefined behaviour; reject it up front along with
  // any other value that couldn't plausibly be a panel-sized BMP.
  if (signedHeight == INT32_MIN) {
    file.close();
    return false;
  }
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

// NTP sync helpers now live in common/include/ntp_sync.h. The wrapper below
void powerDownAndSleep(uint64_t sleepSeconds = config::SLEEP_SECONDS) {
  wifi_sta::disable();
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
  rtcPinsReady = hardware::configureWakePin(PIN_KEY0) && rtcPinsReady;
  rtcPinsReady = hardware::configureWakePin(PIN_KEY1) && rtcPinsReady;
  rtcPinsReady = hardware::configureWakePin(PIN_KEY2) && rtcPinsReady;

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
  hardware::setStatusLed(false);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  hardware::setStatusLed(true);
  local_time::configureTimezone(config::TIMEZONE);
  quiet_hours::configure({config::QUIET_HOURS_ENABLED,
                          config::QUIET_START_HOUR,
                          config::QUIET_START_MINUTE,
                          config::QUIET_END_HOUR,
                          config::QUIET_END_MINUTE});
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
  if (app_logic::startupBeepRequired(coldBoot, buttonWake)) hardware::beep();

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

  const time_t startupTime = time(nullptr);
  const bool schedulingClockSuspicious =
      !local_time::clockIsValid() ||
      (lastNtpSyncEpoch > 0 && startupTime < lastNtpSyncEpoch);
  // The ESP32's deep-sleep RTC drifts several percent (internal 150 kHz RC
  // oscillator), so read the PCF8563 on every wake and reset the system
  // clock from it. PCF8563 is battery-backed and stays accurate to ~20 ppm.
  const bool hardwareRtcCheckedEarly = true;
  if (schedulingClockSuspicious) {
    LOG.println("[rtc] schedule clock is invalid or behind retained state; "
                "trying PCF8563");
  }
  rtc_sync::restoreSystemClock();

  bool wakeEventLogged = wake_report::logWakeEvent(wakeCause, wakePins, false);
  const bool ntpDue = local_time::refreshDue(coldBoot, lastNtpSyncEpoch, config::NTP_REFRESH_SECONDS);
  struct tm localTime = {};
  if (!ntpDue && !coldBoot && !buttonWake && local_time::localClock(localTime) &&
      quiet_hours::active(localTime)) {
    LOG.printf("[quiet] photo refresh suppressed; sleeping until %s\n",
               quiet_hours::endLabel().c_str());
    powerDownAndSleep(quiet_hours::secondsUntilEnd(localTime));
    return;
  }

  if (coldBoot) {
    sensors::readAll(PIN_BATTERY_ENABLE, PIN_BATTERY_ADC, sht4, config::SENSOR_READ_ATTEMPTS, config::SENSOR_RETRY_DELAY_MS, sensorReadings);
    if (!hardwareRtcCheckedEarly) {
      pcf8563::Reading storedRtc;
      rtc_sync::readAndLog(storedRtc);
    }
  }
  epaper.begin();
  sdReady = sd_card::mount(epaper.getSPIinstance(), config::PHOTO_DIR);
  const uint32_t photoCount = countPhotos();
  LOG.printf("[photo] %lu supported files in %s\n",
             static_cast<unsigned long>(photoCount), config::PHOTO_DIR);

  if (sdReady) {
    const String manifestPath =
        String(config::PHOTO_DIR) + photo_manifest::MANIFEST_FILE;
    String manifestJson;
    if (sd_card::readFile(manifestPath, manifestJson, 32U * 1024U)) {
      String foundVersion;
      const photo_manifest::Status status = photo_manifest::inspect(
          manifestJson, config::DITHER_VERSION, foundVersion);
      switch (status) {
        case photo_manifest::Status::Matches:
          LOG.printf("[photo] manifest OK (dither %s)\n",
                     config::DITHER_VERSION);
          break;
        case photo_manifest::Status::StaleDither:
          LOG.printf(
              "[photo] manifest dither %s but firmware expects %s; "
              "re-run prepare_photos.py\n",
              foundVersion.c_str(), config::DITHER_VERSION);
          break;
        case photo_manifest::Status::Unrecognised:
          LOG.println("[photo] manifest present but unrecognised; ignoring");
          break;
        case photo_manifest::Status::Absent:
          break;
      }
    } else {
      LOG.println("[photo] no manifest.json in /photos (legacy card)");
    }
  }

  const bool showStartupStatus = coldBoot;
  const String stationMac = wifi_sta::stationMacAddress();
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
    if (wifi_sta::connectStation(WIFI_SSID, WIFI_PASSWORD, config::WIFI_TIMEOUT_MS)) ntpSynchronized = ntp::synchronizeAndPersist(config::TIMEZONE, config::NTP_SERVER_PRIMARY, config::NTP_SERVER_SECONDARY, config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
    if (!ntpSynchronized && !coldBoot) {
      LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
      rtc_sync::restoreSystemClock();
    }
    wifi_sta::disable();
  } else {
    LOG.println("[wifi] skipped; clock sync is not due");
  }
  local_time::configureTimezone(config::TIMEZONE);
  quiet_hours::configure({config::QUIET_HOURS_ENABLED,
                          config::QUIET_START_HOUR,
                          config::QUIET_START_MINUTE,
                          config::QUIET_END_HOUR,
                          config::QUIET_END_MINUTE});
  if (!wakeEventLogged) {
    wake_report::logWakeEvent(wakeCause, wakePins, true);
  }

  // A cold boot always replaces its temporary startup screen with a photo.
  // Automatic timer wakes inside quiet hours otherwise preserve the photo
  // already retained by the e-paper panel.
  if (!coldBoot && !buttonWake && local_time::localClock(localTime) &&
      quiet_hours::active(localTime)) {
    LOG.printf("[quiet] photo refresh suppressed after clock sync; "
               "sleeping until %s\n",
               quiet_hours::endLabel().c_str());
    powerDownAndSleep(quiet_hours::secondsUntilEnd(localTime));
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
  if (local_time::localClock(localTime)) {
    if (quiet_hours::active(localTime) ||
        quiet_hours::nextWakeFallsInside(localTime, config::SLEEP_SECONDS)) {
      nextSleepSeconds = quiet_hours::secondsUntilEnd(localTime);
      LOG.printf("[quiet] retaining this photo until %s\n",
                 quiet_hours::endLabel().c_str());
    }
  }
  powerDownAndSleep(nextSleepSeconds);
}

void loop() {
  delay(1000);
}
