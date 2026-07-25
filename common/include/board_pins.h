#pragma once

// Board pin table shared by all three viewer apps. The three front-panel
// button pins (3, 4, 5) intentionally stay defined per-app because each
// app names them after the semantics it uses (weather / xkcd:
// PIN_BUTTON_GREEN / RIGHT / LEFT; photo: PIN_KEY0 / KEY1 / KEY2). The
// numeric assignments never differ across apps or reTerminal E-series
// models.

namespace board {

constexpr int PIN_SD_SCK = 7;
constexpr int PIN_SD_MISO = 8;
constexpr int PIN_SD_MOSI = 9;
constexpr int PIN_SD_CS = 14;
constexpr int PIN_SD_DETECT = 15;

#if RETERMINAL_MODEL == 1003
constexpr int PIN_SD_ENABLE = 39;
constexpr int PIN_BATTERY_ENABLE = 40;
#else
constexpr int PIN_SD_ENABLE = 16;
constexpr int PIN_BATTERY_ENABLE = 21;
#endif

#if RETERMINAL_MODEL == 1001 || RETERMINAL_MODEL == 1002
constexpr int PIN_STATUS_LED = 6;
#elif RETERMINAL_MODEL == 1003
constexpr int PIN_STATUS_LED = 16;
#else
constexpr int PIN_STATUS_LED = 48;
#endif

constexpr int PIN_BUZZER = 45;
constexpr int PIN_BATTERY_ADC = 1;
constexpr int PIN_I2C_SDA = 19;
constexpr int PIN_I2C_SCL = 20;
constexpr int PIN_LOG_RX = 44;
constexpr int PIN_LOG_TX = 43;

#if RETERMINAL_MODEL == 1001
constexpr char MODEL_NAME[] = "E1001";
constexpr char COLOR_MODE_NAME[] = "Gray4";
#elif RETERMINAL_MODEL == 1002
constexpr char MODEL_NAME[] = "E1002";
constexpr char COLOR_MODE_NAME[] = "six-color";
#elif RETERMINAL_MODEL == 1003
constexpr char MODEL_NAME[] = "E1003";
constexpr char COLOR_MODE_NAME[] = "Gray16";
#elif RETERMINAL_MODEL == 1004
constexpr char MODEL_NAME[] = "E1004";
constexpr char COLOR_MODE_NAME[] = "six-color";
#endif

}  // namespace board
