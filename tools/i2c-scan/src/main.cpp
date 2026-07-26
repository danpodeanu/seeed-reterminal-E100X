// I2C bus scanner for reTerminal E100X.
//
// Sweeps the standard 7-bit I2C address range on the shared I2C0 bus
// (SDA=GPIO19, SCL=GPIO20) and prints every address that ACKs. Well-
// known chips are annotated; anything else is flagged as UNKNOWN, and
// devices in the charger address range (0x60..0x6F) get an additional
// 32-register hex dump so unfamiliar charger silicon can be fingerprinted
// from the log alone.
//
// Expected devices:
//   0x44  SHT4x  (temperature/humidity)
//   0x51  PCF8563 (RTC)
//   0x5D  GT911 touch controller (E1003 only)
//   0x6A  SY6974B (charger, primary I2C address variant)
//   0x6B  SY6974B / BQ25xxx family (charger, alternate address variant)
//
// Anything outside this table is worth investigating.
//
// Safety notes:
//   - Only READs are ever issued to charger addresses. The SY6974 and
//     BQ25xxx families ignore reads to undefined register addresses
//     (they return 0xFF), so poking 0x00..0x1F does not disturb chip
//     state. Writes could disable charging or change current limits;
//     do not add any without deliberate care.
//   - Register dumps are limited to the 0x60..0x6F charger range so
//     we do not send single-byte reads at devices (like GT911) that
//     use a 16-bit register interface.
//
// Output routing: the reTerminal apps log via UART1 on GPIO43/44 (which
// is where the on-board USB-serial bridge is wired), not via the S3's
// native USB CDC. We do the same here so the output shows up on the
// same COM/ttyUSB port used for flashing. Each sweep is also mirrored
// to /i2c-scan.log on the SD card (when a card is inserted) so you can
// unplug the USB, exercise the device, and inspect the log later.

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>

#include "board_pins.h"

namespace {

constexpr int kSdaPin = board::PIN_I2C_SDA;
constexpr int kSclPin = board::PIN_I2C_SCL;
constexpr int kLogRxPin = board::PIN_LOG_RX;
constexpr int kLogTxPin = board::PIN_LOG_TX;

constexpr const char* kLogPath = "/i2c-scan.log";

// Alias to keep the print calls readable and easy to retarget later.
HardwareSerial& logSerial = Serial1;

SPIClass sdSpi(HSPI);
bool sdMounted = false;
uint32_t sweepCounter = 0;

// -------- Buffered logging --------
//
// Each sweep composes its lines into a single String so we can (a) print
// them to UART1 as they happen and (b) append the whole block to
// /i2c-scan.log in one open/write/flush/close cycle. Opening the file
// per sweep instead of per line keeps FAT overhead low; closing the file
// after every sweep keeps the FAT entries flushed so a sudden power loss
// (e.g. USB unplug) cannot corrupt the filesystem.
String sweepBuffer;

void bufAppend(const String& line) {
  logSerial.print(line);
  sweepBuffer += line;
}

// printf-style helper that both prints and appends to the sweep buffer.
void bufPrintf(const char* fmt, ...) {
  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  bufAppend(String(buf));
}

// -------- I2C helpers --------

const char* knownName(uint8_t addr) {
  switch (addr) {
    case 0x44: return "SHT4x (temp/humidity)";
    case 0x51: return "PCF8563 (RTC)";
    case 0x5D: return "GT911 touch controller (E1003)";
    case 0x6A: return "SY6974B (charger)";
    case 0x6B: return "charger @ 0x6B (SY6974B alt / BQ25xxx family)";
    default:   return nullptr;
  }
}

bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, static_cast<uint8_t>(1)) !=
      static_cast<uint8_t>(1)) {
    return false;
  }
  value = Wire.read();
  return true;
}

void dumpChargerRegisters(uint8_t addr) {
  if (addr < 0x60 || addr > 0x6F) return;
  bufPrintf("[scan]        register dump for 0x%02X:\n", addr);
  for (uint8_t base = 0x00; base < 0x20; base += 0x08) {
    String line;
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "[scan]         %02X:", base);
    line += tmp;
    for (uint8_t off = 0; off < 8; ++off) {
      uint8_t v = 0;
      if (readReg(addr, base + off, v)) {
        snprintf(tmp, sizeof(tmp), " %02X", v);
      } else {
        snprintf(tmp, sizeof(tmp), " --");
      }
      line += tmp;
    }
    line += "\n";
    bufAppend(line);
  }
}

// -------- SD helpers --------

bool mountSd() {
  pinMode(board::PIN_SD_ENABLE, OUTPUT);
  digitalWrite(board::PIN_SD_ENABLE, HIGH);
  pinMode(board::PIN_SD_DETECT, INPUT_PULLUP);
  pinMode(board::PIN_SD_CS, OUTPUT);
  digitalWrite(board::PIN_SD_CS, HIGH);
  delay(50);
  sdSpi.end();
  sdSpi.begin(board::PIN_SD_SCK, board::PIN_SD_MISO, board::PIN_SD_MOSI, -1);
  if (!SD.begin(board::PIN_SD_CS, sdSpi)) {
    logSerial.println("[scan] SD mount failed -- log will be UART-only");
    digitalWrite(board::PIN_SD_ENABLE, LOW);
    return false;
  }
  logSerial.printf("[scan] SD mounted, appending to %s\n", kLogPath);
  return true;
}

// Persist the buffered sweep to SD. Open, write, flush, close on every
// call so the FAT is consistent between sweeps -- a power loss (or USB
// unplug on a device that runs off USB power) at any time between
// sweeps leaves a fully-formed log file rather than an uncommitted
// extend that fsck would need to reclaim.
void flushSweepToSd() {
  if (!sdMounted) {
    sweepBuffer = "";
    return;
  }
  File f = SD.open(kLogPath, FILE_APPEND);
  if (!f) {
    logSerial.println("[scan] could not open log file for append");
    sweepBuffer = "";
    return;
  }
  f.print(sweepBuffer);
  f.flush();
  f.close();
  sweepBuffer = "";
}

// -------- Sweep --------

void scanBus() {
  ++sweepCounter;
  bufPrintf("\n[scan] sweep #%lu (uptime=%lus)\n",
            static_cast<unsigned long>(sweepCounter),
            static_cast<unsigned long>(millis() / 1000UL));
  bufAppend("[scan] sweeping addresses 0x03..0x77 ...\n");
  int found = 0;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t err = Wire.endTransmission();
    if (err == 0) {
      const char* name = knownName(addr);
      if (name) {
        bufPrintf("[scan]   0x%02X  ACK  <- %s\n", addr, name);
      } else {
        bufPrintf("[scan]   0x%02X  ACK  <- UNKNOWN (worth investigating)\n",
                  addr);
      }
      dumpChargerRegisters(addr);
      ++found;
    } else if (err == 4) {
      bufPrintf("[scan]   0x%02X  other error (err=4)\n", addr);
    }
    // err == 2 (NACK on address) is the boring "nothing there" case.
    delay(2);
  }
  bufPrintf("[scan] done. %d device(s) responded.\n", found);
  flushSweepToSd();
}

}  // namespace

void setup() {
  logSerial.begin(115200, SERIAL_8N1, kLogRxPin, kLogTxPin);
  // Give the host serial monitor a moment to attach after the flash reset
  // before we print the header, otherwise the first sweep can scroll off.
  delay(1500);
  logSerial.println();
  logSerial.println("=== reTerminal E100X I2C bus scan ===");
  logSerial.printf("SDA=GPIO%d  SCL=GPIO%d  clock=100kHz\n", kSdaPin, kSclPin);
  logSerial.printf("log on UART1 RX=GPIO%d TX=GPIO%d @115200\n", kLogRxPin,
                   kLogTxPin);

  if (!Wire.begin(kSdaPin, kSclPin)) {
    logSerial.println("[scan] Wire.begin() FAILED -- check pins");
    return;
  }
  Wire.setClock(100000);

  sdMounted = mountSd();

  scanBus();

  logSerial.println("[scan] will rescan every 5 s. Plug/unplug USB to see if");
  logSerial.println("[scan] any address appears or disappears with charger state.");
  logSerial.println("[scan] Sweeps are also mirrored to /i2c-scan.log on SD.");
}

void loop() {
  delay(5000);
  scanBus();
}
