// I2C bus scanner for reTerminal E100X.
//
// Sweeps the standard 7-bit I2C address range on the shared I2C0 bus
// (SDA=GPIO19, SCL=GPIO20) and prints every address that ACKs. Well-
// known chips are annotated; anything else is flagged as UNKNOWN.
//
// Expected devices:
//   0x44  SHT4x  (temperature/humidity)
//   0x51  PCF8563 (RTC)
//   0x6A  SY6974B (battery charger, V1.2+ E1001/E1002 and all E1003/E1004)
//
// Anything outside this table is worth investigating -- in particular,
// a device that shows up somewhere other than 0x6A on an old E1001/E1002
// unit could be the charger IC at a nonstandard address.
//
// Output routing: the reTerminal apps log via UART1 on GPIO43/44 (which
// is where the on-board USB-serial bridge is wired), not via the S3's
// native USB CDC. We do the same here so the output shows up on the
// same COM/ttyUSB port used for flashing.

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr int kSdaPin = 19;
constexpr int kSclPin = 20;
constexpr int kLogRxPin = 44;
constexpr int kLogTxPin = 43;

// Alias to keep the print calls readable and easy to retarget later.
HardwareSerial& logSerial = Serial1;

const char* knownName(uint8_t addr) {
  switch (addr) {
    case 0x44: return "SHT4x (temp/humidity)";
    case 0x51: return "PCF8563 (RTC)";
    case 0x5D: return "GT911 touch controller (E1003)";
    case 0x6A: return "SY6974B (charger)";
    case 0x6B: return "charger @ 0x6B (BQ25895 / BQ25896 / SY6970 family)";
    default:   return nullptr;
  }
}

// Read one 8-bit register. Returns true on ACK, false on NACK / timeout.
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

// Dump the first 32 registers of a charger-family device so the chip can
// be fingerprinted from the log. Limited to 0x60..0x6F to avoid poking at
// devices (like touch controllers) that may not respond well to arbitrary
// register reads.
void dumpChargerRegisters(uint8_t addr) {
  if (addr < 0x60 || addr > 0x6F) return;
  logSerial.printf("[scan]        register dump for 0x%02X:\n", addr);
  for (uint8_t base = 0x00; base < 0x20; base += 0x08) {
    logSerial.printf("[scan]         %02X:", base);
    for (uint8_t off = 0; off < 8; ++off) {
      uint8_t v = 0;
      if (readReg(addr, base + off, v)) {
        logSerial.printf(" %02X", v);
      } else {
        logSerial.print(" --");
      }
    }
    logSerial.println();
  }
}

void scanBus() {
  logSerial.println();
  logSerial.println("[scan] sweeping addresses 0x03..0x77 ...");
  int found = 0;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t err = Wire.endTransmission();
    if (err == 0) {
      const char* name = knownName(addr);
      if (name) {
        logSerial.printf("[scan]   0x%02X  ACK  <- %s\n", addr, name);
      } else {
        logSerial.printf("[scan]   0x%02X  ACK  <- UNKNOWN (worth investigating)\n",
                         addr);
      }
      dumpChargerRegisters(addr);
      ++found;
    } else if (err == 4) {
      logSerial.printf("[scan]   0x%02X  other error (err=4)\n", addr);
    }
    // err == 2 (NACK on address) is the boring "nothing there" case.
    delay(2);
  }
  logSerial.printf("[scan] done. %d device(s) responded.\n", found);
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

  scanBus();

  logSerial.println("[scan] will rescan every 5 s. Plug/unplug USB to see if");
  logSerial.println("[scan] any address appears or disappears with charger state.");
}

void loop() {
  delay(5000);
  scanBus();
}
