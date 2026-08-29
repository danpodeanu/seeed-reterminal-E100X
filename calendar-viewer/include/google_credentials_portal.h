#pragma once

#include <Arduino.h>

class WebServer;

namespace config_portal {
struct Config;
}

namespace google_credentials_portal {

void attachRoutes(WebServer& server, const config_portal::Config& portalConfig);

}  // namespace google_credentials_portal
