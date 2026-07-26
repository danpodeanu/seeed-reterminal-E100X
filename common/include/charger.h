#pragma once

#include <stdint.h>

// Shared reader for the Silergy SY6974B battery-charger IC used across
// every reTerminal E100X board (E1001/E1002 -- V1.2 changelog "Change U2
// ETA6003 to SY6974B" --, E1003, E1004). The chip lives on I2C0 at
// address 0x6A alongside the PCF8563 RTC (0x51) and the SHT4x sensor
// (0x44), so no new bus needs to be brought up.
//
// One `readSy6974b()` call performs a single register read (~1 ms on a
// present chip, ~50 ms timeout on an absent one). Callers use the result
// only for a "connected to power" UI hint, so any failure is reported as
// `valid = false` and treated as "unknown" instead of a hard error.
namespace charger {

// State inferred from the SY6974B SYSTEM_STATUS register (0x08) PG_STAT
// bit. `Unknown` is returned when the chip does not ACK -- either the
// board revision predates the SY6974B swap (older E1001/E1002 shipped
// with ETA6003, which is not I2C-addressable) or the bus was not
// brought up.
enum class State {
  Unknown,
  Disconnected,
  Connected,
};

struct Status {
  State state = State::Unknown;
  bool valid = false;
};

// Read the SY6974B once. Requires `hardware::ensureI2cBus()` to succeed;
// the driver calls it internally so the caller does not have to.
Status readSy6974b();

}  // namespace charger
