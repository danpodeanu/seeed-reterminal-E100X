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
#include <esp_sleep.h>
#include <esp32-hal-psram.h>

#include "config.h"
#include "secrets.h"
#include "dither.h"
#include "image_loader.h"

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
constexpr DitherPalette PANEL_PALETTE = PAL_GRAY4;
constexpr char MODEL_NAME[] = "E1001";
constexpr char COLOR_MODE_NAME[] = "Gray4";
#elif RETERMINAL_MODEL == 1002
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr DitherPalette PANEL_PALETTE = PAL_E6;
constexpr char MODEL_NAME[] = "E1002";
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1003
constexpr uint32_t PANEL_WHITE = TFT_GRAY_15;
constexpr uint32_t PANEL_BLACK = TFT_GRAY_0;
constexpr DitherPalette PANEL_PALETTE = PAL_GRAY16;
constexpr char MODEL_NAME[] = "E1003";
constexpr char COLOR_MODE_NAME[] = "Gray16";
#elif RETERMINAL_MODEL == 1004
constexpr uint32_t PANEL_WHITE = TFT_WHITE;
constexpr uint32_t PANEL_BLACK = TFT_BLACK;
constexpr DitherPalette PANEL_PALETTE = PAL_E6;
constexpr char MODEL_NAME[] = "E1004";
constexpr char COLOR_MODE_NAME[] = "six-color";
#endif

EPaper epaper;
Adafruit_SHT4x sht4;

bool sdReady = false;
bool climateValid = false;
float temperatureC = NAN;
float humidityPct = NAN;
float batteryVoltage = NAN;
int batteryPct = -1;
RTC_DATA_ATTR uint32_t cyclesSinceLatestCheck = config::LATEST_CHECK_CYCLES;

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
      delay(config::SENSOR_RETRY_DELAY_MS);
    }
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
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

void selectFooterFont() {
#if RETERMINAL_MODEL == 1003
  epaper.setFreeFont(&FreeSansBold18pt7b);
#elif RETERMINAL_MODEL == 1004
  epaper.setFreeFont(&FreeSansBold12pt7b);
#else
  epaper.setFreeFont(&FreeSansBold9pt7b);
#endif
}

void drawBadges() {
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  selectStatusFont();

  const int statusCenterY = config::ui(24);
  epaper.fillRect(0, config::ui(5), config::ui(190), config::ui(36), PANEL_WHITE);
  epaper.setTextDatum(ML_DATUM);
  String climate = "--.-C  --%";
  if (climateValid) {
    climate = String(temperatureC, 1) + "C  " + String(humidityPct, 0) + "%";
  }
  epaper.drawString(climate, config::ui(3), statusCenterY, 1);

  epaper.fillRect(config::PANEL_WIDTH - config::ui(150), config::ui(5),
                  config::ui(150), config::ui(36), PANEL_WHITE);
  String percent = batteryPct >= 0 ? String(batteryPct) + "%" : "--%";

  // Nudge the gauge below the mathematical text center so it aligns with the
  // percentage's visible glyphs. Its terminal reaches the panel's right edge.
  const int x = config::PANEL_WIDTH - config::ui(27);
  const int w = config::ui(22);
  const int h = config::ui(12);
  const int gaugeCenterY = statusCenterY + config::ui(2);
  const int y = gaugeCenterY - h / 2;
  const int outline = max(1, config::ui(1));
  const int terminalWidth = config::PANEL_WIDTH - (x + w);
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

  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
}

void renderStatus(const String& message, const String& detail = "") {
  epaper.fillSprite(PANEL_WHITE);
  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
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
  epaper.update();
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
    return false;
  }
  if (!SD.exists(config::CACHE_DIR) && !SD.mkdir(config::CACHE_DIR)) {
    LOG.println("[sd] could not create /xkcd");
    SD.end();
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

bool httpGetString(const String& url, String& body) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(config::HTTP_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    LOG.printf("[http] GET %s -> %d\n", url.c_str(), status);
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  return !body.isEmpty();
}

bool downloadToSd(const String& url, const String& destination) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS);
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

  while (http.connected() && (declaredSize < 0 || total < static_cast<size_t>(declaredSize))) {
    const size_t available = stream->available();
    if (available > 0) {
      const size_t wanted = available < bufferSize ? available : bufferSize;
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
  if (declaredSize >= 0 && total != static_cast<size_t>(declaredSize)) ok = false;
  free(buffer);
  file.flush();
  file.close();
  http.end();

  if (!ok || total == 0) {
    SD.remove(temporary);
    return false;
  }
  SD.remove(destination);
  if (!SD.rename(temporary, destination)) {
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

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS);
  HTTPClient http;
  http.setConnectTimeout(config::HTTP_TIMEOUT_MS);
  http.setTimeout(config::DOWNLOAD_IDLE_TIMEOUT_MS);
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

  while (http.connected() && (declaredSize < 0 || total < static_cast<size_t>(declaredSize))) {
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

bool getComic(int number, bool networkAvailable, Comic& comic) {
  const String metaPath = metadataPath(number);
  String json;
  bool metadataCached = readFile(metaPath, json) && parseComic(json, comic);

  if (!metadataCached) {
    if (!networkAvailable || number == 404) return false;
    const String url = "https://xkcd.com/" + String(number) + "/info.0.json";
    if (!httpGetString(url, json) || !parseComic(json, comic)) return false;
    writeFileAtomically(metaPath, json);
    LOG.printf("[cache] saved metadata #%d\n", number);
  }

  const String extension = imageExtension(comic.imageUrl);
  if (extension.isEmpty()) {
    LOG.printf("[comic] #%d uses an unsupported image format\n", comic.number);
    return false;
  }
  comic.imagePath = String(config::CACHE_DIR) + "/" + comic.number + extension;
  if (!SD.exists(comic.imagePath)) {
    if (!networkAvailable) return false;
    LOG.printf("[comic] downloading #%d from %s\n", comic.number, comic.imageUrl.c_str());
    if (!downloadToSd(comic.imageUrl, comic.imagePath)) return false;
  } else {
    LOG.printf("[cache] using %s\n", comic.imagePath.c_str());
  }
  return true;
}

bool getLatestNumber(bool networkAvailable, int& latest) {
  String json;
  Comic comic;
  const bool shouldCheckOnline = networkAvailable &&
      (!SD.exists(config::LATEST_CACHE) ||
       cyclesSinceLatestCheck >= config::LATEST_CHECK_CYCLES);
  if (shouldCheckOnline) {
    // Whether this succeeds or falls back to the saved value, avoid hitting
    // xkcd every 15 minutes. A missing cache is retried on the next wake.
    if (SD.exists(config::LATEST_CACHE)) cyclesSinceLatestCheck = 0;
    if (httpGetString(config::XKCD_LATEST_URL, json) && parseComic(json, comic)) {
      latest = comic.number;
      cyclesSinceLatestCheck = 0;
      writeFileAtomically(config::LATEST_CACHE, json);
      writeFileAtomically(metadataPath(latest), json);
      LOG.printf("[xkcd] latest is #%d\n", latest);
      return true;
    }
  }
  if (readFile(config::LATEST_CACHE, json) && parseComic(json, comic)) {
    latest = comic.number;
    LOG.printf("[xkcd] cached latest is #%d (online check in %lu cycles)\n", latest,
               static_cast<unsigned long>(
                   cyclesSinceLatestCheck >= config::LATEST_CHECK_CYCLES
                       ? 0
                       : config::LATEST_CHECK_CYCLES - cyclesSinceLatestCheck));
    return true;
  }
  return false;
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
      if (name.endsWith(".json") && name != "latest.json") {
        name.remove(name.length() - 5);
        bool numeric = !name.isEmpty();
        for (size_t i = 0; i < name.length(); ++i) numeric &= isDigit(name[i]);
        if (numeric) {
          ++seen;
          if (random(static_cast<long>(seen)) == 0) selected = name.toInt();
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
  layout.footerDividerY = config::FOOTER_BOTTOM -
                          layout.footerLineCount * config::FOOTER_LINE_HEIGHT -
                          config::ui(5);
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

bool loadUsableComic(int number, bool networkAvailable, Comic& comic,
                     RgbImage& image, ImageLayout& layout) {
  if (!getComic(number, networkAvailable, comic)) return false;
  logMemory("before decode");
  if (!load_image_from_sd(comic.imagePath.c_str(), 0, 0, &image)) {
    LOG.printf("[comic] #%d could not be decoded\n", number);
    return false;
  }
  layout = calculateLayout(comic, image.width, image.height);
  LOG.printf("[layout] #%d source=%dx%d target=%dx%d scale=%.3f\n",
             comic.number, image.width, image.height, layout.width, layout.height,
             layout.scale);
  if (layout.scale < config::MIN_DISPLAY_SCALE) {
    LOG.printf("[comic] #%d skipped: it needs reduction below %.0f%%\n",
               comic.number, config::MIN_DISPLAY_SCALE * 100.0f);
    image_free(&image);
    return false;
  }
  return true;
}

bool acquireComic(bool networkAvailable, Comic& comic, RgbImage& image,
                  ImageLayout& layout) {
  int latest = 0;
  if (getLatestNumber(networkAvailable, latest)) {
    for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS; ++attempt) {
      int number = random(1, latest + 1);
      if (number == 404) continue;
      LOG.printf("[comic] random attempt %u: #%d\n", attempt + 1, number);
      if (loadUsableComic(number, networkAvailable, comic, image, layout)) return true;
      image_free(&image);
    }
  }

  LOG.println("[comic] trying the local SD cache");
  for (uint8_t attempt = 0; attempt < config::MAX_COMIC_ATTEMPTS; ++attempt) {
    const int number = pickRandomCachedNumber();
    if (number <= 0 || number == 404) continue;
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
    if (layout.scale >= config::MIN_DISPLAY_SCALE) return true;

    LOG.printf("[comic] live #%d skipped: it needs reduction below %.0f%%\n",
               comic.number, config::MIN_DISPLAY_SCALE * 100.0f);
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

  epaper.setTextColor(PANEL_BLACK, PANEL_WHITE, true);
  epaper.setTextDatum(MC_DATUM);
  const String heading = "XKCD #" + String(comic.number) + " - " + comic.title;
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
  epaper.setTextDatum(TC_DATUM);
  int footerY = layout.footerDividerY + config::ui(5);
  for (int i = 0; i < layout.footerLineCount; ++i) {
    epaper.drawString(layout.footerLines[i], config::PANEL_WIDTH / 2, footerY, 1);
    footerY += config::FOOTER_LINE_HEIGHT;
  }
  epaper.setFreeFont(nullptr);
  epaper.setTextFont(2);
  drawBadges();

  LOG.println("[render] refreshing panel");
  epaper.update();
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
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < config::WIFI_TIMEOUT_MS) {
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

void powerDownAndSleep() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  if (sdReady) SD.end();
  digitalWrite(PIN_SD_ENABLE, LOW);
  digitalWrite(PIN_BATTERY_ENABLE, LOW);
  if (cyclesSinceLatestCheck < config::LATEST_CHECK_CYCLES) {
    ++cyclesSinceLatestCheck;
  }

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  const uint32_t releaseStarted = millis();
  while ((!digitalRead(PIN_BUTTON_GREEN) || !digitalRead(PIN_BUTTON_RIGHT)) &&
         millis() - releaseStarted < 2000) {
    delay(10);
  }

  rtc_gpio_init(static_cast<gpio_num_t>(PIN_BUTTON_GREEN));
  rtc_gpio_set_direction(static_cast<gpio_num_t>(PIN_BUTTON_GREEN), RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(static_cast<gpio_num_t>(PIN_BUTTON_GREEN));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BUTTON_GREEN));
  rtc_gpio_init(static_cast<gpio_num_t>(PIN_BUTTON_RIGHT));
  rtc_gpio_set_direction(static_cast<gpio_num_t>(PIN_BUTTON_RIGHT), RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(static_cast<gpio_num_t>(PIN_BUTTON_RIGHT));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BUTTON_RIGHT));

  const uint64_t wakeMask = (1ULL << PIN_BUTTON_GREEN) | (1ULL << PIN_BUTTON_RIGHT);
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(config::SLEEP_SECONDS * 1000000ULL);
  LOG.printf("[sleep] %llu seconds; GPIO3/GPIO4 wake enabled\n",
             static_cast<unsigned long long>(config::SLEEP_SECONDS));
  LOG.flush();
  delay(50);
  esp_deep_sleep_start();
}

}  // namespace

void setup() {
  LOG.begin(115200, SERIAL_8N1, PIN_LOG_RX, PIN_LOG_TX);
  delay(500);
  LOG.println();
  LOG.println("============================================");
  LOG.printf(" reTerminal %s standalone XKCD / %s\n", MODEL_NAME, COLOR_MODE_NAME);
  LOG.println("============================================");

  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RIGHT, INPUT_PULLUP);
  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  LOG.printf("[boot] wake cause=%d, PSRAM=%luK\n", wakeCause,
             static_cast<unsigned long>(ESP.getPsramSize() / 1024));
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT1) beep();

  randomSeed(esp_random());
  readSensors();

  epaper.begin();
#if RETERMINAL_MODEL == 1001
  epaper.initGrayMode(GRAY_LEVEL4);
#elif RETERMINAL_MODEL == 1003
  epaper.initGrayMode(GRAY_LEVEL16);
#endif
  epaper.fillSprite(PANEL_WHITE);

  const bool coldBoot = wakeCause == ESP_SLEEP_WAKEUP_UNDEFINED;
  if (coldBoot) renderStatus("Connecting to " + String(WIFI_SSID));

  sdReady = mountSd();
  const bool networkAvailable = connectWifi();

  Comic comic;
  RgbImage image;
  ImageLayout layout;
  bool displayed = false;
  const bool acquired = sdReady
                            ? acquireComic(networkAvailable, comic, image, layout)
                            : acquireComicWithoutSd(networkAvailable, comic, image, layout);
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

  powerDownAndSleep();
}

void loop() {
  delay(1000);
}
