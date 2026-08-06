#pragma once

#include <stdint.h>
#include <stdio.h>

namespace portal_identity {

inline void formatSsidSuffix(const uint8_t mac[6], char suffix[5]) {
  snprintf(suffix, 5, "%02X%02X", mac[4], mac[5]);
}

}  // namespace portal_identity
