#pragma once

#include <stdint.h>

// Board pin table shared by the viewer apps and hardware tools. E1005
// (reTerminal Sticky) has a different board design: separate display and SD
// power rails, a second I2C bus for touch, and different button GPIOs.

namespace board {

#if RETERMINAL_MODEL == 1003
constexpr int PIN_PERIPHERAL_ENABLE = 39;
constexpr int PIN_BATTERY_ENABLE = 40;
#elif RETERMINAL_MODEL == 1005
constexpr int PIN_PERIPHERAL_ENABLE = 47;
constexpr int PIN_BATTERY_ENABLE = -1;
#else
constexpr int PIN_PERIPHERAL_ENABLE = 16;
constexpr int PIN_BATTERY_ENABLE = 21;
#endif

#if RETERMINAL_MODEL == 1005
constexpr int PIN_SD_SCK = 13;
constexpr int PIN_SD_MISO = 12;
constexpr int PIN_SD_MOSI = 14;
constexpr int PIN_SD_CS = 8;
constexpr int PIN_SD_DETECT = 11;
constexpr int PIN_SD_ENABLE = 10;
constexpr uint32_t SD_POWER_SETTLE_MS = 100;
#else
constexpr int PIN_SD_SCK = 7;
constexpr int PIN_SD_MISO = 8;
constexpr int PIN_SD_MOSI = 9;
constexpr int PIN_SD_CS = 14;
constexpr int PIN_SD_DETECT = 15;
constexpr int PIN_SD_ENABLE = PIN_PERIPHERAL_ENABLE;
constexpr uint32_t SD_POWER_SETTLE_MS = 50;
#endif

#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
constexpr int PIN_STATUS_LED = 6;
#elif RETERMINAL_MODEL == 1003
constexpr int PIN_STATUS_LED = 16;
#elif RETERMINAL_MODEL == 1004
constexpr int PIN_STATUS_LED = 48;
#else
constexpr int PIN_STATUS_LED = -1;
#endif

constexpr int PIN_BUZZER =
    RETERMINAL_MODEL == 1005 ? 48 : 45;
constexpr int PIN_BATTERY_ADC =
    RETERMINAL_MODEL == 1005 ? -1 : 1;

#if RETERMINAL_MODEL == 1005
constexpr int PIN_I2C_SDA = 1;
constexpr int PIN_I2C_SCL = 0;
constexpr int PIN_TOUCH_SDA = 3;
constexpr int PIN_TOUCH_SCL = 2;
constexpr int PIN_TOUCH_ENABLE = 42;
constexpr int PIN_TOUCH_INTERRUPT = 21;
constexpr int PIN_TOUCH_RESET = 41;
constexpr uint32_t TOUCH_POWER_SETTLE_MS = 250;
constexpr int PIN_POWER_HOLD = 45;
constexpr int PIN_POWER_LOCK = 46;
// Active high when a valid external power source is present.
constexpr int PIN_EXTERNAL_POWER = 9;
constexpr int PIN_BUTTON_0 = 4;
constexpr int PIN_BUTTON_1 = 5;
constexpr int PIN_BUTTON_2 = 6;
#else
constexpr int PIN_I2C_SDA = 19;
constexpr int PIN_I2C_SCL = 20;
constexpr int PIN_TOUCH_SDA = -1;
constexpr int PIN_TOUCH_SCL = -1;
constexpr int PIN_TOUCH_ENABLE = -1;
constexpr int PIN_TOUCH_INTERRUPT = -1;
constexpr int PIN_TOUCH_RESET = -1;
constexpr uint32_t TOUCH_POWER_SETTLE_MS = 0;
constexpr int PIN_POWER_HOLD = -1;
constexpr int PIN_POWER_LOCK = -1;
constexpr int PIN_EXTERNAL_POWER = -1;
constexpr int PIN_BUTTON_0 = 3;
constexpr int PIN_BUTTON_1 = 4;
constexpr int PIN_BUTTON_2 = 5;
#endif

constexpr int PIN_LOG_RX = 44;
constexpr int PIN_LOG_TX = 43;

#if RETERMINAL_MODEL == 1001
#define MODEL_NAME_LITERAL "E1001"
constexpr char COLOR_MODE_NAME[] = "Gray4";
#elif RETERMINAL_MODEL == 1002
#define MODEL_NAME_LITERAL "E1002"
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1003
#define MODEL_NAME_LITERAL "E1003"
constexpr char COLOR_MODE_NAME[] = "Gray16";
#elif RETERMINAL_MODEL == 1004
#define MODEL_NAME_LITERAL "E1004"
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1005
#define MODEL_NAME_LITERAL "E1005"
constexpr char COLOR_MODE_NAME[] = "monochrome";
#else
#error "Unsupported RETERMINAL_MODEL"
#endif
constexpr char MODEL_NAME[] = MODEL_NAME_LITERAL;

}  // namespace board
