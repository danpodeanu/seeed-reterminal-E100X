// I2C bus and button scanner for the reTerminal E-series.
//
// E1001-E1004 expose one shared I2C bus on GPIO19/20. E1005
// (reTerminal Sticky) has a sensor bus on GPIO1/0 and a separately powered
// GT911 touch bus on GPIO3/2. Every ACK and front-button press is printed to
// UART1 and mirrored to /i2c-scan.log when an SD card is available.

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>

#include "board_pins.h"
#include "peripheral_power.h"
#include "power_latch.h"

namespace {

constexpr int kLogRxPin = board::PIN_LOG_RX;
constexpr int kLogTxPin = board::PIN_LOG_TX;
constexpr const char* kLogPath = "/i2c-scan.log";
constexpr uint32_t kScanIntervalMs = 5000;
constexpr uint32_t kButtonDebounceMs = 30;

HardwareSerial& logSerial = Serial1;
SPIClass sdSpi(HSPI);

#if RETERMINAL_MODEL == 1005
TwoWire sensorWire(1);
TwoWire touchWire(0);
#else
TwoWire sensorWire(0);
#endif

enum class BusKind {
  Sensor,
  Touch,
};

struct ButtonState {
  int pin;
  const char* name;
  int stableLevel;
  int sampledLevel;
  uint32_t changedAtMs;
};

#if RETERMINAL_MODEL == 1005
ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "OK / power", HIGH, HIGH, 0},
    {board::PIN_BUTTON_1, "UP", HIGH, HIGH, 0},
    {board::PIN_BUTTON_2, "DOWN", HIGH, HIGH, 0},
};
#else
ButtonState buttons[] = {
    {board::PIN_BUTTON_0, "GPIO3", HIGH, HIGH, 0},
    {board::PIN_BUTTON_1, "GPIO4", HIGH, HIGH, 0},
    {board::PIN_BUTTON_2, "GPIO5", HIGH, HIGH, 0},
};
#endif

bool sdMounted = false;
bool scanInProgress = false;
uint32_t sweepCounter = 0;
uint32_t nextScanAtMs = 0;
String sweepBuffer;

void appendToSd(const String& text) {
  if (!sdMounted || text.isEmpty()) return;
  File file = SD.open(kLogPath, FILE_APPEND);
  if (!file) {
    logSerial.println("[scan] could not open log file for append");
    return;
  }
  file.print(text);
  file.flush();
  file.close();
}

void bufAppend(const String& line) {
  logSerial.print(line);
  sweepBuffer += line;
}

void bufPrintf(const char* fmt, ...) {
  char buf[224];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  bufAppend(String(buf));
}

void logButtonPress(const ButtonState& button) {
  char line[128];
  snprintf(line, sizeof(line),
           "[button] %s pressed (GPIO%d, uptime=%lus)\n", button.name,
           button.pin, static_cast<unsigned long>(millis() / 1000UL));
  logSerial.print(line);
  if (scanInProgress) {
    sweepBuffer += line;
  } else {
    appendToSd(String(line));
  }
}

void pollButtons() {
  const uint32_t now = millis();
  for (ButtonState& button : buttons) {
    const int level = digitalRead(button.pin);
    if (level != button.sampledLevel) {
      button.sampledLevel = level;
      button.changedAtMs = now;
    }
    if (level != button.stableLevel &&
        static_cast<uint32_t>(now - button.changedAtMs) >=
            kButtonDebounceMs) {
      button.stableLevel = level;
      if (level == LOW) logButtonPress(button);
    }
  }
}

void responsiveDelay(uint32_t durationMs) {
  const uint32_t start = millis();
  while (static_cast<uint32_t>(millis() - start) < durationMs) {
    pollButtons();
    delay(1);
  }
}

const char* knownName(uint8_t address, BusKind kind) {
#if RETERMINAL_MODEL == 1005
  if (kind == BusKind::Touch) {
    if (address == 0x14 || address == 0x5D) return "GT911 touch controller";
    return nullptr;
  }
  switch (address) {
    case 0x44: return "SHT40 temperature/humidity sensor";
    case 0x45: return "SHT40 alternate address";
    case 0x51: return "PCF8563 RTC";
    case 0x55: return "BQ27220 battery fuel gauge";
    case 0x6A: return "LSM6DS3TR-C IMU";
    default:   return nullptr;
  }
#else
  (void)kind;
  switch (address) {
    case 0x44: return "SHT4x temperature/humidity sensor";
    case 0x51: return "PCF8563 RTC";
    case 0x5D: return "GT911 touch controller (E1003)";
    case 0x6A: return "SY6974B charger";
    case 0x6B: return "charger (SY6974B alternate / BQ25xxx family)";
    default:   return nullptr;
  }
#endif
}

bool readReg(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t& value) {
  wire.beginTransmission(address);
  wire.write(reg);
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom(address, static_cast<uint8_t>(1)) !=
      static_cast<uint8_t>(1)) {
    return false;
  }
  value = wire.read();
  return true;
}

void dumpChargerRegisters(TwoWire& wire, uint8_t address) {
#if RETERMINAL_MODEL == 1005
  (void)wire;
  (void)address;
#else
  if (address < 0x60 || address > 0x6F) return;
  bufPrintf("[scan]        register dump for 0x%02X:\n", address);
  for (uint8_t base = 0x00; base < 0x20; base += 0x08) {
    String line;
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "[scan]         %02X:", base);
    line += tmp;
    for (uint8_t offset = 0; offset < 8; ++offset) {
      uint8_t value = 0;
      if (readReg(wire, address, base + offset, value)) {
        snprintf(tmp, sizeof(tmp), " %02X", value);
      } else {
        snprintf(tmp, sizeof(tmp), " --");
      }
      line += tmp;
      pollButtons();
    }
    line += "\n";
    bufAppend(line);
  }
#endif
}

bool mountSd() {
  peripheral_power::enableSd();
  delay(board::SD_POWER_SETTLE_MS);
  pinMode(board::PIN_SD_DETECT, INPUT_PULLUP);
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  sdSpi.end();
  sdSpi.begin(board::PIN_SD_SCK, board::PIN_SD_MISO, board::PIN_SD_MOSI, -1);
  if (!SD.begin(board::PIN_SD_CS, sdSpi)) {
    logSerial.println("[scan] SD mount failed -- log will be UART-only");
    SD.end();
    sdSpi.end();
    pinMode(board::PIN_SD_CS, INPUT);
    pinMode(board::PIN_SD_SCK, INPUT);
    pinMode(board::PIN_SD_MOSI, INPUT);
    pinMode(board::PIN_SD_MISO, INPUT);
    peripheral_power::disableSd();
    return false;
  }
  logSerial.printf("[scan] SD mounted, appending to %s\n", kLogPath);
  return true;
}

void flushSweepToSd() {
  appendToSd(sweepBuffer);
  sweepBuffer = "";
}

void scanBus(TwoWire& wire, const char* busName, int sdaPin, int sclPin,
             BusKind kind) {
  bufPrintf("[scan] %s: SDA=GPIO%d SCL=GPIO%d, addresses 0x03..0x77\n",
            busName, sdaPin, sclPin);
  int found = 0;
  for (uint8_t address = 0x03; address <= 0x77; ++address) {
    wire.beginTransmission(address);
    const uint8_t error = wire.endTransmission();
    if (error == 0) {
      const char* name = knownName(address, kind);
      if (name) {
        bufPrintf("[scan]   0x%02X  ACK  <- %s\n", address, name);
      } else {
        bufPrintf("[scan]   0x%02X  ACK  <- UNKNOWN (worth investigating)\n",
                  address);
      }
      dumpChargerRegisters(wire, address);
      ++found;
    } else if (error == 4) {
      bufPrintf("[scan]   0x%02X  other error (err=4)\n", address);
    }
    responsiveDelay(2);
  }
  bufPrintf("[scan] %s done. %d device(s) responded.\n", busName, found);
}

void scanAllBuses() {
  ++sweepCounter;
  scanInProgress = true;
  bufPrintf("\n[scan] sweep #%lu (uptime=%lus)\n",
            static_cast<unsigned long>(sweepCounter),
            static_cast<unsigned long>(millis() / 1000UL));
  scanBus(sensorWire, "sensor I2C", board::PIN_I2C_SDA,
          board::PIN_I2C_SCL, BusKind::Sensor);
#if RETERMINAL_MODEL == 1005
  scanBus(touchWire, "touch I2C", board::PIN_TOUCH_SDA,
          board::PIN_TOUCH_SCL, BusKind::Touch);
#endif
  scanInProgress = false;
  flushSweepToSd();
}

#if RETERMINAL_MODEL == 1005
void resetGt911ToAddress(uint8_t address) {
  const int interruptLevel = address == 0x14 ? HIGH : LOW;
  pinMode(board::PIN_TOUCH_RESET, OUTPUT);
  pinMode(board::PIN_TOUCH_INTERRUPT, OUTPUT);
  digitalWrite(board::PIN_TOUCH_RESET, LOW);
  digitalWrite(board::PIN_TOUCH_INTERRUPT, interruptLevel);
  delay(20);
  digitalWrite(board::PIN_TOUCH_RESET, HIGH);
  delay(20);
  pinMode(board::PIN_TOUCH_INTERRUPT, INPUT);
  delay(80);
}
#endif

void configureButtons() {
  logSerial.print("[button] monitoring active-low buttons:");
  for (ButtonState& button : buttons) {
    pinMode(button.pin, INPUT_PULLUP);
    button.stableLevel = digitalRead(button.pin);
    button.sampledLevel = button.stableLevel;
    button.changedAtMs = millis();
    logSerial.printf(" %s=GPIO%d", button.name, button.pin);
  }
  logSerial.println();
}

}  // namespace

void setup() {
  power_latch::holdOn();
  logSerial.begin(115200, SERIAL_8N1, kLogRxPin, kLogTxPin);
  delay(1500);
  logSerial.println();
  logSerial.printf("=== reTerminal %s I2C + button scan ===\n",
                   board::MODEL_NAME);
  logSerial.printf("log on UART1 RX=GPIO%d TX=GPIO%d @115200\n", kLogRxPin,
                   kLogTxPin);
  configureButtons();

  if (!sensorWire.begin(board::PIN_I2C_SDA, board::PIN_I2C_SCL, 100000)) {
    logSerial.println("[scan] sensor I2C Wire.begin() FAILED -- check pins");
    return;
  }

#if RETERMINAL_MODEL == 1005
  pinMode(board::PIN_TOUCH_ENABLE, OUTPUT);
  digitalWrite(board::PIN_TOUCH_ENABLE, HIGH);
  delay(board::TOUCH_POWER_SETTLE_MS);
  if (!touchWire.begin(board::PIN_TOUCH_SDA, board::PIN_TOUCH_SCL, 100000)) {
    logSerial.println("[scan] touch I2C Wire.begin() FAILED -- check pins");
    return;
  }
  resetGt911ToAddress(0x5D);
#endif

  sdMounted = mountSd();
  scanAllBuses();

  logSerial.printf("[scan] will rescan every %lus; button presses are logged immediately.\n",
                   static_cast<unsigned long>(kScanIntervalMs / 1000UL));
  if (sdMounted) {
    logSerial.printf("[scan] scans and button presses are mirrored to %s.\n",
                     kLogPath);
  }
  nextScanAtMs = millis() + kScanIntervalMs;
}

void loop() {
  pollButtons();
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextScanAtMs) >= 0) {
    scanAllBuses();
    nextScanAtMs = millis() + kScanIntervalMs;
  }
  delay(5);
}
