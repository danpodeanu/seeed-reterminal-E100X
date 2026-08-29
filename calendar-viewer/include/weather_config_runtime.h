#pragma once

#include "calendar_config_runtime.h"

namespace weather_config {
namespace runtime {

inline double latitude() {
  return calendar_config::runtime::latitude();
}
inline double longitude() {
  return calendar_config::runtime::longitude();
}
inline config::WeatherProvider weatherProvider() {
  return calendar_config::runtime::weatherProvider();
}
inline bool nwsAlertsEnabled() {
  return calendar_config::runtime::nwsAlertsEnabled();
}
inline const char* qweatherHost() {
  return calendar_config::runtime::qweatherHost();
}
inline const char* qweatherProjectId() {
  return calendar_config::runtime::qweatherProjectId();
}
inline const char* qweatherCredentialId() {
  return calendar_config::runtime::qweatherCredentialId();
}
inline const char* qweatherPrivateKeyHex() {
  return calendar_config::runtime::qweatherPrivateKeyHex();
}
inline const char* qweatherLang() {
  return calendar_config::runtime::qweatherLang();
}
inline bool qweatherAlertsEnabled() {
  return calendar_config::runtime::qweatherAlertsEnabled();
}

}  // namespace runtime
}  // namespace weather_config
