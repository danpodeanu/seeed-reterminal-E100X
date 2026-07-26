// I2C bus scanner for reTerminal E100X.
//
// Sweeps the standard 7-bit I2C address range on the shared I2C0 bus
// (SDA=GPIO19, SCL=GPIO20) and prints every address that ACKs. Also
// annotates the well-known chips on this bus so it's obvious which
// entries correspond to the RTC, humidity sensor, and charger IC.
//
// Expected devices:
//   0x44  SHT4x  (temperature/humidity)
//   0x51  PCF8563 (RTC)
//   0x6A  SY6974B (battery charger, V1.2+ E1001/E1002 and all E1003/E1004)
//
// Anything else that ACKs is worth investigating -- in particular, a
// device that shows up somewhere other than 0x6A on an old E1001/E1002
// unit could be the charger IC at a nonstandard address.

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr int kSdaPin = 19;
constexpr int kSclPin = 20;

const char* knownName(uint8_t addr) {
  switch (addr) {
    case 0x44: return "SHT4x (temp/humidity)";
    case 0x51: return "PCF8563 (RTC)";
    case 0x6A: return "SY6974B (charger)";
    default:   return nullptr;
  }
}

void scanBus() {
  Serial.println();
  Serial.println("[scan] sweeping addresses 0x03..0x77 ...");
  int found = 0;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    const uint8_t err = Wire.endTransmission();
    if (err == 0) {
      const char* name = knownName(addr);
      if (name) {
        Serial.printf("[scan]   0x%02X  ACK  <- %s\n", addr, name);
      } else {
        Serial.printf("[scan]   0x%02X  ACK  <- UNKNOWN (worth investigating)\n",
                      addr);
      }
      ++found;
    } else if (err == 4) {
      Serial.printf("[scan]   0x%02X  other error (err=4)\n", addr);
    }
    // err == 2 (NACK on address) is the boring "nothing there" case.
    delay(2);
  }
  Serial.printf("[scan] done. %d device(s) responded.\n", found);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }
  Serial.println();
  Serial.println("=== reTerminal E100X I2C bus scan ===");
  Serial.printf("SDA=GPIO%d  SCL=GPIO%d  clock=100kHz\n", kSdaPin, kSclPin);

  if (!Wire.begin(kSdaPin, kSclPin)) {
    Serial.println("[scan] Wire.begin() FAILED -- check pins");
    return;
  }
  Wire.setClock(100000);

  scanBus();

  Serial.println("[scan] will rescan every 5 s. Plug/unplug USB to see if");
  Serial.println("[scan] any address appears or disappears with charger state.");
}

void loop() {
  delay(5000);
  scanBus();
}
