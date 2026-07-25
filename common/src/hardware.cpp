#include "hardware.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app_logger.h"
#include "board_pins.h"

namespace hardware {
namespace {

bool i2cReady = false;

}  // namespace

void setStatusLed(bool on) {
  pinMode(board::PIN_STATUS_LED, OUTPUT);
  digitalWrite(board::PIN_STATUS_LED, on ? LOW : HIGH);
}

void beep() {
  pinMode(board::PIN_BUZZER, OUTPUT);
  tone(board::PIN_BUZZER, 1000, 100);
  delay(120);
  noTone(board::PIN_BUZZER);
  digitalWrite(board::PIN_BUZZER, LOW);
}

bool ensureI2cBus() {
  if (i2cReady) return true;
  i2cReady = Wire.begin(board::PIN_I2C_SDA, board::PIN_I2C_SCL);
  if (!i2cReady) {
    LOG.println("[rtc] could not initialize the I2C bus");
    return false;
  }
  Wire.setClock(100000);
  return true;
}

void resetI2cBus() { i2cReady = false; }

bool configureWakePin(int pin) {
  pinMode(pin, INPUT_PULLUP);
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  rtc_gpio_hold_dis(gpio);
  return rtc_gpio_init(gpio) == ESP_OK &&
         rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY) == ESP_OK &&
         rtc_gpio_pullup_en(gpio) == ESP_OK &&
         rtc_gpio_pulldown_dis(gpio) == ESP_OK;
}

}  // namespace hardware
