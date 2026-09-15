#pragma once

#include <Arduino.h>

#include "dashboard_data.h"

namespace dashboard_fetch {

bool fetch(String& body, String& failureReason);

}  // namespace dashboard_fetch
