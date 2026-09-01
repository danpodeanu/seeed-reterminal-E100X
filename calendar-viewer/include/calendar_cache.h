#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "calendar_data.h"

namespace calendar_cache {

bool load(uint64_t identity, calendar::Data& data,
          calendar::Window& window, String& failureReason);

bool save(uint64_t identity, const calendar::Data& data,
          const calendar::Window& window, String& failureReason);

}  // namespace calendar_cache
