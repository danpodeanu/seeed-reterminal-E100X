#pragma once

// Firmware version shared across all apps.
//
// Local builds use the development value below. The release workflow replaces
// it with the tag version before compiling and verifies the resulting image.
// The value is baked into every firmware image in three places:
//
//   1. The boot log line ("[boot] fw <version>") - useful for confirming
//      which build is on a device without opening it up.
//   2. The Wi-Fi/config portal screen - printed under the "Device MAC"
//      line so anyone scanning a QR code can also read the version.
//   3. A rodata marker "reterminal-fw:<version>" (see version.cpp) - so
//      future tooling can inspect a .bin for its version without
//      running it. The SD-OTA driver logs this on both sides of an
//      install (running vs incoming).
//
// Kept as both a constexpr constant (for C++ callers) AND a
// preprocessor macro (for string concatenation into rodata markers),
// matching the pattern used for MODEL_NAME / MODEL_NAME_LITERAL.

#define FIRMWARE_VERSION_LITERAL "2.0.0-dev"

namespace board {

extern const char* const FIRMWARE_VERSION;

}  // namespace board
