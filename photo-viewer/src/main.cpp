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
#include "panel_watchdog.h"
#include "rtc_sync.h"
#include "wifi_sta.h"
#include "climate_sensor.h"
#include "sd_card.h"
#include "sd_web_portal.h"
#include "sd_web_portal_ui.h"
#include "config_portal.h"
#include "config_portal_ui.h"
#include "wifi_schema.h"
#include "photo_wifi_credentials.h"
#include "photo_config_runtime.h"
#include "photo_config_schema.h"
#include "log_sd_sink.h"
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
// SD-portal mode flag. Persists across deep sleep and ESP.restart() so
// the device remembers whether the user pressed an arrow to switch
// modes. Cleared to false on a hard power cycle (RTC RAM is only
// preserved while VDD stays up), which is the correct default: after a
// power-off the user expects photo mode again.
RTC_DATA_ATTR bool sdPortalMode = false;
// Set to true when we're leaving SD-portal mode via ESP.restart() so
// the fresh setup() call knows to re-render the current photo rather
// than advance to the next one.
RTC_DATA_ATTR bool photoRefreshOnly = false;
// Seed for the deterministic shuffle used by countPhotos(). Zero means
// "not seeded yet"; setup() clears it on cold boot. Every wake in the
// same session reuses the seed so /photos is shuffled into the same
// order, which keeps currentPhotoIndex stable across wakes - without
// this, each wake re-shuffles and the "next photo" ordinal can land on
// the same file the previous wake already showed.
RTC_DATA_ATTR uint32_t photoShuffleSeed = 0;
// Index of the photo currently sitting on the e-paper panel. Used to
// short-circuit timer wakes when advancing would land on the same file
// (e.g. only one photo on the card): the panel already shows the right
// image, so we skip the 20 s refresh and just go back to sleep.
// -1 means "no known photo on the panel yet".
RTC_DATA_ATTR int32_t lastRenderedIndex = -1;

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
  panel_watchdog::guard([]() { epaper.update(); });
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
  // BMP takes the fast prepared-4-bit path in renderPreparedBmp(). PNG is
  // decoded via the pngle streaming decoder in renderGenericPhoto() -- it
  // still allocates a width*height*3 RGB buffer in PSRAM, so extremely
  // large PNGs (e.g. an 8000x6000 photo saved as PNG) will fail gracefully
  // with an "[png] OOM" log line instead of crashing. Typical /photos PNGs
  // sized for the panel (a few hundred pixels a side) fit comfortably.
  // JPEG is intentionally still gated behind the browser uploader because
  // 12 MP iPhone photos routinely OOM the same buffer at ~36 MB.
  return name.endsWith(".bmp") || name.endsWith(".png");
}

String baseName(String path) {
  const int slash = path.lastIndexOf('/');
  if (slash >= 0) path = path.substring(slash + 1);
  return path;
}

uint32_t countPhotos() {
  if (!sdReady) return 0;
  File directory = sd_card::openForRead(config::PHOTO_DIR);
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
      // Skip macOS/FAT sidecars like "._IMG_1234.bmp" - the finder
      // writes those to removable media and they'd otherwise inflate
      // the ordinal count with zero-visible photos.
      const String bn = baseName(path);
      if (bn.startsWith("._")) {
        entry.close();
        entry = directory.openNextFile();
        continue;
      }
      photoList.emplace_back(std::move(path));
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
  // Debug override: pinned photo filename shortcuts the entire enumeration
  // to just the requested file (verified to exist and to be a supported
  // format). Empty string disables the override. countPhotos() runs more
  // than once per boot (cold-boot "any photos?" probe + main render), so
  // logging happens at the caller instead of here to avoid double logs.
  const char* pinned = photo_config::runtime::pinnedPhoto();
  if (pinned && pinned[0] != '\0') {
    String pinnedName(pinned);
    pinnedName.trim();
    String only;
    for (const String& p : photoList) {
      if (baseName(p) == pinnedName) { only = p; break; }
    }
    photoList.clear();
    if (only.length() > 0) {
      photoList.emplace_back(std::move(only));
    }
    return static_cast<uint32_t>(photoList.size());
  }
  if (photo_config::runtime::randomOrder()) {
    // Seeded xorshift32 shuffle. The seed lives in RTC memory and stays
    // fixed across button/timer wakes within a power session, so
    // "advance by 1" always lands on the next photo the user last saw.
    // photoShuffleSeed is reset on cold boot so each fresh power-on
    // still gets a new random order. esp_random() provides bootstrap
    // entropy.
    if (photoShuffleSeed == 0) {
      photoShuffleSeed = esp_random();
      if (photoShuffleSeed == 0) photoShuffleSeed = 1;  // never 0
    }
    uint32_t s = photoShuffleSeed;
    app_logic::shuffleInPlace(
        photoList, [&s](size_t upperExclusive) -> size_t {
          // xorshift32 - cheap, good enough for shuffling a directory.
          s ^= s << 13;
          s ^= s >> 17;
          s ^= s << 5;
          return static_cast<size_t>(s) % upperExclusive;
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
  File file = sd_card::openForRead(path);
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
  // PNGs (and any non-BMP file supportedPhotoName() lets through) fall
  // through to the pngle-backed streaming decoder. renderGenericPhoto()
  // returns false with a "[photo] decode failed" line on OOM, so a huge
  // PNG will just skip to the next candidate instead of taking the app
  // down.
  if (lower.endsWith(".png")) return renderGenericPhoto(path);
  return false;
}

// NTP sync helpers now live in common/include/ntp_sync.h. The wrapper below
void powerDownAndSleep(uint64_t sleepSeconds = 0) {
  if (sleepSeconds == 0) sleepSeconds = photo_config::runtime::sleepSeconds();
  wifi_sta::disable();
  // Close the log file before SD.end() so its FAT/directory update
  // hits disk cleanly. Safe to call unconditionally -- no-ops when no
  // sink is attached.
  appLog.detachSdSink();
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

// Poll all three front-panel buttons. Returns true as soon as any of
// them registers a solid press (LOW for at least ~60 ms). Used by the
// SD portal loop to detect the "exit portal" gesture. The green button
// (GPIO 5) is the same one that enters the portal, so treating it as
// an exit gesture too gives the user a symmetric enter/exit gesture
// on the same key.
bool exitButtonPressedNow() {
  if (digitalRead(PIN_KEY0) == LOW || digitalRead(PIN_KEY1) == LOW ||
      digitalRead(PIN_KEY2) == LOW) {
    // Debounce: require the press to still be held after a short delay.
    delay(30);
    if (digitalRead(PIN_KEY0) == LOW || digitalRead(PIN_KEY1) == LOW ||
        digitalRead(PIN_KEY2) == LOW) {
      return true;
    }
  }
  return false;
}

// Render the SD-portal welcome screen using whichever panel fonts fit.
// Chooses point sizes that match photo-viewer's renderStatus() so the
// two look visually related.
void renderPortalOnPanel(const String& ssid, const String& password,
                         const IPAddress& ip, uint16_t port) {
#if RETERMINAL_MODEL == 1001
  const GFXfont* titleFont = &FreeSansBold18pt7b;
  const GFXfont* subtitleFont = &FreeSans12pt7b;
  const GFXfont* captionFont = &FreeSansBold9pt7b;
  const GFXfont* detailFont = &FreeSans9pt7b;
#elif RETERMINAL_MODEL == 1002
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
#endif

  config_portal::ui::RenderInfo info;
  info.modelLabel = MODEL_NAME;
  info.title = "Configure";
  info.tagline = "Join the AP for Wi-Fi, SD, photos";
  info.ssid = ssid;
  info.wifiPassword = password;
  info.url = String("http://") + ip.toString();
  info.macAddress = wifi_sta::stationMacAddress();
  info.wifiPayload = config_portal::wifiQrPayload(
      ssid, password.length() ? password.c_str() : nullptr);
  info.urlPayload = config_portal::urlQrPayload(ip, port, "/wifi");
  info.fonts.titleFont = titleFont;
  info.fonts.subtitleFont = subtitleFont;
  info.fonts.captionFont = captionFont;
  info.fonts.detailFont = detailFont;

  config_portal::ui::renderPortalScreen<EPaper>(
      epaper, config::PANEL_WIDTH, config::PANEL_HEIGHT, PANEL_BLACK,
      PANEL_WHITE, info);
  panel_watchdog::guard([]() { epaper.update(); });
}

// SD-portal mode entry point. Called from setup() when sdPortalMode is
// true. Brings up the soft-AP + HTTP portal (config_portal for Wi-Fi +
// Reset tabs, sd_web_portal attached to the same server for SD +
// Photos tabs), draws the welcome QR screen, and services HTTP
// requests forever. Exits when the user presses any front-panel button
// or when the web UI POSTs /exit-portal, at which point we schedule a
// return to photo mode and reboot so setup() gets a fresh environment
// (WebServer + softAP tear-down inside a single boot is fiddly).
[[noreturn]] void runSdWebPortal() {
  LOG.println("[portal] entering configuration portal mode");

  epaper.begin();
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif

  sdReady = sd_card::mount(epaper.getSPIinstance(), config::PHOTO_DIR);
  if (!sdReady) {
    // No card - show the same message the photo path uses so behaviour
    // is consistent, then wait for the user to press an arrow to bail
    // out. We don't tear down and reboot here because a card might get
    // inserted while we're waiting.
    renderStatus("No SD card", "Insert a FAT32 card and press any button");
    while (!exitButtonPressedNow()) delay(50);
    sdPortalMode = false;
    photoRefreshOnly = true;
    LOG.println("[portal] no-card exit; restarting into photo mode");
    LOG.flush();
    delay(200);
    ESP.restart();
  }

  // Extra nav tabs the config_portal chrome renders between Settings
  // and Reset. The pages behind these URLs are served by
  // sd_web_portal::attachRoutes below - they share the same WebServer.
  static const config_portal::NavTab kExtraTabs[] = {
      {"SD",     "/browse?path=%2F", "sd"},
      {"Photos", "/upload-photo",    "photos"},
  };

  config_portal::Config portalCfg;
  portalCfg.wifiSchema = &config_portal::kWifiSchema;
  portalCfg.appSchema = &photo_config::kSchema;
  portalCfg.appName = "Photo Viewer";
  portalCfg.helpUrl = config::PORTAL_HELP_URL;
  portalCfg.apSsidPrefix = config::PORTAL_SSID_PREFIX;
  portalCfg.apIp = IPAddress(192, 168, 1, 1);
  portalCfg.apGateway = IPAddress(192, 168, 1, 1);
  portalCfg.apNetmask = IPAddress(255, 255, 255, 0);
  portalCfg.httpPort = config::PORTAL_HTTP_PORT;
  portalCfg.maxConnections = config::PORTAL_MAX_CONNECTIONS;
  portalCfg.useAutoApPassword = true;
  portalCfg.extraTabs = kExtraTabs;
  portalCfg.extraTabCount = sizeof(kExtraTabs) / sizeof(kExtraTabs[0]);
  portalCfg.wifiFallback = [](const char* key) -> String {
    if (strcmp(key, "ssid") == 0) return String(photo_wifi::ssid());
    if (strcmp(key, "password") == 0) return String(photo_wifi::password());
    return String();
  };

  if (!config_portal::begin(portalCfg)) {
    renderStatus("Wi-Fi start failed",
                 "Press any button to return to photo mode");
    while (!exitButtonPressedNow()) delay(50);
    sdPortalMode = false;
    photoRefreshOnly = true;
    LOG.println("[portal] AP start failed; restarting into photo mode");
    LOG.flush();
    delay(200);
    ESP.restart();
  }

  // Build the SD/photo portal config and attach its handlers to the
  // WebServer config_portal just opened. sd_web_portal::attachRoutes
  // does NOT touch AP/DNS - those are already owned by config_portal.
  sd_web_portal::Config sdCfg;
  sdCfg.panelWidth = config::PANEL_WIDTH;
  sdCfg.panelHeight = config::PANEL_HEIGHT;
#if RETERMINAL_MODEL == 1001
  sdCfg.panelPalette = "gray4";
#elif RETERMINAL_MODEL == 1003
  sdCfg.panelPalette = "gray16";
#else
  sdCfg.panelPalette = "e6";
#endif
  sdCfg.panelModel = MODEL_NAME;
  sdCfg.photosDir = config::PHOTO_DIR;
  sdCfg.urlQrPath = "/upload-photo";
  // Render the shared nav strip once so sd-web pages display the same
  // tab bar. Cross-portal nav highlight is intentionally left off:
  // sd-web pages already advertise which page they are via their own
  // header ("SD Card Portal" + breadcrumb).
  static String s_navHtml;
  s_navHtml = config_portal::renderNavStripHtml(portalCfg, nullptr);
  sdCfg.navHtml = s_navHtml.c_str();
  WebServer* server = config_portal::webServer();
  if (server) {
    sd_web_portal::attachRoutes(*server, sdCfg);
  } else {
    LOG.println("[portal] webServer() returned null; SD tabs disabled");
  }

  renderPortalOnPanel(config_portal::currentSsid(),
                      config_portal::currentApPassword(),
                      config_portal::currentIp(),
                      config_portal::currentPort());
  LOG.printf("[portal] SSID=\"%s\" URL=http://%s:%u/\n",
             config_portal::currentSsid().c_str(),
             config_portal::currentIp().toString().c_str(),
             config_portal::currentPort());

  pinMode(PIN_KEY0, INPUT_PULLUP);
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);

  while (true) {
    config_portal::loop();
    const bool webExit = sd_web_portal::exitRequested()
                         || config_portal::rebootRequested();
    if (webExit || exitButtonPressedNow()) {
      LOG.println(webExit
                      ? "[portal] web exit requested; leaving portal mode"
                      : "[portal] button pressed; exiting portal mode");
      hardware::beep();
      // Give the HTTP server a moment to flush the response to the
      // browser before we tear the AP down.
      if (webExit) {
        const uint32_t drainStart = millis();
        while (millis() - drainStart < 400) {
          config_portal::loop();
          delay(10);
        }
      }
      sd_web_portal::end();
      config_portal::end();
      appLog.detachSdSink();
      if (sdReady) SD.end();
      sdPortalMode = false;
      photoRefreshOnly = true;
      LOG.flush();
      // Wait for all buttons to be released so we don't immediately
      // re-toggle after the ESP.restart() boot.
      const uint32_t startWait = millis();
      while ((digitalRead(PIN_KEY0) == LOW ||
              digitalRead(PIN_KEY1) == LOW ||
              digitalRead(PIN_KEY2) == LOW) &&
             millis() - startWait < 2000) {
        delay(10);
      }
      delay(200);
      ESP.restart();
    }
    delay(2);
  }
}

}  // namespace

void setup() {
  hardware::setStatusLed(true);
  photo_wifi::load();
  photo_config::runtime::load();
  local_time::configureTimezone(photo_config::runtime::timezone());
  quiet_hours::configure({photo_config::runtime::quietHoursEnabled(),
                          photo_config::runtime::quietStartHour(),
                          photo_config::runtime::quietStartMinute(),
                          photo_config::runtime::quietEndHour(),
                          photo_config::runtime::quietEndMinute()});
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

  // Beep immediately on any physical button wake so the user gets
  // instant feedback, even if we're about to spend several seconds
  // spinning up Wi-Fi for portal mode.
  if (buttonWake) hardware::beep();

  // Button gestures on button wake:
  //   - Green (KEY2)       -> enter the SD upload portal.
  //   - Arrow-left  (KEY1) -> previous photo.
  //   - Arrow-right (KEY0) -> next photo.
  //   - Any button while already in portal -> exit portal and refresh.
  // Cold boots start in photo mode unless the SD has no photos, in which
  // case we jump straight into the portal so the user can upload
  // something without hunting for the right gesture.
  if (coldBoot) {
    sdPortalMode = false;
    photoRefreshOnly = false;
    lastRenderedIndex = -1;
    // Reset the shuffle seed so a fresh power-on picks a new random
    // photo order. Deep-sleep wakes reuse the last seed to keep
    // "advance by one" stable across wakes.
    photoShuffleSeed = 0;
    sdReady = sd_card::mount(epaper.getSPIinstance(), config::PHOTO_DIR);
    const uint32_t initialPhotoCount = countPhotos();
    if (initialPhotoCount == 0) {
      LOG.printf("[boot] no photos in %s - entering upload portal\n",
                 config::PHOTO_DIR);
      sdPortalMode = true;
    }
  } else if (buttonWake) {
    if (sdPortalMode) {
      // Any wake while portal-mode is armed means "leave portal": clear
      // the flag and force a same-photo redraw so nothing looks stale.
      sdPortalMode = false;
      photoRefreshOnly = true;
    } else if (key2Wake) {
      // Green: enter the upload portal.
      LOG.println("[boot] green pressed; entering upload portal");
      sdPortalMode = true;
    }
    // Arrow wakes fall through unmodified; app_logic::photoDirection()
    // maps key1Wake -> previous and everything else -> next.
  }

  if (sdPortalMode) {
    runSdWebPortal();
    return;  // unreachable - runSdWebPortal is [[noreturn]]
  }

  // startupBeepRequired only fires on cold boots now that buttonWake
  // has already beeped above.
  if (!buttonWake && app_logic::startupBeepRequired(coldBoot, buttonWake)) {
    hardware::beep();
  }

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
  if (sdReady && photo_config::runtime::logToSd()) {
    log_sd_sink::install(appLog);
  }
  const uint32_t photoCount = countPhotos();
  const char* pinned = photo_config::runtime::pinnedPhoto();
  if (pinned && pinned[0] != '\0') {
    // Pinned-photo debug override: photoCount is either 1 (match) or 0
    // (name doesn't exist in /photos).
    if (photoCount == 1) {
      LOG.printf("[photo] pinned to %s (debug override active)\n", pinned);
    } else {
      LOG.printf(
          "[photo] pinned filename \"%s\" not found in %s (debug override)\n",
          pinned, config::PHOTO_DIR);
    }
  } else {
    LOG.printf("[photo] %lu supported files in %s\n",
               static_cast<unsigned long>(photoCount), config::PHOTO_DIR);
  }

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
    renderStatus("Connecting to " + String(photo_wifi::ssid()), statusDetail,
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
    renderStatus("Connecting to " + String(photo_wifi::ssid()), statusDetail,
                 stationMac);
  }
#endif

  bool ntpSynchronized = false;
  if (ntpDue) {
    if (wifi_sta::connectStation(photo_wifi::ssid(), photo_wifi::password(), config::WIFI_TIMEOUT_MS)) ntpSynchronized = ntp::synchronizeAndPersist(photo_config::runtime::timezone(), photo_config::runtime::ntpPrimary(), photo_config::runtime::ntpSecondary(), config::NTP_DHCP_TIMEOUT_MS, config::NTP_SYNC_TIMEOUT_MS, &lastNtpSyncEpoch);
    if (!ntpSynchronized && !coldBoot) {
      LOG.println("[ntp] using PCF8563 fallback after synchronization failure");
      rtc_sync::restoreSystemClock();
    }
    wifi_sta::disable();
  } else {
    LOG.println("[wifi] skipped; clock sync is not due");
  }
  local_time::configureTimezone(photo_config::runtime::timezone());
  quiet_hours::configure({photo_config::runtime::quietHoursEnabled(),
                          photo_config::runtime::quietStartHour(),
                          photo_config::runtime::quietStartMinute(),
                          photo_config::runtime::quietEndHour(),
                          photo_config::runtime::quietEndMinute()});
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
    // Direction rules:
    //   - photoRefreshOnly (set on portal-mode exit) redraws current photo.
    //   - key1Wake (left arrow) steps back one photo.
    //   - Anything else (green button, right arrow, timer, cold boot) advances.
    int direction;
    if (photoRefreshOnly) {
      direction = 0;
      photoRefreshOnly = false;
    } else {
      direction = app_logic::photoDirection(key1Wake);
    }
    if (currentPhotoIndex < 0) currentPhotoIndex = 0;
    else currentPhotoIndex += direction;

    // Peek at where we'd land after normalisation. If a timer wake would
    // put the same photo back on the panel (typically because there's
    // only one file in /photos), skip the render entirely - the panel
    // already shows this image and a fresh 20 s refresh would just
    // burn a pigment cycle.
    const int32_t peekedIndex =
        app_logic::normalizePhotoIndex(currentPhotoIndex, photoCount);
    const bool timerWake = !coldBoot && !buttonWake;
    if (timerWake && lastRenderedIndex >= 0 &&
        peekedIndex == lastRenderedIndex) {
      currentPhotoIndex = peekedIndex;
      LOG.printf("[photo] timer wake: %ld/%lu already on panel, "
                 "skipping refresh\n",
                 static_cast<long>(peekedIndex + 1),
                 static_cast<unsigned long>(photoCount));
      displayed = true;  // suppress the "Photo unavailable" fallback
    } else {
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
            lastRenderedIndex = normalized;
            displayed = true;
            break;
          }
        }
        currentPhotoIndex += direction;
      }
    }
  }

  if (!displayed) {
    renderStatus("Photo unavailable",
                 sdReady ? "Run tools/prepare_photos.py and copy files to /photos"
                         : "Insert a FAT32 SD card");
  }

  const uint64_t configuredSleepSeconds = photo_config::runtime::sleepSeconds();
  uint64_t nextSleepSeconds = configuredSleepSeconds;
  if (local_time::localClock(localTime)) {
    if (quiet_hours::active(localTime) ||
        quiet_hours::nextWakeFallsInside(localTime, configuredSleepSeconds)) {
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
