#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// SD-card Wi-Fi portal shared by the tools/sd-web utility and, later,
// any viewer app that wants to expose its SD card over Wi-Fi for
// operator-side maintenance (uploading fonts, deleting cache dirs,
// pulling logs, etc.).
//
// Runtime flow, once `begin()` has succeeded:
//   1. The ESP32 is running as a soft-AP on `apIp` (default 192.168.1.1),
//      subnet /24, with DHCP handing out `apIp` + 1 onward to clients.
//   2. An HTTP server is listening on `httpPort` (default 80).
//   3. The caller pumps `loop()` from their Arduino loop().
//
// Preconditions the caller must satisfy before begin():
//   * Panel SPI has been finalised (epaper_setup::finalize).
//   * The SD card has been mounted (sd_card::mount).
// The portal does NOT mount the SD itself so an embedding app can share
// its own mount, and so this file never touches epaper/SD headers -
// keeping the header cheap to include from anywhere.
namespace sd_web_portal {

struct Config {
  // Prefix for the auto-generated AP SSID. The full SSID is
  // "<prefix><first 6 hex digits of the AP MAC, no colons>" which for
  // MAC AA:BB:CC:DD:EE:FF and prefix "ReTerminal " yields
  // "ReTerminal AABBCC". Six digits identify one reTerminal well enough
  // within a room of them while still fitting on a small QR code.
  const char* apSsidPrefix = "ReTerminal ";

  // Password for the AP. When null/empty the AP is open (WPA-none),
  // which is what the standalone tool ships with because the QR codes
  // assume no credential.
  const char* apPassword = nullptr;

  // Static IP configuration for the AP. Kept as literals so the QR code
  // payload can be composed from these fields with no round-trip
  // through the softAP object.
  IPAddress apIp{192, 168, 1, 1};
  IPAddress apGateway{192, 168, 1, 1};
  IPAddress apNetmask{255, 255, 255, 0};

  // TCP port for the built-in web server.
  uint16_t httpPort = 80;

  // Maximum simultaneous Wi-Fi clients. ESP32 supports up to 10 STA
  // clients on softAP; four is plenty for a maintenance portal and
  // keeps the DHCP pool small enough to be predictable.
  uint8_t maxConnections = 4;
};

// Build the SSID that begin() would use for the given config, without
// starting the AP. Handy for UI code that wants to display the SSID
// before begin() runs (or for tests). Reads the AP MAC via esp_read_mac.
String buildSsid(const Config& cfg = Config{});

// Format the WPA-none Wi-Fi QR payload for the given SSID. Encodes as
//   WIFI:S:<ssid>;T:nopass;;
// with the SSID characters ; \ , " : escaped per the WPA QR spec. When
// `password` is non-empty, emits T:WPA;P:<password>;; instead.
String wifiQrPayload(const String& ssid, const char* password = nullptr);

// Format the browser QR payload for the given AP IP + port.
String urlQrPayload(const IPAddress& ip, uint16_t port = 80);

// Start the AP + HTTP server. Returns true on success. Idempotent
// against duplicate calls (a second call re-applies config).
bool begin(const Config& cfg = Config{});

// Service in-flight HTTP requests. Call every loop() iteration; blocks
// only for the duration of a single request or client-connection tick.
void loop();

// Cheap accessors valid after begin(). Empty string / 0.0.0.0 before.
const String& currentSsid();
IPAddress currentIp();
uint16_t currentPort();

// Stop the HTTP server and shut down the AP. Not normally needed - the
// portal is a one-shot power-cycle tool - but useful for embedding apps
// that want to run the portal on demand.
void end();

}  // namespace sd_web_portal
