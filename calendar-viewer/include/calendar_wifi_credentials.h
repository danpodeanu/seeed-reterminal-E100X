#pragma once

#include <Arduino.h>

namespace calendar_wifi {

void load();
bool recordPasswordOverride(bool passwordIsEmpty, String& failureReason);
bool haveCredentials();
bool nvsEmpty();
const char* ssid();
const char* password();

}  // namespace calendar_wifi
