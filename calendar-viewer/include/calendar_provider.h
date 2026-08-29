#pragma once

#include <Arduino.h>

#include "calendar_data.h"

namespace calendar_provider {

bool fetchIcal(const calendar::Window& window, calendar::Data& out,
               String& failureReason, bool bypassHttpCache);
bool fetchGoogle(const calendar::Window& window, calendar::Data& out,
                 String& failureReason, bool bypassHttpCache);

}  // namespace calendar_provider
