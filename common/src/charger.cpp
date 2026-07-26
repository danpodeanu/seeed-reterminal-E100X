#include "charger.h"

#include <Arduino.h>
#include <Wire.h>

#include "app_logger.h"
#include "hardware.h"

namespace charger {
namespace {

// SY6974B-family chargers appear at either 0x6A or 0x6B depending on the
// exact part variant Seeed populated on a given board revision. Both
// share the same 12-register map (0x00..0x0B), including PG_STAT at
// register 0x08 bit 2. We probe both addresses on the first wake and
// remember the winner in RTC memory so later wakes skip straight to it.
constexpr uint8_t SY6974B_ADDR_PRIMARY = 0x6A;
constexpr uint8_t SY6974B_ADDR_ALT = 0x6B;
constexpr uint8_t REG_SYSTEM_STATUS = 0x08;
constexpr uint8_t BIT_PG_STAT = 0x04;  // bit 2

// 0 means "not resolved yet"; any other value is a cached I2C address.
// Deep-sleep wakes preserve this so we do not repeat the probe every
// refresh; a cold boot zeroes it, at which point we probe both
// candidates again.
RTC_DATA_ATTR uint8_t cachedAddress = 0;

bool readRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  // Restart, don't release the bus -- the SY6974 family expects a
  // repeated start between the register write and the read.
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(1)) !=
      static_cast<uint8_t>(1)) {
    return false;
  }
  value = Wire.read();
  return true;
}

// Try to read SYSTEM_STATUS from the given address. Returns true and
// sets `value` when the chip ACKs.
bool tryRead(uint8_t address, uint8_t& value) {
  return readRegister(address, REG_SYSTEM_STATUS, value);
}

}  // namespace

Status readSy6974b() {
  Status status;
  if (!hardware::ensureI2cBus()) return status;

  uint8_t sysStatus = 0;
  uint8_t hitAddress = 0;

  if (cachedAddress != 0) {
    if (tryRead(cachedAddress, sysStatus)) {
      hitAddress = cachedAddress;
    } else {
      // Cached address stopped responding -- reprobe from scratch.
      cachedAddress = 0;
    }
  }
  if (hitAddress == 0) {
    if (tryRead(SY6974B_ADDR_PRIMARY, sysStatus)) {
      hitAddress = SY6974B_ADDR_PRIMARY;
    } else if (tryRead(SY6974B_ADDR_ALT, sysStatus)) {
      hitAddress = SY6974B_ADDR_ALT;
    }
    if (hitAddress != 0) {
      cachedAddress = hitAddress;
      LOG.printf("[charger] SY6974B family found at 0x%02X\n", hitAddress);
    }
  }

  if (hitAddress == 0) {
    // Older E1001/E1002 shipped with an ETA6003 that does not respond
    // on either 0x6A or 0x6B. Log once per cold boot (the flag lives
    // in RTC memory and is cleared by the loader on power-on, so we
    // do not spam the log on every deep-sleep wake) then let callers
    // render as if the charger state is unknown.
    static RTC_DATA_ATTR bool absenceLogged = false;
    if (!absenceLogged) {
      LOG.println("[charger] SY6974B not present (older revision or absent)");
      absenceLogged = true;
    }
    return status;
  }

  status.valid = true;
  status.state = (sysStatus & BIT_PG_STAT) ? State::Connected
                                           : State::Disconnected;
  LOG.printf("[charger] SY6974B @0x%02X status=0x%02X external_power=%s\n",
             hitAddress, sysStatus,
             status.state == State::Connected ? "yes" : "no");
  return status;
}

}  // namespace charger
