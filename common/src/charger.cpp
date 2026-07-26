#include "charger.h"

#include <Arduino.h>
#include <Wire.h>

#include "app_logger.h"
#include "hardware.h"

namespace charger {
namespace {

constexpr uint8_t SY6974B_ADDR = 0x6A;
constexpr uint8_t REG_SYSTEM_STATUS = 0x08;
constexpr uint8_t BIT_PG_STAT = 0x04;  // bit 2

bool readRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(SY6974B_ADDR);
  Wire.write(reg);
  // Restart, don't release the bus -- SY6974B expects a repeated start.
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(SY6974B_ADDR, static_cast<uint8_t>(1)) !=
      static_cast<uint8_t>(1)) {
    return false;
  }
  value = Wire.read();
  return true;
}

}  // namespace

Status readSy6974b() {
  Status status;
  if (!hardware::ensureI2cBus()) return status;
  uint8_t sysStatus = 0;
  if (!readRegister(REG_SYSTEM_STATUS, sysStatus)) {
    // Older E1001/E1002 shipped with an ETA6003 that does not respond
    // on 0x6A. Log once at INFO so the absence isn't confusing, then
    // let callers render as if the charger state is unknown.
    LOG.println("[charger] SY6974B not present (older revision or absent)");
    return status;
  }
  status.valid = true;
  status.state = (sysStatus & BIT_PG_STAT) ? State::Connected
                                           : State::Disconnected;
  LOG.printf("[charger] SY6974B status=0x%02X external_power=%s\n", sysStatus,
             status.state == State::Connected ? "yes" : "no");
  return status;
}

}  // namespace charger
