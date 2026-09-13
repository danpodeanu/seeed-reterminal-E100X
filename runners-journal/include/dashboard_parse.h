#pragma once

#include "dashboard_data.h"

namespace dashboard {

bool parse(const String& body, DashboardData& out);

}  // namespace dashboard
