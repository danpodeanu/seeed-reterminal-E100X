#include "gt911_touch.h"

#include <algorithm>

#include "board_pins.h"

namespace {

constexpr uint8_t kGt911AddressPrimary = 0x5D;
constexpr uint8_t kGt911AddressAlternate = 0x14;
constexpr uint16_t kRegisterProductId = 0x8140;
constexpr uint16_t kRegisterResolution = 0x8146;
constexpr uint16_t kRegisterStatus = 0x814E;
constexpr uint16_t kRegisterFirstPoint = 0x814F;
constexpr uint8_t kMaxTouchPoints = 5;

}  // namespace

bool Gt911Touch::begin(TwoWire& wire) {
  if (board::PIN_TOUCH_ENABLE < 0) return false;

  pinMode(board::PIN_TOUCH_ENABLE, OUTPUT);
  digitalWrite(board::PIN_TOUCH_ENABLE, HIGH);
  delay(board::TOUCH_POWER_SETTLE_MS);

  wire_ = &wire;
  if (!wire_->begin(board::PIN_TOUCH_SDA, board::PIN_TOUCH_SCL, 100000)) {
    wire_ = nullptr;
    return false;
  }

  constexpr uint8_t addresses[] = {
      kGt911AddressAlternate,
      kGt911AddressPrimary,
  };
  for (const uint8_t candidate : addresses) {
    if (!resetForAddress(candidate) || !probe(candidate)) continue;
    address_ = candidate;
    if (!readRegisters(kRegisterProductId,
                       reinterpret_cast<uint8_t*>(productId_), 4)) {
      address_ = 0;
      continue;
    }

    uint8_t resolution[4] = {};
    if (readRegisters(kRegisterResolution, resolution, sizeof(resolution))) {
      const uint16_t width =
          static_cast<uint16_t>(resolution[0]) |
          (static_cast<uint16_t>(resolution[1]) << 8);
      const uint16_t height =
          static_cast<uint16_t>(resolution[2]) |
          (static_cast<uint16_t>(resolution[3]) << 8);
      if (width > 0) sensorWidth_ = width;
      if (height > 0) sensorHeight_ = height;
    }
    clearStatus();
    return true;
  }

  end();
  return false;
}

bool Gt911Touch::resetForAddress(uint8_t address) {
  const int interruptLevel =
      address == kGt911AddressAlternate ? HIGH : LOW;

  pinMode(board::PIN_TOUCH_RESET, OUTPUT);
  pinMode(board::PIN_TOUCH_INTERRUPT, OUTPUT);
  digitalWrite(board::PIN_TOUCH_RESET, LOW);
  digitalWrite(board::PIN_TOUCH_INTERRUPT, interruptLevel);
  delay(20);
  digitalWrite(board::PIN_TOUCH_RESET, HIGH);
  delay(20);
  pinMode(board::PIN_TOUCH_INTERRUPT, INPUT);
  delay(80);
  return true;
}

bool Gt911Touch::probe(uint8_t address) {
  wire_->beginTransmission(address);
  return wire_->endTransmission() == 0;
}

bool Gt911Touch::poll(Point& point) {
  if (!wire_ || address_ == 0) return false;

  uint8_t status = 0;
  if (!readRegisters(kRegisterStatus, &status, 1)) return false;
  if ((status & 0x80U) == 0) return false;

  const uint8_t count = status & 0x0FU;
  if (count == 0 || count > kMaxTouchPoints) {
    clearStatus();
    return false;
  }

  uint8_t raw[8] = {};
  const bool readOk =
      readRegisters(kRegisterFirstPoint, raw, sizeof(raw));
  clearStatus();
  if (!readOk) return false;

  const uint16_t rawX =
      static_cast<uint16_t>(raw[1]) |
      (static_cast<uint16_t>(raw[2]) << 8);
  const uint16_t rawY =
      static_cast<uint16_t>(raw[3]) |
      (static_cast<uint16_t>(raw[4]) << 8);
  point.x = scale(std::min(rawX, sensorWidth_), sensorWidth_, 479);
  point.y = scale(
      static_cast<uint16_t>(sensorHeight_ - std::min(rawY, sensorHeight_)),
      sensorHeight_, 799);
  point.size =
      static_cast<uint16_t>(raw[5]) |
      (static_cast<uint16_t>(raw[6]) << 8);
  point.id = raw[0];
  return true;
}

void Gt911Touch::end() {
  if (wire_) {
    wire_->end();
    wire_ = nullptr;
  }
  address_ = 0;
  if (board::PIN_TOUCH_RESET >= 0) {
    pinMode(board::PIN_TOUCH_RESET, OUTPUT);
    digitalWrite(board::PIN_TOUCH_RESET, LOW);
  }
  if (board::PIN_TOUCH_INTERRUPT >= 0) {
    pinMode(board::PIN_TOUCH_INTERRUPT, OUTPUT);
    digitalWrite(board::PIN_TOUCH_INTERRUPT, LOW);
  }
  if (board::PIN_TOUCH_ENABLE >= 0) {
    pinMode(board::PIN_TOUCH_ENABLE, OUTPUT);
    digitalWrite(board::PIN_TOUCH_ENABLE, LOW);
  }
}

bool Gt911Touch::readRegisters(uint16_t reg, uint8_t* data, size_t length) {
  if (!wire_ || address_ == 0 || !data || length == 0) return false;
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(reg >> 8));
  wire_->write(static_cast<uint8_t>(reg & 0xFF));
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom(address_, static_cast<uint8_t>(length)) != length) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    data[i] = wire_->read();
  }
  return true;
}

bool Gt911Touch::writeRegister(uint16_t reg, uint8_t value) {
  if (!wire_ || address_ == 0) return false;
  wire_->beginTransmission(address_);
  wire_->write(static_cast<uint8_t>(reg >> 8));
  wire_->write(static_cast<uint8_t>(reg & 0xFF));
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

void Gt911Touch::clearStatus() {
  (void)writeRegister(kRegisterStatus, 0);
}

uint16_t Gt911Touch::scale(uint16_t value, uint16_t sourceMax,
                           uint16_t targetMax) {
  if (sourceMax == 0) return 0;
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(value) * targetMax + sourceMax / 2U) /
      sourceMax);
}
