#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

namespace calendar_http {

bool readBody(HTTPClient& http, size_t maximumBytes, uint32_t idleTimeoutMs,
              String& body, String& failureReason,
              int* transportError = nullptr);

}  // namespace calendar_http
