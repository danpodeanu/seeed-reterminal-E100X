#pragma once

// Copy this file to `secrets.h` and fill in the credentials for the Wi-Fi
// network you want the sd-web tool to use for NTP synchronisation. The
// credentials are NOT used to serve the portal - the tool always exposes
// its own open access point for the phone/laptop to connect to. They are
// only used briefly at boot to hit an NTP server so that uploaded files
// get a real modification timestamp instead of the FAT epoch.
//
// If you don't want NTP sync (e.g. running the tool on a network you can
// only reach through a captive portal), leave WIFI_SSID empty. The tool
// will fall back to the on-board PCF8563 RTC only.

constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
