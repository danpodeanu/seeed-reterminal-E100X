#pragma once

#include <Arduino.h>
#include <stdint.h>

// Wi-Fi station helpers shared by every viewer app. These wrap the parts
// of the ESP32 Wi-Fi driver each app was doing identically, plus a couple
// of app-specific hooks (failure reason string, cancellation callback)
// exposed as optional parameters.
namespace wifi_sta {

// Formatted station MAC address ("AA:BB:CC:DD:EE:FF"), or "unavailable"
// if the driver can't return one. Safe to call before Wi-Fi is up.
String stationMacAddress();

// Bring the Wi-Fi radio to a low-power state: disconnect, WIFI_OFF. Idem-
// potent - returns immediately when the mode is already WIFI_MODE_NULL.
void disable();

// Optional callback returning true when a slow connect should be
// aborted before its timeout expires. xkcd uses this to poll the
// maintenance-cancel button; weather and photo pass nullptr.
using ShouldAbortFn = bool (*)();

enum class ConnectOutcome : uint8_t {
  Connected,
  NotConfigured,
  Cancelled,
  Failed,
};

struct ConnectResult {
  ConnectOutcome outcome;
  bool connected;
  uint8_t stationStatus;
  uint8_t disconnectReason;
};

// Connect to the given SSID/password with a wall-clock timeout. Detailed
// station and driver causes are returned for programmatic handling and console
// logging; `failureReason` remains the short text suitable for device screens.
ConnectResult connectStation(const char* ssid, const char* password,
                             uint32_t timeoutMs,
                             String* failureReason = nullptr,
                             ShouldAbortFn shouldAbort = nullptr);

}  // namespace wifi_sta
