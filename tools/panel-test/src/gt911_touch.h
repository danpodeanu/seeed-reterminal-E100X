#pragma once

#include <Arduino.h>
#include <Wire.h>

class Gt911Touch {
 public:
  struct Point {
    uint16_t x;
    uint16_t y;
    uint16_t size;
    uint8_t id;
  };

  bool begin(TwoWire& wire);
  bool poll(Point& point);
  void end();

  uint8_t address() const { return address_; }
  const char* productId() const { return productId_; }
  uint16_t sensorWidth() const { return sensorWidth_; }
  uint16_t sensorHeight() const { return sensorHeight_; }

 private:
  bool resetForAddress(uint8_t address);
  bool probe(uint8_t address);
  bool readRegisters(uint16_t reg, uint8_t* data, size_t length);
  bool writeRegister(uint16_t reg, uint8_t value);
  void clearStatus();
  static uint16_t scale(uint16_t value, uint16_t sourceMax,
                        uint16_t targetMax);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0;
  char productId_[5] = {};
  uint16_t sensorWidth_ = 480;
  uint16_t sensorHeight_ = 800;
};
