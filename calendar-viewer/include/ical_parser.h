#pragma once

#include <stddef.h>
#include <time.h>

#include <string>

#include "calendar_data.h"

namespace ical {

bool parse(const std::string& payload, const calendar::Window& window,
           size_t maximumEvents, calendar::Data& out,
           std::string& failureReason);

}  // namespace ical
