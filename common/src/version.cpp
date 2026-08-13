#include "version.h"

// Bake a version marker into every firmware image so external tooling
// (and the SD-OTA driver's incoming-image scanner) can identify the
// build without executing it. Same __attribute__((used)) trick as the
// model tag in sd_ota.cpp.
extern "C" {
const char kFirmwareVersionTag[] = "reterminal-fw:" FIRMWARE_VERSION_LITERAL;
}

namespace board {

const char* const FIRMWARE_VERSION =
    kFirmwareVersionTag + sizeof("reterminal-fw:") - 1;

}  // namespace board
