#include "epaper_setup.h"

#include <Arduino.h>

#include "board_pins.h"

namespace epaper_setup {

void finalize(SPIClass& panelSpi) {
  pinMode(board::PIN_SD_ENABLE, OUTPUT);
  digitalWrite(board::PIN_SD_ENABLE, HIGH);
  panelSpi.end();
  panelSpi.begin(board::PIN_SD_SCK, board::PIN_SD_MISO, board::PIN_SD_MOSI, -1);
}

}  // namespace epaper_setup
